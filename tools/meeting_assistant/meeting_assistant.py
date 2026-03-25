"""
ESPro AI Companion App
======================
Bridges the ESPro AI smartwatch to desktop meeting apps (Google Meet, Teams, Zoom).

Architecture:
    Watch <--MQTT--> This App <--Keyboard Shortcuts--> Meeting Apps
                              <--System Audio Capture--> AI Notes (Groq STT + LLM)

Features:
    - Auto-detect active meeting window (Meet/Teams/Zoom)
    - Keyboard shortcut control for mic/video/hand-raise
    - System audio capture → Groq Whisper STT → live transcription
    - End-of-meeting summary via Groq LLM
    - Hand-off mode (captures discussion while user is away)
    - Audio device monitoring
    - System tray icon for background operation

Usage:
    python meeting_assistant.py              # Run with config.json in same dir
    python meeting_assistant.py --config C:\\path\\to\\config.json
"""

import json
import sys
import time
import threading
import logging
import argparse
import os
import signal
from pathlib import Path

# Local modules
from meeting_detector import MeetingDetector
from meeting_controller import MeetingController
from audio_capture import AudioCapture
from mqtt_bridge import MQTTBridge
from ai_notes import AINotes

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(name)s] %(levelname)s: %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("main")


class MeetingAssistant:
    """Main orchestrator for the desktop meeting companion."""

    def __init__(self, config_path: str):
        with open(config_path, "r") as f:
            self.config = json.load(f)

        self.running = False
        self.meeting_active = False
        self.current_app = "none"
        self.handoff_active = False
        self.handoff_notes = []

        # Components
        self.mqtt = MQTTBridge(self.config["mqtt"], self._on_watch_command)
        self.detector = MeetingDetector(self.config["meeting_detection"])
        self.controller = MeetingController(self.config["keyboard_shortcuts"])
        self.ai_notes = AINotes(self.config.get("groq_api_key", ""))

        self.audio_capture = None
        if self.config.get("audio", {}).get("enabled", False):
            self.audio_capture = AudioCapture(self.config["audio"])

    def start(self):
        """Start all components and the main loop."""
        log.info("Starting ESPro AI Companion App...")
        self.running = True

        # Connect MQTT
        self.mqtt.connect()
        time.sleep(1)

        # Start main detection loop in a thread
        self._detection_thread = threading.Thread(
            target=self._detection_loop, daemon=True
        )
        self._detection_thread.start()

        log.info("ESPro AI Companion App running. Press Ctrl+C to stop.")

    def stop(self):
        """Stop all components gracefully."""
        log.info("Stopping ESPro AI Companion App...")
        self.running = False

        if self.meeting_active:
            self._end_meeting()

        if self.audio_capture:
            self.audio_capture.stop()

        self.mqtt.publish_state("active", "0")
        self.mqtt.disconnect()
        log.info("Stopped.")

    def _detection_loop(self):
        """Poll for active meeting windows."""
        poll_s = self.config["meeting_detection"].get("poll_interval_s", 2)

        while self.running:
            try:
                app, title = self.detector.detect()

                if app != "none" and not self.meeting_active:
                    self._start_meeting(app, title)
                elif app == "none" and self.meeting_active:
                    self._end_meeting()
                elif app != "none" and app != self.current_app:
                    # Switched meeting apps
                    log.info(f"Meeting app changed: {self.current_app} → {app}")
                    self.current_app = app
                    self.mqtt.publish_state("app", app)

                # Periodically update mic/video/audio device state
                if self.meeting_active:
                    self._update_state()

            except Exception as e:
                log.error(f"Detection error: {e}")

            time.sleep(poll_s)

    def _start_meeting(self, app: str, title: str):
        """Handle meeting start."""
        log.info(f"Meeting detected: {app} — {title}")
        self.meeting_active = True
        self.current_app = app
        self.handoff_notes = []

        self.mqtt.publish_state("active", "1")
        self.mqtt.publish_state("app", app)
        self.mqtt.publish_state("mic", "on")
        self.mqtt.publish_state("video", "on")
        self.mqtt.publish_state("hand", "down")

        # Start audio capture for notes
        if self.audio_capture:
            self.audio_capture.start(self._on_audio_chunk)
            log.info("Audio capture started for AI notes")

        # Send audio device info
        audio_dev = self._get_audio_device()
        self.mqtt.publish_state("audio_dev", audio_dev)

    def _end_meeting(self):
        """Handle meeting end — generate summary."""
        log.info("Meeting ended")
        self.meeting_active = False

        # Stop audio capture
        if self.audio_capture:
            self.audio_capture.stop()

        # Generate summary from notes
        if self.ai_notes.has_transcript():
            log.info("Generating meeting summary...")
            summary = self.ai_notes.summarize()
            self.mqtt.publish_notes("summary", summary)
            log.info(f"Summary: {summary[:100]}...")

        self.ai_notes.reset()

        self.mqtt.publish_state("active", "0")
        self.mqtt.publish_state("app", "none")
        self.current_app = "none"

    def _update_state(self):
        """Periodically publish current state."""
        audio_dev = self._get_audio_device()
        self.mqtt.publish_state("audio_dev", audio_dev)

    def _on_audio_chunk(self, audio_data, sample_rate):
        """Callback when audio chunk is ready for transcription."""
        if not self.ai_notes.api_key:
            return

        try:
            text = self.ai_notes.transcribe(audio_data, sample_rate)
            if text and text.strip():
                log.info(f"Transcribed: {text[:80]}")
                self.mqtt.publish_notes("live", text)

                if self.handoff_active:
                    self.handoff_notes.append(text)
        except Exception as e:
            log.error(f"Transcription error: {e}")

    def _on_watch_command(self, cmd: str, value: str):
        """Handle commands from the watch via MQTT."""
        log.info(f"Watch command: {cmd}={value}")

        if cmd == "mic" and value == "toggle":
            self.controller.toggle_mic(self.current_app)
            # Toggle local state tracking
            current = self.mqtt.last_states.get("mic", "on")
            new_state = "off" if current == "on" else "on"
            self.mqtt.publish_state("mic", new_state)

        elif cmd == "video" and value == "toggle":
            self.controller.toggle_video(self.current_app)
            current = self.mqtt.last_states.get("video", "on")
            new_state = "off" if current == "on" else "on"
            self.mqtt.publish_state("video", new_state)

        elif cmd == "hand" and value == "toggle":
            self.controller.toggle_hand(self.current_app)
            current = self.mqtt.last_states.get("hand", "down")
            new_state = "down" if current == "up" else "up"
            self.mqtt.publish_state("hand", new_state)

        elif cmd == "volume":
            self.controller.adjust_volume(value)

        elif cmd == "audio_dev" and value == "next":
            self.controller.next_audio_device()
            time.sleep(0.5)
            audio_dev = self._get_audio_device()
            self.mqtt.publish_state("audio_dev", audio_dev)

        elif cmd == "handoff":
            if value == "start":
                self.handoff_active = True
                self.handoff_notes = []
                log.info("Hand-off mode started — capturing discussion points")
            elif value == "stop":
                self.handoff_active = False
                if self.handoff_notes:
                    points = "\n".join(
                        f"- {n}" for n in self.handoff_notes[-10:]
                    )
                    # Summarize handoff notes
                    if self.ai_notes.api_key:
                        handoff_summary = self.ai_notes.summarize_handoff(
                            self.handoff_notes
                        )
                        self.mqtt.publish_notes("handoff", handoff_summary)
                    else:
                        self.mqtt.publish_notes("handoff", points)
                    log.info("Hand-off summary sent to watch")
                self.handoff_notes = []

        elif cmd == "end" and value == "1":
            if self.ai_notes.has_transcript():
                summary = self.ai_notes.summarize()
                self.mqtt.publish_notes("summary", summary)

    def _get_audio_device(self) -> str:
        """Get current default audio output device name."""
        try:
            import soundcard as sc

            default = sc.default_speaker()
            return default.name if default else "Unknown"
        except Exception:
            return "Unknown"


def main():
    parser = argparse.ArgumentParser(description="ESPro AI Companion App")
    parser.add_argument(
        "--config",
        default=str(Path(__file__).parent / "config.json"),
        help="Path to config.json",
    )
    parser.add_argument(
        "--settings",
        action="store_true",
        help="Open settings GUI before starting",
    )
    args = parser.parse_args()

    # Show settings GUI if requested or config doesn't exist
    if args.settings or not os.path.exists(args.config):
        reason = "Settings requested" if args.settings else "No config.json found"
        log.info(f"{reason} — opening settings GUI...")
        from settings_gui import open_settings

        open_settings(args.config)

        if not os.path.exists(args.config):
            log.error("No config saved. Exiting.")
            sys.exit(1)

    app = MeetingAssistant(args.config)

    def signal_handler(sig, frame):
        app.stop()
        sys.exit(0)

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    app.start()

    # Keep main thread alive
    try:
        while app.running:
            time.sleep(1)
    except KeyboardInterrupt:
        app.stop()


if __name__ == "__main__":
    main()
