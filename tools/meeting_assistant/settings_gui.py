"""
Settings GUI — Configuration screen for the ESPro Meeting Assistant.

Uses tkinter (built into Python, no extra dependencies).
Provides fields for:
  - MQTT Broker connection (IP/hostname, port, credentials)
  - Device name / discovery
  - Groq API key
  - Audio capture settings
  - Meeting detection tuning
  - Keyboard shortcut customization

Saves to config.json on disk. Can test MQTT connection from the UI.
"""

import json
import os
import sys
import threading
import tkinter as tk
from tkinter import ttk, messagebox, filedialog
from pathlib import Path

try:
    import paho.mqtt.client as mqtt
except ImportError:
    mqtt = None


DEFAULT_CONFIG = {
    "mqtt": {
        "broker": "localhost",
        "port": 1883,
        "username": "",
        "password": "",
        "device_name": "ESPro_AI_Watch",
    },
    "groq_api_key": "",
    "audio": {
        "enabled": True,
        "sample_rate": 16000,
        "chunk_duration_s": 30,
        "language": "en",
    },
    "meeting_detection": {
        "poll_interval_s": 2,
        "window_patterns": {
            "meet": ["Meet -", "Google Meet"],
            "teams": ["Microsoft Teams", "| Microsoft Teams"],
            "zoom": ["Zoom Meeting", "Zoom Webinar"],
        },
    },
    "keyboard_shortcuts": {
        "meet": {"mic": "ctrl+d", "video": "ctrl+e", "hand": "ctrl+alt+h"},
        "teams": {
            "mic": "ctrl+shift+m",
            "video": "ctrl+shift+o",
            "hand": "ctrl+shift+k",
        },
        "zoom": {"mic": "alt+a", "video": "alt+v", "hand": "alt+y"},
    },
}


