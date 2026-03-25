"""
AI Notes — Groq-powered meeting transcription and summarization.

Uses Groq's Whisper API for STT and Llama for LLM summarization.
"""

import io
import struct
import logging

try:
    from groq import Groq
except ImportError:
    Groq = None

log = logging.getLogger("ai_notes")


class AINotes:
    """AI-powered meeting note-taking and summarization."""

    def __init__(self, api_key: str):
        self.api_key = api_key
        self.client = None
        if api_key and Groq:
            self.client = Groq(api_key=api_key)

        self.transcript_lines = []
        self.full_transcript = []

    def has_transcript(self) -> bool:
        """Check if there are any transcribed lines."""
        return len(self.full_transcript) > 0

    def reset(self):
        """Reset all transcript data for a new meeting."""
        self.transcript_lines = []
        self.full_transcript = []

    def transcribe(self, audio_data, sample_rate: int) -> str:
        """
        Transcribe audio data using Groq Whisper.

        Args:
            audio_data: numpy int16 array of audio samples
            sample_rate: sample rate of the audio

        Returns:
            Transcribed text string
        """
        if not self.client:
            return ""

        # Create WAV file in memory
        wav_buffer = self._create_wav(audio_data, sample_rate)

        try:
            transcription = self.client.audio.transcriptions.create(
                file=("audio.wav", wav_buffer, "audio/wav"),
                model="whisper-large-v3-turbo",
                language="en",
                response_format="text",
            )

            text = str(transcription).strip()
            if text:
                self.transcript_lines.append(text)
                self.full_transcript.append(text)
                # Keep only last 20 lines in rolling buffer
                if len(self.transcript_lines) > 20:
                    self.transcript_lines = self.transcript_lines[-20:]

            return text

        except Exception as e:
            log.error(f"Transcription error: {e}")
            return ""

    def summarize(self) -> str:
        """Generate end-of-meeting summary using Groq LLM."""
        if not self.client or not self.full_transcript:
            return "No transcript available."

        full_text = "\n".join(self.full_transcript)
        # Truncate to fit context window
        if len(full_text) > 8000:
            full_text = full_text[-8000:]

        try:
            response = self.client.chat.completions.create(
                model="llama-3.3-70b-versatile",
                messages=[
                    {
                        "role": "system",
                        "content": (
                            "You are a concise meeting summarizer. "
                            "Summarize the key discussion points, decisions, "
                            "and action items from this meeting transcript. "
                            "Keep it under 200 words. Use bullet points."
                        ),
                    },
                    {
                        "role": "user",
                        "content": f"Meeting transcript:\n{full_text}",
                    },
                ],
                max_tokens=300,
                temperature=0.3,
            )
            return response.choices[0].message.content.strip()
        except Exception as e:
            log.error(f"Summarization error: {e}")
            return f"Summary failed: {e}"

    def summarize_handoff(self, notes: list) -> str:
        """Summarize what was discussed while user was away."""
        if not self.client or not notes:
            return "Nothing discussed while you were away."

        text = "\n".join(notes[-15:])  # Last 15 chunks

        try:
            response = self.client.chat.completions.create(
                model="llama-3.3-70b-versatile",
                messages=[
                    {
                        "role": "system",
                        "content": (
                            "You are a meeting note assistant. "
                            "The user was away from a meeting. "
                            "Summarize what was discussed while they were away. "
                            "Focus on key points, decisions, and anything "
                            "the user needs to know. Keep it under 150 words."
                        ),
                    },
                    {
                        "role": "user",
                        "content": f"Discussion while I was away:\n{text}",
                    },
                ],
                max_tokens=200,
                temperature=0.3,
            )
            return response.choices[0].message.content.strip()
        except Exception as e:
            log.error(f"Handoff summary error: {e}")
            return f"Handoff summary failed: {e}"

    @staticmethod
    def _create_wav(audio_data, sample_rate: int) -> io.BytesIO:
        """Create an in-memory WAV file from int16 audio data."""
        buf = io.BytesIO()
        num_samples = len(audio_data)
        data_size = num_samples * 2  # 16-bit = 2 bytes per sample

        # WAV header
        buf.write(b"RIFF")
        buf.write(struct.pack("<I", 36 + data_size))
        buf.write(b"WAVE")
        buf.write(b"fmt ")
        buf.write(struct.pack("<I", 16))  # Chunk size
        buf.write(struct.pack("<H", 1))  # PCM format
        buf.write(struct.pack("<H", 1))  # Mono
        buf.write(struct.pack("<I", sample_rate))
        buf.write(struct.pack("<I", sample_rate * 2))  # Byte rate
        buf.write(struct.pack("<H", 2))  # Block align
        buf.write(struct.pack("<H", 16))  # Bits per sample
        buf.write(b"data")
        buf.write(struct.pack("<I", data_size))
        buf.write(audio_data.tobytes())

        buf.seek(0)
        return buf
