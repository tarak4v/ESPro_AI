"""
ESPro AI Companion App — Windows Tray Application
==================================================
System tray app with a popup dashboard showing:
  - Mosquitto broker status (running / stopped, PID, port)
  - MQTT connection state
  - Active meeting info
  - Quick controls (start/stop, open settings)

Runs the meeting assistant in the background and provides a GUI overlay.
"""

import json
import os
import sys
import subprocess
import threading
import time
import tkinter as tk
from tkinter import ttk, messagebox
from pathlib import Path
import logging
import signal
import ctypes

# Local modules
from meeting_assistant import MeetingAssistant
from settings_gui import open_settings

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(name)s] %(levelname)s: %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("companion")

# ── Windows constants for tray icon ──
APP_NAME = "ESPro AI Companion"
CONFIG_NAME = "config.json"


def get_config_path():
    return str(Path(__file__).parent / CONFIG_NAME)


def find_mosquitto_status():
    """Check if Mosquitto broker is running. Returns dict with status info."""
    info = {
        "running": False,
        "pid": None,
        "port": None,
        "exe_path": None,
        "error": None,
    }
    try:
        # Use WMIC to find mosquitto process
        result = subprocess.run(
            ["wmic", "process", "where", "name='mosquitto.exe'", "get",
             "ProcessId,ExecutablePath,CommandLine", "/format:csv"],
            capture_output=True, text=True, timeout=5, creationflags=0x08000000
        )
        lines = [l.strip() for l in result.stdout.strip().split("\n") if l.strip() and "mosquitto" in l.lower()]
        if lines:
            # Parse CSV: Node,CommandLine,ExecutablePath,ProcessId
            for line in lines:
                parts = line.split(",")
                if len(parts) >= 4:
                    info["running"] = True
                    info["pid"] = parts[-1].strip()
                    info["exe_path"] = parts[-2].strip() if len(parts) > 2 else ""
                    cmd_line = ",".join(parts[1:-2])  # CommandLine may contain commas
                    # Try to extract port from command line
                    info["port"] = _extract_port(cmd_line)
                    break
        if info["running"] and not info["port"]:
            info["port"] = _check_mosquitto_port(info.get("pid"))
    except Exception as e:
        info["error"] = str(e)

    return info


def _extract_port(cmd_line):
    """Try to extract port from mosquitto command line args."""
    # Look for -p <port> or parse config file reference
    parts = cmd_line.split()
    for i, p in enumerate(parts):
        if p == "-p" and i + 1 < len(parts):
            try:
                return int(parts[i + 1])
            except ValueError:
                pass
        if p == "-c" and i + 1 < len(parts):
            # Try to read port from config file
            conf_path = parts[i + 1].strip('"').strip("'")
            return _read_port_from_conf(conf_path)
    return None


def _read_port_from_conf(conf_path):
    """Read listener port from mosquitto.conf."""
    try:
        with open(conf_path, "r") as f:
            for line in f:
                line = line.strip()
                if line.startswith("listener"):
                    parts = line.split()
                    if len(parts) >= 2:
                        return int(parts[1])
                elif line.startswith("port"):
                    parts = line.split()
                    if len(parts) >= 2:
                        return int(parts[1])
    except Exception:
        pass
    return None


def _check_mosquitto_port(pid):
    """Use netstat to find what port mosquitto is listening on."""
    if not pid:
        return 1883  # default
    try:
        result = subprocess.run(
            ["netstat", "-ano"],
            capture_output=True, text=True, timeout=5, creationflags=0x08000000
        )
        for line in result.stdout.split("\n"):
            if "LISTENING" in line and pid in line:
                parts = line.split()
                if len(parts) >= 2:
                    addr = parts[1]
                    if ":" in addr:
                        port_str = addr.rsplit(":", 1)[-1]
                        try:
                            return int(port_str)
                        except ValueError:
                            pass
    except Exception:
        pass
    return 1883  # default assumption