class SettingsGUI:
    """Tkinter settings window for the Meeting Assistant."""

    def __init__(self, config_path: str, on_save_callback=None):
        self.config_path = config_path
        self.on_save_callback = on_save_callback
        self.config = self._load_config()

        self.root = tk.Tk()
        self.root.title("ESPro AI Companion App — Settings")
        self.root.geometry("680x620")
        self.root.resizable(True, True)
        self.root.configure(bg="#1a1a2e")

        # Style
        self.style = ttk.Style()
        self.style.theme_use("clam")
        self._apply_dark_theme()

        self._build_ui()
        self._populate_fields()

    def _apply_dark_theme(self):
        bg = "#1a1a2e"
        card = "#262640"
        fg = "#e0e0e0"
        accent = "#4CAF50"
        entry_bg = "#1e1e38"
        border = "#444"

        self.style.configure(".", background=bg, foreground=fg, fieldbackground=entry_bg)
        self.style.configure("TFrame", background=bg)
        self.style.configure("Card.TFrame", background=card)
        self.style.configure("TLabel", background=bg, foreground=fg, font=("Segoe UI", 9))
        self.style.configure("Card.TLabel", background=card, foreground=fg, font=("Segoe UI", 9))
        self.style.configure("Header.TLabel", background=bg, foreground=accent, font=("Segoe UI", 11, "bold"))
        self.style.configure("CardHeader.TLabel", background=card, foreground="#aaa", font=("Segoe UI", 10, "bold"))
        self.style.configure("Status.TLabel", background=bg, foreground="#888", font=("Segoe UI", 8))
        self.style.configure("TEntry", fieldbackground=entry_bg, foreground=fg, insertcolor=fg)
        self.style.configure("TCheckbutton", background=card, foreground=fg)
        self.style.configure("TButton", background=accent, foreground="#fff", font=("Segoe UI", 9, "bold"))
        self.style.map("TButton", background=[("active", "#388E3C")])
        self.style.configure("Test.TButton", background="#2196F3")
        self.style.map("Test.TButton", background=[("active", "#1976D2")])
        self.style.configure("Danger.TButton", background="#e53935")
        self.style.map("Danger.TButton", background=[("active", "#c62828")])
        self.style.configure("TNotebook", background=bg)
        self.style.configure("TNotebook.Tab", background=card, foreground=fg, padding=[12, 4])
        self.style.map("TNotebook.Tab", background=[("selected", accent)], foreground=[("selected", "#fff")])

    def _load_config(self) -> dict:
        if os.path.exists(self.config_path):
            try:
                with open(self.config_path, "r") as f:
                    cfg = json.load(f)
                # Merge with defaults for any missing keys
                return self._deep_merge(DEFAULT_CONFIG, cfg)
            except Exception:
                pass
        return dict(DEFAULT_CONFIG)

    @staticmethod
    def _deep_merge(base: dict, override: dict) -> dict:
        result = dict(base)
        for k, v in override.items():
            if k in result and isinstance(result[k], dict) and isinstance(v, dict):
                result[k] = SettingsGUI._deep_merge(result[k], v)
            else:
                result[k] = v
        return result

    def _build_ui(self):
        # Title
        title = ttk.Label(self.root, text="\u2699  ESPro AI Companion App", style="Header.TLabel")
        title.pack(pady=(10, 5))

        # Notebook tabs
        self.notebook = ttk.Notebook(self.root)
        self.notebook.pack(fill="both", expand=True, padx=10, pady=5)

        self._build_connection_tab()
        self._build_audio_tab()
        self._build_shortcuts_tab()
        self._build_detection_tab()

        # Bottom buttons
        btn_frame = ttk.Frame(self.root)
        btn_frame.pack(fill="x", padx=10, pady=8)

        ttk.Button(btn_frame, text="Save & Apply", command=self._save).pack(side="left", padx=4)
        ttk.Button(btn_frame, text="Test Connection", style="Test.TButton", command=self._test_connection).pack(side="left", padx=4)
        ttk.Button(btn_frame, text="Reset Defaults", style="Danger.TButton", command=self._reset_defaults).pack(side="right", padx=4)

        self.lbl_status = ttk.Label(self.root, text="", style="Status.TLabel")
        self.lbl_status.pack(pady=(0, 6))

    # ── Connection Tab ──────────────────────────────────────────
    def _build_connection_tab(self):
        frame = ttk.Frame(self.notebook)
        self.notebook.add(frame, text="  Connection  ")

        # MQTT Card
        card = ttk.Frame(frame, style="Card.TFrame", padding=12)
        card.pack(fill="x", padx=10, pady=8)

        ttk.Label(card, text="MQTT Broker", style="CardHeader.TLabel").grid(row=0, column=0, columnspan=4, sticky="w", pady=(0, 8))

        ttk.Label(card, text="Broker IP / Hostname:", style="Card.TLabel").grid(row=1, column=0, sticky="w")
        self.ent_broker = ttk.Entry(card, width=30)
        self.ent_broker.grid(row=1, column=1, padx=(4, 12), pady=3, sticky="ew")

        ttk.Label(card, text="Port:", style="Card.TLabel").grid(row=1, column=2, sticky="w")
        self.ent_port = ttk.Entry(card, width=8)
        self.ent_port.grid(row=1, column=3, padx=4, pady=3)

        ttk.Label(card, text="Username (optional):", style="Card.TLabel").grid(row=2, column=0, sticky="w")
        self.ent_mqtt_user = ttk.Entry(card, width=30)
        self.ent_mqtt_user.grid(row=2, column=1, padx=(4, 12), pady=3, sticky="ew")

        ttk.Label(card, text="Password:", style="Card.TLabel").grid(row=2, column=2, sticky="w")
        self.ent_mqtt_pass = ttk.Entry(card, width=20, show="*")
        self.ent_mqtt_pass.grid(row=2, column=3, padx=4, pady=3)

        card.columnconfigure(1, weight=1)

        # Device Card
        card2 = ttk.Frame(frame, style="Card.TFrame", padding=12)
        card2.pack(fill="x", padx=10, pady=4)

        ttk.Label(card2, text="Device", style="CardHeader.TLabel").grid(row=0, column=0, columnspan=3, sticky="w", pady=(0, 8))

        ttk.Label(card2, text="Watch Device Name:", style="Card.TLabel").grid(row=1, column=0, sticky="w")
        self.ent_device_name = ttk.Entry(card2, width=30)
        self.ent_device_name.grid(row=1, column=1, padx=4, pady=3, sticky="ew")

        ttk.Label(card2, text="(Must match the device name in watch config)", style="Status.TLabel").grid(row=2, column=0, columnspan=3, sticky="w", pady=(0, 4))

        card2.columnconfigure(1, weight=1)

        # AI Card
        card3 = ttk.Frame(frame, style="Card.TFrame", padding=12)
        card3.pack(fill="x", padx=10, pady=4)

        ttk.Label(card3, text="AI Provider (Groq)", style="CardHeader.TLabel").grid(row=0, column=0, columnspan=2, sticky="w", pady=(0, 8))

        ttk.Label(card3, text="Groq API Key:", style="Card.TLabel").grid(row=1, column=0, sticky="w")
        self.ent_groq_key = ttk.Entry(card3, width=50, show="*")
        self.ent_groq_key.grid(row=1, column=1, padx=4, pady=3, sticky="ew")

        self.var_show_key = tk.BooleanVar(value=False)
        ttk.Checkbutton(card3, text="Show key", variable=self.var_show_key,
                        command=self._toggle_key_visibility, style="TCheckbutton").grid(row=2, column=1, sticky="w")

        card3.columnconfigure(1, weight=1)

        # Connection info
        info_frame = ttk.Frame(frame, padding=8)
        info_frame.pack(fill="x", padx=10)
        ttk.Label(info_frame, text="MQTT Topic Pattern:  espro/<device_name>/meeting/...", style="Status.TLabel").pack(anchor="w")
        ttk.Label(info_frame, text="Watch publishes commands to .../cmd/*  |  Desktop publishes state to .../state/*", style="Status.TLabel").pack(anchor="w")

    # ── Audio Tab ────────────────────────────────────────────────
    def _build_audio_tab(self):
        frame = ttk.Frame(self.notebook)
        self.notebook.add(frame, text="  Audio  ")

        card = ttk.Frame(frame, style="Card.TFrame", padding=12)
        card.pack(fill="x", padx=10, pady=8)

        ttk.Label(card, text="Audio Capture Settings", style="CardHeader.TLabel").grid(row=0, column=0, columnspan=2, sticky="w", pady=(0, 8))

        self.var_audio_enabled = tk.BooleanVar()
        ttk.Checkbutton(card, text="Enable system audio capture for AI note-taking",
                        variable=self.var_audio_enabled, style="TCheckbutton").grid(row=1, column=0, columnspan=2, sticky="w", pady=3)

        ttk.Label(card, text="Sample Rate (Hz):", style="Card.TLabel").grid(row=2, column=0, sticky="w")
        self.ent_sample_rate = ttk.Entry(card, width=10)
        self.ent_sample_rate.grid(row=2, column=1, padx=4, pady=3, sticky="w")

        ttk.Label(card, text="Chunk Duration (s):", style="Card.TLabel").grid(row=3, column=0, sticky="w")
        self.ent_chunk_dur = ttk.Entry(card, width=10)
        self.ent_chunk_dur.grid(row=3, column=1, padx=4, pady=3, sticky="w")

        ttk.Label(card, text="Language:", style="Card.TLabel").grid(row=4, column=0, sticky="w")
        self.ent_language = ttk.Entry(card, width=10)
        self.ent_language.grid(row=4, column=1, padx=4, pady=3, sticky="w")

        card.columnconfigure(1, weight=1)

        # Audio info
        info = ttk.Frame(frame, padding=8)
        info.pack(fill="x", padx=10)
        ttk.Label(info, text="Audio is captured from system loopback (what you hear).", style="Status.TLabel").pack(anchor="w")
        ttk.Label(info, text="Captured audio is sent to Groq Whisper STT for live transcription.", style="Status.TLabel").pack(anchor="w")
        ttk.Label(info, text="Requires 'soundcard' library and a Groq API key.", style="Status.TLabel").pack(anchor="w")

    # ── Shortcuts Tab ────────────────────────────────────────────
    def _build_shortcuts_tab(self):
        frame = ttk.Frame(self.notebook)
        self.notebook.add(frame, text="  Shortcuts  ")

        self.shortcut_entries = {}

        apps = [
            ("Google Meet", "meet"),
            ("Microsoft Teams", "teams"),
            ("Zoom", "zoom"),
        ]
        actions = ["mic", "video", "hand"]

        for i, (display_name, app_key) in enumerate(apps):
            card = ttk.Frame(frame, style="Card.TFrame", padding=10)
            card.pack(fill="x", padx=10, pady=4)

            ttk.Label(card, text=display_name, style="CardHeader.TLabel").grid(row=0, column=0, columnspan=6, sticky="w", pady=(0, 6))

            self.shortcut_entries[app_key] = {}
            for j, action in enumerate(actions):
                col = j * 2
                label_text = {"mic": "Mute/Unmute:", "video": "Video On/Off:", "hand": "Raise Hand:"}[action]
                ttk.Label(card, text=label_text, style="Card.TLabel").grid(row=1, column=col, sticky="w", padx=(0, 2))
                ent = ttk.Entry(card, width=16)
                ent.grid(row=1, column=col + 1, padx=(0, 12), pady=3)
                self.shortcut_entries[app_key][action] = ent

            for c in range(6):
                card.columnconfigure(c, weight=1 if c % 2 == 1 else 0)

        info = ttk.Frame(frame, padding=8)
        info.pack(fill="x", padx=10)
        ttk.Label(info, text="Format: ctrl+d, ctrl+shift+m, alt+a, etc.", style="Status.TLabel").pack(anchor="w")
        ttk.Label(info, text="These shortcuts are sent to the meeting app when you toggle from the watch.", style="Status.TLabel").pack(anchor="w")

    # ── Detection Tab ────────────────────────────────────────────
    def _build_detection_tab(self):
        frame = ttk.Frame(self.notebook)
        self.notebook.add(frame, text="  Detection  ")

        card = ttk.Frame(frame, style="Card.TFrame", padding=12)
        card.pack(fill="x", padx=10, pady=8)

        ttk.Label(card, text="Meeting Detection", style="CardHeader.TLabel").grid(row=0, column=0, columnspan=2, sticky="w", pady=(0, 8))

        ttk.Label(card, text="Poll Interval (s):", style="Card.TLabel").grid(row=1, column=0, sticky="w")
        self.ent_poll_interval = ttk.Entry(card, width=8)
        self.ent_poll_interval.grid(row=1, column=1, padx=4, pady=3, sticky="w")

        card.columnconfigure(1, weight=1)

        # Window patterns
        card2 = ttk.Frame(frame, style="Card.TFrame", padding=12)
        card2.pack(fill="x", padx=10, pady=4)

        ttk.Label(card2, text="Window Title Patterns (comma-separated)", style="CardHeader.TLabel").grid(row=0, column=0, columnspan=2, sticky="w", pady=(0, 8))

        self.pattern_entries = {}
        apps = [("Google Meet:", "meet"), ("Microsoft Teams:", "teams"), ("Zoom:", "zoom")]
        for i, (label, key) in enumerate(apps):
            ttk.Label(card2, text=label, style="Card.TLabel").grid(row=i + 1, column=0, sticky="w")
            ent = ttk.Entry(card2, width=50)
            ent.grid(row=i + 1, column=1, padx=4, pady=3, sticky="ew")
            self.pattern_entries[key] = ent

        card2.columnconfigure(1, weight=1)

        info = ttk.Frame(frame, padding=8)
        info.pack(fill="x", padx=10)
        ttk.Label(info, text="The app scans all open window titles looking for these patterns.", style="Status.TLabel").pack(anchor="w")
        ttk.Label(info, text="Add custom patterns if your meeting window has a different title.", style="Status.TLabel").pack(anchor="w")

    # ── Populate fields from config ─────────────────────────────
    def _populate_fields(self):
        cfg = self.config
        m = cfg.get("mqtt", {})

        self.ent_broker.insert(0, m.get("broker", "localhost"))
        self.ent_port.insert(0, str(m.get("port", 1883)))
        self.ent_mqtt_user.insert(0, m.get("username", ""))
        self.ent_mqtt_pass.insert(0, m.get("password", ""))
        self.ent_device_name.insert(0, m.get("device_name", "ESPro_AI_Watch"))

        self.ent_groq_key.insert(0, cfg.get("groq_api_key", ""))

        audio = cfg.get("audio", {})
        self.var_audio_enabled.set(audio.get("enabled", True))
        self.ent_sample_rate.insert(0, str(audio.get("sample_rate", 16000)))
        self.ent_chunk_dur.insert(0, str(audio.get("chunk_duration_s", 30)))
        self.ent_language.insert(0, audio.get("language", "en"))

        det = cfg.get("meeting_detection", {})
        self.ent_poll_interval.insert(0, str(det.get("poll_interval_s", 2)))

        patterns = det.get("window_patterns", {})
        for key, ent in self.pattern_entries.items():
            vals = patterns.get(key, [])
            ent.insert(0, ", ".join(vals))

        shortcuts = cfg.get("keyboard_shortcuts", {})
        for app_key, actions in self.shortcut_entries.items():
            app_sc = shortcuts.get(app_key, {})
            for action, ent in actions.items():
                ent.insert(0, app_sc.get(action, ""))

    # ── Collect fields into config dict ─────────────────────────
    def _collect_config(self) -> dict:
        cfg = {}

        cfg["mqtt"] = {
            "broker": self.ent_broker.get().strip(),
            "port": int(self.ent_port.get().strip() or "1883"),
            "username": self.ent_mqtt_user.get().strip(),
            "password": self.ent_mqtt_pass.get().strip(),
            "device_name": self.ent_device_name.get().strip() or "ESPro_AI_Watch",
        }

        cfg["groq_api_key"] = self.ent_groq_key.get().strip()

        cfg["audio"] = {
            "enabled": self.var_audio_enabled.get(),
            "sample_rate": int(self.ent_sample_rate.get().strip() or "16000"),
            "chunk_duration_s": int(self.ent_chunk_dur.get().strip() or "30"),
            "language": self.ent_language.get().strip() or "en",
        }

        cfg["meeting_detection"] = {
            "poll_interval_s": int(self.ent_poll_interval.get().strip() or "2"),
            "window_patterns": {},
        }
        for key, ent in self.pattern_entries.items():
            raw = ent.get().strip()
            cfg["meeting_detection"]["window_patterns"][key] = (
                [p.strip() for p in raw.split(",") if p.strip()] if raw else []
            )

        cfg["keyboard_shortcuts"] = {}
        for app_key, actions in self.shortcut_entries.items():
            cfg["keyboard_shortcuts"][app_key] = {}
            for action, ent in actions.items():
                cfg["keyboard_shortcuts"][app_key][action] = ent.get().strip()

        return cfg

    # ── Save ────────────────────────────────────────────────────
    def _save(self):
        try:
            cfg = self._collect_config()

            # Validate
            if not cfg["mqtt"]["broker"]:
                messagebox.showerror("Validation", "MQTT Broker IP/hostname is required.")
                return

            with open(self.config_path, "w") as f:
                json.dump(cfg, f, indent=4)

            self.config = cfg
            self._set_status("Settings saved to config.json", "#4CAF50")

            if self.on_save_callback:
                self.on_save_callback(cfg)

        except ValueError as e:
            messagebox.showerror("Validation Error", f"Invalid number: {e}")
        except Exception as e:
            messagebox.showerror("Save Error", str(e))

    # ── Test MQTT Connection ────────────────────────────────────
    def _test_connection(self):
        if mqtt is None:
            messagebox.showerror("Error", "paho-mqtt not installed.\npip install paho-mqtt")
            return

        broker = self.ent_broker.get().strip()
        port = int(self.ent_port.get().strip() or "1883")
        user = self.ent_mqtt_user.get().strip()
        password = self.ent_mqtt_pass.get().strip()

        if not broker:
            messagebox.showerror("Error", "Enter a broker IP/hostname first.")
            return

        self._set_status(f"Testing connection to {broker}:{port}...", "#2196F3")
        self.root.update()

        def _test():
            result = {"connected": False, "error": ""}
            event = threading.Event()

            def on_connect(client, userdata, flags, rc):
                if rc == 0:
                    result["connected"] = True
                else:
                    codes = {
                        1: "Incorrect protocol",
                        2: "Invalid client ID",
                        3: "Server unavailable",
                        4: "Bad credentials",
                        5: "Not authorized",
                    }
                    result["error"] = codes.get(rc, f"Error code {rc}")
                event.set()

            client = mqtt.Client(client_id="espro_test", protocol=mqtt.MQTTv311)
            if user:
                client.username_pw_set(user, password)
            client.on_connect = on_connect

            try:
                client.connect(broker, port, keepalive=5)
                client.loop_start()
                event.wait(timeout=5)
                client.loop_stop()
                client.disconnect()
            except Exception as e:
                result["error"] = str(e)

            # Update UI from main thread
            self.root.after(0, lambda: self._show_test_result(result))

        threading.Thread(target=_test, daemon=True).start()

    def _show_test_result(self, result):
        if result["connected"]:
            self._set_status("Connection successful!", "#4CAF50")
            messagebox.showinfo("MQTT Test", "Successfully connected to MQTT broker!")
        else:
            err = result.get("error", "Timeout — broker not reachable")
            self._set_status(f"Connection failed: {err}", "#e53935")
            messagebox.showerror("MQTT Test", f"Connection failed:\n{err}")

    # ── Reset to defaults ───────────────────────────────────────
    def _reset_defaults(self):
        if not messagebox.askyesno("Reset", "Reset all settings to defaults?"):
            return

        self.config = dict(DEFAULT_CONFIG)

        # Clear and re-populate all fields
        for ent in [self.ent_broker, self.ent_port, self.ent_mqtt_user, self.ent_mqtt_pass,
                     self.ent_device_name, self.ent_groq_key, self.ent_sample_rate,
                     self.ent_chunk_dur, self.ent_language, self.ent_poll_interval]:
            ent.delete(0, tk.END)

        for entries in self.pattern_entries.values():
            entries.delete(0, tk.END)

        for app_entries in self.shortcut_entries.values():
            for ent in app_entries.values():
                ent.delete(0, tk.END)

        self._populate_fields()
        self._set_status("Reset to defaults (not saved yet)", "#FF9800")

    # ── Helpers ──────────────────────────────────────────────────
    def _toggle_key_visibility(self):
        show = "" if self.var_show_key.get() else "*"
        self.ent_groq_key.configure(show=show)

    def _set_status(self, text: str, color: str = "#888"):
        self.lbl_status.configure(text=text, foreground=color)

    def run(self):
        """Run the settings GUI (blocking)."""
        self.root.mainloop()


def open_settings(config_path: str, on_save_callback=None):
    """Open the settings GUI window."""
    gui = SettingsGUI(config_path, on_save_callback)
    gui.run()


if __name__ == "__main__":
    # Standalone — open settings GUI
    cfg_path = str(Path(__file__).parent / "config.json")
    open_settings(cfg_path)
