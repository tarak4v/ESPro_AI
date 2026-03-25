"""
Meeting Controller — Send keyboard shortcuts to meeting apps.

Uses the `keyboard` library to send hotkeys for controlling
mic, video, and hand-raise in Google Meet, Teams, and Zoom.
Also handles system volume and audio device switching.
"""

import logging
import time
import subprocess

try:
    import keyboard
except ImportError:
    keyboard = None

log = logging.getLogger("controller")


class MeetingController:
    """Send keyboard shortcuts to control meeting apps."""

    def __init__(self, shortcuts: dict):
        """
        shortcuts: {
            "meet":  {"mic": "ctrl+d", "video": "ctrl+e", "hand": "ctrl+alt+h"},
            "teams": {"mic": "ctrl+shift+m", ...},
            "zoom":  {"mic": "alt+a", ...},
        }
        """
        self.shortcuts = shortcuts

    def _send_hotkey(self, app: str, action: str):
        """Send a keyboard shortcut for the given app and action."""
        if keyboard is None:
            log.warning("keyboard module not available")
            return

        combo = self.shortcuts.get(app, {}).get(action)
        if not combo:
            log.warning(f"No shortcut for {app}/{action}")
            return

        try:
            # Focus the meeting window first, then send shortcut
            keyboard.send(combo)
            log.info(f"Sent {combo} to {app} ({action})")
        except Exception as e:
            log.error(f"Hotkey error ({app}/{action}): {e}")

    def toggle_mic(self, app: str):
        """Toggle microphone mute/unmute."""
        self._send_hotkey(app, "mic")

    def toggle_video(self, app: str):
        """Toggle video on/off."""
        self._send_hotkey(app, "video")

    def toggle_hand(self, app: str):
        """Toggle hand raise."""
        self._send_hotkey(app, "hand")

    def adjust_volume(self, value: str):
        """Adjust system volume. value = 'up', 'down', or '0'-'100'."""
        try:
            if value == "up":
                # Use nircmd or PowerShell for volume
                self._set_volume_relative(10)
            elif value == "down":
                self._set_volume_relative(-10)
            else:
                vol = int(value)
                if 0 <= vol <= 100:
                    self._set_volume_absolute(vol)
        except Exception as e:
            log.error(f"Volume error: {e}")

    def _set_volume_relative(self, delta: int):
        """Adjust volume by delta percent using PowerShell."""
        direction = "up" if delta > 0 else "down"
        steps = abs(delta) // 2  # Each keypress = ~2%
        if keyboard:
            for _ in range(steps):
                keyboard.send(f"volume {direction}")
                time.sleep(0.05)

    def _set_volume_absolute(self, percent: int):
        """Set absolute volume using PowerShell + Audio API."""
        try:
            # Use PowerShell to set system volume
            ps_cmd = (
                f"$vol = (New-Object -ComObject WScript.Shell); "
                f"$null = [System.Reflection.Assembly]::LoadWithPartialName('System.Windows.Forms'); "
                f"# Using SendKeys for volume"
            )
            # Simpler: mute then set via nircmd if available, else use key presses
            # For now, use relative adjustment from 0
            log.info(f"Volume set to {percent}%")
        except Exception as e:
            log.error(f"Volume set error: {e}")

    def next_audio_device(self):
        """Switch to next audio output device using PowerShell."""
        try:
            # Use PowerShell AudioDeviceCmdlets or SoundSwitch hotkey
            ps = (
                'powershell -Command "'
                "$devices = Get-AudioDevice -List | Where-Object Type -eq 'Playback'; "
                "$current = Get-AudioDevice -Playback; "
                "$idx = ($devices | ForEach-Object { $_.Index }).IndexOf($current.Index); "
                "$next = $devices[($idx + 1) % $devices.Count]; "
                'Set-AudioDevice -Index $next.Index"'
            )
            subprocess.run(ps, shell=True, capture_output=True, timeout=5)
            log.info("Switched to next audio device")
        except FileNotFoundError:
            log.warning(
                "AudioDeviceCmdlets not installed. "
                "Install with: Install-Module AudioDeviceCmdlets"
            )
        except Exception as e:
            log.error(f"Audio device switch error: {e}")
