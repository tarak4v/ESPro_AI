@echo off
REM ============================================================
REM  ESPro AI Companion App — Build EXE with PyInstaller
REM ============================================================
REM
REM  Prerequisites:
REM    pip install -r requirements.txt
REM
REM  Output: dist\meeting_assistant.exe (single file, ~30MB)
REM

echo === ESPro AI Companion App — Building EXE ===

REM Ensure dependencies are installed
pip install -r requirements.txt

REM Build single-file windowed EXE
pyinstaller ^
    --onefile ^
    --name espro_ai_companion ^
    --icon NUL ^
    --add-data "config.json;." ^
    --hidden-import paho.mqtt.client ^
    --hidden-import soundcard ^
    --hidden-import keyboard ^
    --hidden-import pygetwindow ^
    --hidden-import groq ^
    --hidden-import numpy ^
    --hidden-import settings_gui ^
    --hidden-import meeting_assistant ^
    --hidden-import meeting_detector ^
    --hidden-import meeting_controller ^
    --hidden-import audio_capture ^
    --hidden-import mqtt_bridge ^
    --hidden-import ai_notes ^
    --hidden-import tkinter ^
    --hidden-import pystray ^
    --hidden-import PIL ^
    --noconsole ^
    companion_app.py

if %ERRORLEVEL% EQU 0 (
    echo.
    echo === Build successful! ===
    echo EXE location: dist\meeting_assistant.exe
    echo.
    echo To run:
    echo   1. Copy config.json to same folder as EXE
    echo   2. Edit config.json with your MQTT broker and Groq API key
    echo   3. Run: meeting_assistant.exe
    echo.
) else (
    echo.
    echo === Build FAILED ===
    echo Check the error messages above.
)

pause
