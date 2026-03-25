"""
Audio Capture — Records system/loopback audio for meeting transcription.

Uses the `soundcard` library to capture from the default loopback device
(what you hear). Captures in chunks and sends to a callback for STT.
"""

import logging
import threading
import time
import numpy as np

try:
    import soundcard as sc
except ImportError:
    sc = None

log = logging.getLogger("audio")


class AudioCapture:
    """Capture system audio (loopback) in chunks for transcription."""

    def __init__(self, config: dict):
        self.sample_rate = config.get("sample_rate", 16000)
        self.chunk_duration = config.get("chunk_duration_s", 30)
        self.enabled = config.get("enabled", True)
        self.recover_delay_s = config.get("recover_delay_s", 1.5)
        self._running = False
        self._thread = None
        self._callback = None
        self._last_error = ""
        self._last_error_ts = 0.0

    def start(self, callback):
        """
        Start capturing audio.

        callback(audio_data: np.ndarray, sample_rate: int) called
        every chunk_duration seconds with the audio data.
        """
        if not self.enabled or sc is None:
            log.warning("Audio capture disabled or soundcard not available")
            return

        self._callback = callback
        self._running = True
        self._thread = threading.Thread(target=self._capture_loop, daemon=True)
        self._thread.start()
        log.info(
            f"Audio capture started "
            f"(rate={self.sample_rate}, chunk={self.chunk_duration}s)"
        )

    def stop(self):
        """Stop capturing."""
        self._running = False
        if self._thread:
            self._thread.join(timeout=5)
            self._thread = None
        log.info("Audio capture stopped")

    def _capture_loop(self):
        """Continuously capture audio chunks from loopback."""
        frames_per_chunk = self.sample_rate * self.chunk_duration

        while self._running:
            try:
                speaker = sc.default_speaker()
                if speaker is None:
                    self._log_capture_error("No default speaker available")
                    time.sleep(self.recover_delay_s)
                    continue

                # Re-bind every recovery cycle to survive device switches.
                loopback = sc.get_microphone(id=str(speaker.name), include_loopback=True)
                if loopback is None:
                    self._log_capture_error("No loopback device found")
                    time.sleep(self.recover_delay_s)
                    continue

                with loopback.recorder(samplerate=self.sample_rate) as recorder:
                    log.info("Audio loopback attached: %s", speaker.name)
                    while self._running:
                        try:
                            data = recorder.record(numframes=frames_per_chunk)

                            # Convert to mono float32
                            if data.ndim > 1:
                                data = np.mean(data, axis=1)

                            # Convert to int16 for Whisper
                            audio_int16 = (data * 32767).astype(np.int16)

                            # Check if there's actual audio (not silence)
                            rms = np.sqrt(np.mean(data**2))
                            if rms > 0.001:  # Threshold for non-silence
                                if self._callback:
                                    self._callback(audio_int16, self.sample_rate)
                            else:
                                log.debug("Chunk was silence, skipping")

                        except Exception as e:
                            # Typical Windows WASAPI transient failure (e.g. 0x88890007).
                            self._log_capture_error(f"Capture chunk error: {e}")
                            time.sleep(self.recover_delay_s)
                            break

            except Exception as e:
                self._log_capture_error(f"Audio capture setup error: {e}")
                time.sleep(self.recover_delay_s)

    def _log_capture_error(self, msg: str):
        """Throttle repeated identical capture errors to avoid log spam."""
        now = time.time()
        if msg != self._last_error or (now - self._last_error_ts) > 10.0:
            log.error(msg)
            self._last_error = msg
            self._last_error_ts = now
