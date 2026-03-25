# -*- mode: python ; coding: utf-8 -*-


a = Analysis(
    ['companion_app.py'],
    pathex=[],
    binaries=[],
    datas=[('config.json', '.')],
    hiddenimports=['paho.mqtt.client', 'soundcard', 'keyboard', 'pygetwindow', 'groq', 'numpy', 'settings_gui', 'meeting_assistant', 'meeting_detector', 'meeting_controller', 'audio_capture', 'mqtt_bridge', 'ai_notes', 'tkinter', 'pystray', 'PIL'],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name='espro_ai_companion',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon=['NUL'],
)