class CompanionDashboard:
    """Tkinter popup dashboard for the companion app."""

    def __init__(self):
        self.assistant = None
        self.assistant_thread = None
        self.config_path = get_config_path()
        self._running = True

        # Build root window
        self.root = tk.Tk()
        self.root.title(APP_NAME)
        self.root.geometry("520x480")
        self.root.resizable(False, False)
        self.root.configure(bg="#1a1a2e")
        self.root.protocol("WM_DELETE_WINDOW", self._minimize_to_tray)

        # Keep reference for tray
        self._withdrawn = False

        # Style
        self.style = ttk.Style()
        self.style.theme_use("clam")
        self._apply_theme()

        # Build UI
        self._build_ui()

        # Start status polling
        self._poll_status()

        # Auto-start assistant if config exists
        if os.path.exists(self.config_path):
            self.root.after(500, self._start_assistant)

    def _apply_theme(self):
        bg = "#1a1a2e"
        card = "#262640"
        fg = "#e0e0e0"
        accent = "#4CAF50"
        entry_bg = "#1e1e38"

        self.style.configure(".", background=bg, foreground=fg)
        self.style.configure("TFrame", background=bg)
        self.style.configure("Card.TFrame", background=card)
        self.style.configure("TLabel", background=bg, foreground=fg, font=("Segoe UI", 9))
        self.style.configure("Card.TLabel", background=card, foreground=fg, font=("Segoe UI", 9))
        self.style.configure("Header.TLabel", background=bg, foreground=accent, font=("Segoe UI", 12, "bold"))
        self.style.configure("CardHeader.TLabel", background=card, foreground="#aaa", font=("Segoe UI", 10, "bold"))
        self.style.configure("Value.TLabel", background=card, foreground="#fff", font=("Segoe UI", 10))
        self.style.configure("StatusOK.TLabel", background=card, foreground="#4CAF50", font=("Segoe UI", 10, "bold"))
        self.style.configure("StatusErr.TLabel", background=card, foreground="#e53935", font=("Segoe UI", 10, "bold"))
        self.style.configure("StatusWarn.TLabel", background=card, foreground="#FF9800", font=("Segoe UI", 10, "bold"))
        self.style.configure("TButton", background=accent, foreground="#fff", font=("Segoe UI", 9, "bold"))
        self.style.map("TButton", background=[("active", "#388E3C")])
        self.style.configure("Stop.TButton", background="#e53935", foreground="#fff")
        self.style.map("Stop.TButton", background=[("active", "#c62828")])
        self.style.configure("Settings.TButton", background="#2196F3", foreground="#fff")
        self.style.map("Settings.TButton", background=[("active", "#1976D2")])

    def _build_ui(self):
        # ── Title bar ──
        title_frame = ttk.Frame(self.root)
        title_frame.pack(fill="x", padx=12, pady=(10, 4))
        ttk.Label(title_frame, text="\u26A1 ESPro AI Companion", style="Header.TLabel").pack(side="left")

        # ── Mosquitto Status Card ──
        self._build_mosquitto_card()

        # ── MQTT Connection Card ──
        self._build_mqtt_card()

        # ── Meeting Status Card ──
        self._build_meeting_card()

        # ── Control Buttons ──
        btn_frame = ttk.Frame(self.root)
        btn_frame.pack(fill="x", padx=12, pady=8)

        self.btn_start = ttk.Button(btn_frame, text="\u25B6  Start", command=self._start_assistant)
        self.btn_start.pack(side="left", padx=4)

        self.btn_stop = ttk.Button(btn_frame, text="\u25A0  Stop", style="Stop.TButton", command=self._stop_assistant)
        self.btn_stop.pack(side="left", padx=4)

        ttk.Button(btn_frame, text="\u2699  Settings", style="Settings.TButton", command=self._open_settings).pack(side="left", padx=4)
        ttk.Button(btn_frame, text="\u21BB  Refresh", command=self._poll_status).pack(side="right", padx=4)

        # ── Status bar ──
        self.lbl_status = ttk.Label(self.root, text="Ready", font=("Segoe UI", 8), foreground="#666")
        self.lbl_status.pack(side="bottom", fill="x", padx=12, pady=(0, 6))

    def _build_mosquitto_card(self):
        card = ttk.Frame(self.root, style="Card.TFrame", padding=10)
        card.pack(fill="x", padx=12, pady=4)

        ttk.Label(card, text="Mosquitto Broker", style="CardHeader.TLabel").grid(row=0, column=0, columnspan=4, sticky="w", pady=(0, 6))

        ttk.Label(card, text="Status:", style="Card.TLabel").grid(row=1, column=0, sticky="w")
        self.lbl_mosq_status = ttk.Label(card, text="Checking...", style="Value.TLabel")
        self.lbl_mosq_status.grid(row=1, column=1, sticky="w", padx=(4, 16))

        ttk.Label(card, text="PID:", style="Card.TLabel").grid(row=1, column=2, sticky="w")
        self.lbl_mosq_pid = ttk.Label(card, text="—", style="Value.TLabel")
        self.lbl_mosq_pid.grid(row=1, column=3, sticky="w", padx=4)

        ttk.Label(card, text="Port:", style="Card.TLabel").grid(row=2, column=0, sticky="w")
        self.lbl_mosq_port = ttk.Label(card, text="—", style="Value.TLabel")
        self.lbl_mosq_port.grid(row=2, column=1, sticky="w", padx=4)

        ttk.Label(card, text="Path:", style="Card.TLabel").grid(row=2, column=2, sticky="w")
        self.lbl_mosq_path = ttk.Label(card, text="—", style="Value.TLabel", wraplength=200)
        self.lbl_mosq_path.grid(row=2, column=3, sticky="w", padx=4)

        for c in range(4):
            card.columnconfigure(c, weight=1)

    def _build_mqtt_card(self):
        card = ttk.Frame(self.root, style="Card.TFrame", padding=10)
        card.pack(fill="x", padx=12, pady=4)

        ttk.Label(card, text="MQTT Connection", style="CardHeader.TLabel").grid(row=0, column=0, columnspan=4, sticky="w", pady=(0, 6))

        ttk.Label(card, text="Broker:", style="Card.TLabel").grid(row=1, column=0, sticky="w")
        self.lbl_mqtt_broker = ttk.Label(card, text="—", style="Value.TLabel")
        self.lbl_mqtt_broker.grid(row=1, column=1, sticky="w", padx=(4, 16))

        ttk.Label(card, text="Connected:", style="Card.TLabel").grid(row=1, column=2, sticky="w")
        self.lbl_mqtt_connected = ttk.Label(card, text="—", style="Value.TLabel")
        self.lbl_mqtt_connected.grid(row=1, column=3, sticky="w", padx=4)

        ttk.Label(card, text="Device:", style="Card.TLabel").grid(row=2, column=0, sticky="w")
        self.lbl_mqtt_device = ttk.Label(card, text="—", style="Value.TLabel")
        self.lbl_mqtt_device.grid(row=2, column=1, columnspan=3, sticky="w", padx=4)

        for c in range(4):
            card.columnconfigure(c, weight=1)

    def _build_meeting_card(self):
        card = ttk.Frame(self.root, style="Card.TFrame", padding=10)
        card.pack(fill="x", padx=12, pady=4)

        ttk.Label(card, text="Meeting Status", style="CardHeader.TLabel").grid(row=0, column=0, columnspan=4, sticky="w", pady=(0, 6))

        ttk.Label(card, text="Active:", style="Card.TLabel").grid(row=1, column=0, sticky="w")
        self.lbl_meeting_active = ttk.Label(card, text="No", style="Value.TLabel")
        self.lbl_meeting_active.grid(row=1, column=1, sticky="w", padx=(4, 16))

        ttk.Label(card, text="App:", style="Card.TLabel").grid(row=1, column=2, sticky="w")
        self.lbl_meeting_app = ttk.Label(card, text="—", style="Value.TLabel")
        self.lbl_meeting_app.grid(row=1, column=3, sticky="w", padx=4)

        ttk.Label(card, text="Mic:", style="Card.TLabel").grid(row=2, column=0, sticky="w")
        self.lbl_meeting_mic = ttk.Label(card, text="—", style="Value.TLabel")
        self.lbl_meeting_mic.grid(row=2, column=1, sticky="w", padx=(4, 16))

        ttk.Label(card, text="Video:", style="Card.TLabel").grid(row=2, column=2, sticky="w")
        self.lbl_meeting_video = ttk.Label(card, text="—", style="Value.TLabel")
        self.lbl_meeting_video.grid(row=2, column=3, sticky="w", padx=4)

        ttk.Label(card, text="Audio Device:", style="Card.TLabel").grid(row=3, column=0, sticky="w")
        self.lbl_meeting_audio = ttk.Label(card, text="—", style="Value.TLabel", wraplength=350)
        self.lbl_meeting_audio.grid(row=3, column=1, columnspan=3, sticky="w", padx=4)

        for c in range(4):
            card.columnconfigure(c, weight=1)

    # ── Status polling ──────────────────────────────────────────
    def _poll_status(self):
        """Update all status fields."""
        threading.Thread(target=self._update_mosquitto_status, daemon=True).start()
        self._update_mqtt_status()
        self._update_meeting_status()

        # Re-poll every 5 seconds while window is visible
        if self._running:
            self.root.after(5000, self._poll_status)

    def _update_mosquitto_status(self):
        info = find_mosquitto_status()
        self.root.after(0, lambda: self._apply_mosquitto_info(info))

    def _apply_mosquitto_info(self, info):
        if info["running"]:
            self.lbl_mosq_status.configure(text="\u2705 Running", style="StatusOK.TLabel")
            self.lbl_mosq_pid.configure(text=str(info["pid"]) if info["pid"] else "—")
            port = info["port"] if info["port"] else 1883
            self.lbl_mosq_port.configure(text=str(port))
            path = info.get("exe_path", "")
            self.lbl_mosq_path.configure(text=path if path else "—")
        else:
            self.lbl_mosq_status.configure(text="\u274C Stopped", style="StatusErr.TLabel")
            self.lbl_mosq_pid.configure(text="—")
            self.lbl_mosq_port.configure(text="—")
            self.lbl_mosq_path.configure(text="—")
            if info.get("error"):
                self.lbl_status.configure(text=f"Mosquitto check error: {info['error']}", foreground="#e53935")

    def _update_mqtt_status(self):
        if self.assistant and self.assistant.mqtt:
            mqtt = self.assistant.mqtt
            self.lbl_mqtt_broker.configure(text=f"{mqtt.broker}:{mqtt.port}")
            if mqtt.connected:
                self.lbl_mqtt_connected.configure(text="\u2705 Yes", style="StatusOK.TLabel")
            else:
                self.lbl_mqtt_connected.configure(text="\u274C No", style="StatusErr.TLabel")
            self.lbl_mqtt_device.configure(text=mqtt.topic_base)
        else:
            self.lbl_mqtt_broker.configure(text="—")
            self.lbl_mqtt_connected.configure(text="—", style="Value.TLabel")
            self.lbl_mqtt_device.configure(text="—")

    def _update_meeting_status(self):
        if self.assistant:
            if self.assistant.meeting_active:
                self.lbl_meeting_active.configure(text="\u2705 Yes", style="StatusOK.TLabel")
                app_name = self.assistant.current_app.title()
                self.lbl_meeting_app.configure(text=app_name)
            else:
                self.lbl_meeting_active.configure(text="No", style="Value.TLabel")
                self.lbl_meeting_app.configure(text="—")

            states = self.assistant.mqtt.last_states if self.assistant.mqtt else {}
            mic = states.get("mic", "—")
            video = states.get("video", "—")
            audio_dev = states.get("audio_dev", "—")

            mic_icon = "\U0001F50A" if mic == "on" else "\U0001F507" if mic == "off" else ""
            vid_icon = "\U0001F4F9" if video == "on" else "\u26AB" if video == "off" else ""

            self.lbl_meeting_mic.configure(text=f"{mic_icon} {mic}")
            self.lbl_meeting_video.configure(text=f"{vid_icon} {video}")
            self.lbl_meeting_audio.configure(text=audio_dev)
        else:
            for lbl in [self.lbl_meeting_active, self.lbl_meeting_app,
                        self.lbl_meeting_mic, self.lbl_meeting_video, self.lbl_meeting_audio]:
                lbl.configure(text="—", style="Value.TLabel")

    # ── Controls ────────────────────────────────────────────────
    def _start_assistant(self):
        if self.assistant and self.assistant.running:
            self.lbl_status.configure(text="Already running", foreground="#FF9800")
            return

        if not os.path.exists(self.config_path):
            messagebox.showwarning("Config Missing", "No config.json found. Opening settings...")
            self._open_settings()
            if not os.path.exists(self.config_path):
                return

        try:
            self.assistant = MeetingAssistant(self.config_path)
            self.assistant_thread = threading.Thread(target=self._run_assistant, daemon=True)
            self.assistant_thread.start()
            self.lbl_status.configure(text="Assistant started", foreground="#4CAF50")
            self.btn_start.state(["disabled"])
            self.btn_stop.state(["!disabled"])
        except Exception as e:
            self.lbl_status.configure(text=f"Start failed: {e}", foreground="#e53935")
            log.error(f"Failed to start assistant: {e}")

    def _run_assistant(self):
        try:
            self.assistant.start()
        except Exception as e:
            log.error(f"Assistant error: {e}")
            self.root.after(0, lambda: self.lbl_status.configure(
                text=f"Assistant error: {e}", foreground="#e53935"))

    def _stop_assistant(self):
        if self.assistant and self.assistant.running:
            threading.Thread(target=self._do_stop, daemon=True).start()
        else:
            self.lbl_status.configure(text="Not running", foreground="#888")

    def _do_stop(self):
        try:
            self.assistant.stop()
        except Exception as e:
            log.error(f"Stop error: {e}")
        self.assistant = None
        self.root.after(0, lambda: self._on_stopped())

    def _on_stopped(self):
        self.lbl_status.configure(text="Assistant stopped", foreground="#FF9800")
        self.btn_start.state(["!disabled"])
        self.btn_stop.state(["disabled"])

    def _open_settings(self):
        # Open settings in a new toplevel to avoid blocking
        def _do():
            open_settings(self.config_path, on_save_callback=self._on_settings_saved)
        threading.Thread(target=_do, daemon=True).start()

    def _on_settings_saved(self, cfg):
        self.root.after(0, lambda: self.lbl_status.configure(
            text="Settings saved. Restart assistant to apply.", foreground="#4CAF50"))

    def _minimize_to_tray(self):
        """Minimize to background instead of closing."""
        self.root.withdraw()
        self._withdrawn = True

    def show(self):
        """Show the popup window."""
        self.root.deiconify()
        self.root.lift()
        self.root.focus_force()
        self._withdrawn = False
        self._poll_status()

    def quit(self):
        """Fully exit the app."""
        self._running = False
        if self.assistant and self.assistant.running:
            self.assistant.stop()
        self.root.quit()
        self.root.destroy()

    def run(self):
        """Run the main loop with system tray support."""
        try:
            import pystray
            from PIL import Image
            has_tray = True
        except ImportError:
            has_tray = False
            log.warning("pystray/Pillow not installed — running without tray icon")

        if has_tray:
            self._start_tray_icon()
        self.root.mainloop()

    def _start_tray_icon(self):
        """Create system tray icon in background thread."""
        import pystray
        from PIL import Image, ImageDraw

        # Create a simple icon (green circle with white E)
        img = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
        draw = ImageDraw.Draw(img)
        draw.ellipse([4, 4, 60, 60], fill="#4CAF50")
        draw.text((20, 15), "E", fill="white")

        def on_show(icon, item):
            self.root.after(0, self.show)

        def on_quit(icon, item):
            icon.stop()
            self.root.after(0, self.quit)

        menu = pystray.Menu(
            pystray.MenuItem("Show Dashboard", on_show, default=True),
            pystray.Menu.SEPARATOR,
            pystray.MenuItem("Quit", on_quit),
        )

        self.tray_icon = pystray.Icon(APP_NAME, img, APP_NAME, menu)
        threading.Thread(target=self.tray_icon.run, daemon=True).start()


def main():
    dashboard = CompanionDashboard()
    dashboard.run()


if __name__ == "__main__":
    main()
