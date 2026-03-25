"""
Meeting Detector — Polls window titles to detect active video calls.

Supports Google Meet, Microsoft Teams, Zoom.
Uses pygetwindow for cross-platform window enumeration.
"""

import logging
import re

try:
    import pygetwindow as gw
except ImportError:
    gw = None

log = logging.getLogger("detector")


class MeetingDetector:
    """Detect active meeting apps by scanning window titles."""

    def __init__(self, config: dict):
        self.poll_interval = config.get("poll_interval_s", 2)
        self.patterns = config.get(
            "window_patterns",
            {
                "meet": ["Meet -", "Google Meet"],
                "teams": ["Microsoft Teams", "| Microsoft Teams"],
                "zoom": ["Zoom Meeting", "Zoom Webinar"],
            },
        )

    def detect(self) -> tuple:
        """
        Scan all window titles for meeting patterns.

        Returns:
            (app_name, window_title) — e.g. ("meet", "Meet - Project Sync")
            ("none", "") if no meeting detected
        """
        if gw is None:
            log.warning("pygetwindow not available — detection disabled")
            return ("none", "")

        try:
            titles = gw.getAllTitles()
        except Exception as e:
            log.debug(f"Window scan error: {e}")
            return ("none", "")

        for title in titles:
            if not title or len(title) < 3:
                continue
            for app, patterns in self.patterns.items():
                for pattern in patterns:
                    if pattern.lower() in title.lower():
                        return (app, title)

        return ("none", "")

    def is_meeting_focused(self, app: str) -> bool:
        """Check if the meeting window is currently focused."""
        if gw is None:
            return False

        try:
            active = gw.getActiveWindow()
            if active and active.title:
                for pattern in self.patterns.get(app, []):
                    if pattern.lower() in active.title.lower():
                        return True
        except Exception:
            pass
        return False
