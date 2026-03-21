# ESPro AI — Multi-Modal AI Smartwatch OS

An AI-first smartwatch operating system for ESP32-S3, designed around voice interaction
and a multi-agent architecture. Built with ESP-IDF v5.5.1, LVGL v8.4, and FreeRTOS.

**Hardware:** Waveshare ESP32-S3-Touch-LCD-3.49 (640×172 AMOLED)

---

## Architecture

```
┌─────────────────────────────────────────────────┐
│  AI Layer                                       │
│  ├── Agent Orchestrator (multi-agent routing)   │
│  ├── Voice Pipeline (VAD → STT → Intent → TTS) │
│  ├── LLM Client (Groq Llama 3.3 70B)          │
│  ├── STT Client (Groq Whisper)                 │
│  └── Intent Engine (offline keyword matching)   │
├─────────────────────────────────────────────────┤
│  UI Layer (LVGL 8.4)                           │
│  ├── UI Manager (render task + screen lifecycle)│
│  ├── Theme Engine (dark/light, NVS persistent) │
│  ├── Screens (Home, Apps, Settings)            │
│  └── Widgets (status bar, voice indicator)      │
├─────────────────────────────────────────────────┤
│  Services                                       │
│  ├── WiFi Manager (STA + NTP + auto-reconnect) │
│  └── Storage (LittleFS 7MB + NVS helpers)      │
├─────────────────────────────────────────────────┤
│  Core OS                                        │
│  ├── OS Kernel (task creation, core pinning)   │
│  ├── Event Bus (queue-based pub/sub)           │
│  └── Performance Monitor (latency + CSV logs)  │
├─────────────────────────────────────────────────┤
│  HAL (Hardware Abstraction Layer)              │
│  ├── Display (AXS15231B QSPI, 640×172)        │
│  ├── Touch (capacitive, gesture detection)      │
│  ├── Audio (I2S, ES8311 DAC, ES7210 ADC)      │
│  ├── IMU (QMI8658, steps, gestures)            │
│  └── RTC (PCF85063, BCD time)                  │
└─────────────────────────────────────────────────┘
```

## Task Model (Dual-Core)

| Task | Core | Priority | Stack | Rate | Purpose |
|------|------|----------|-------|------|---------|
| LVGL Render | 0 | 5 | 8 KB | ~200 Hz | `lv_timer_handler()` |
| Touch Input | 0 | 4 | 3 KB | 50 Hz | Touch polling + swipe detection |
| Sensor Fusion | 0 | 3 | 4 KB | 50 Hz | IMU, steps, gestures |
| Perf Monitor | 0 | 1 | 3 KB | 0.2 Hz | Stats reporting |
| Event Dispatcher | 1 | 5 | 4 KB | On-demand | Event bus delivery |
| App Manager | 1 | 4 | 4 KB | 10 Hz | Screen state machine |
| Voice Pipeline | 1 | 3 | 24 KB* | Continuous | Mic → STT → Intent |
| Agent Orchestrator | 1 | 2 | 8 KB | On-demand | AI agent routing |

*Voice pipeline stack allocated in PSRAM

## AI Agents

The orchestrator manages specialized agents:

| Agent | Type | Background | Purpose |
|-------|------|-----------|---------|
| Voice | Foreground | No | STT → LLM for open queries |
| Proactive | Background | 60s | Context-aware suggestions |
| Wellness | Background | 30s | Health monitoring, reminders |
| Task | Foreground | No | Productivity, timers, calendar |

Agents declare `can_handle()` for auto-routing and `background_tick()` for periodic work.

## Event Bus

All inter-task communication uses typed events:
- **System:** boot, sleep, wake
- **Input:** tap, swipe, button, long press
- **UI:** screen change, notifications
- **AI:** voice wake, STT result, agent response
- **Sensor:** gesture, orientation, step count
- **Network:** WiFi connect/disconnect, NTP sync

Events carry timestamps for latency measurement.

## Performance Monitoring

Every subsystem logs metrics via `perf_log_event(slot, ms)`:
- Console report every 30 seconds
- CSV flush to LittleFS every 5 minutes
- Python analyzer: `tools/perf_analyzer.py`

## Project Structure

```
main/
├── core/
│   ├── main.c              # Boot sequence (10 stages)
│   ├── os_kernel.c/h       # Task creation + LVGL mutex
│   ├── event_bus.c/h       # Pub/sub event system
│   └── perf_monitor.c/h    # Latency tracking + CSV logs
├── hal/
│   ├── display.c/h         # AXS15231B QSPI + LVGL driver
│   ├── touch.c/h           # Touch input + gesture detection
│   ├── audio.c/h           # I2S + codec + mic recording
│   ├── imu.c/h             # QMI8658 + step counter + gestures
│   └── rtc.c/h             # PCF85063 BCD time
├── ui/
│   ├── ui_manager.c/h      # LVGL task + screen lifecycle
│   ├── theme.c/h           # Dark/light palette
│   ├── screens/
│   │   ├── scr_home.c      # Clock + date + weather
│   │   ├── scr_apps.c      # App launcher grid
│   │   └── scr_settings.c  # Settings + perf display
│   └── widgets/
│       ├── wgt_status_bar.c
│       └── wgt_voice_indicator.c
├── ai/
│   ├── agent_orchestrator.c/h  # Multi-agent manager
│   ├── voice_pipeline.c/h     # VAD → STT → Intent pipeline
│   ├── llm_client.c/h         # Groq chat completions
│   ├── stt_client.c/h         # Groq Whisper transcription
│   └── intent_engine.c/h      # Offline keyword matching
├── services/
│   ├── wifi_manager.c/h       # WiFi STA + NTP
│   └── storage.c/h            # LittleFS + NVS unified
├── test/
│   └── perf_test.c/h          # Boot-time benchmarks
├── hw_config.h                # Pin definitions
└── secrets.h.example          # API keys template
```

## MCP Servers

Configured in `.vscode/mcp.json`:
- **context7** — ESP-IDF/LVGL/FreeRTOS documentation lookup
- **github** — Repository management (issues, PRs)
- **filesystem** — Direct file access
- **memory** — Persistent architecture decisions
- **sequential-thinking** — Complex problem decomposition

## Copilot Agents

Defined in `.github/agents/`:
- **firmware-dev** — C/ESP-IDF coding, task architecture
- **ai-pipeline** — Voice, STT, LLM, intent, agents
- **hardware-debug** — I2C, SPI, GPIO, memory issues
- **perf-analyst** — Performance analysis and optimization

## Build & Flash

```bash
# Environment setup
. $IDF_PATH/export.sh       # Linux/Mac
. $env:IDF_PATH\export.ps1  # Windows

# Build
idf.py build

# Flash
idf.py -p COM10 flash monitor

# Performance analysis
python tools/perf_analyzer.py --serial COM10
python tools/perf_analyzer.py --csv perf_log.csv
```

## Quick Start

1. Copy `main/secrets.h.example` → `main/secrets.h` and fill in API keys
2. `idf.py set-target esp32s3`
3. `idf.py build`
4. `idf.py -p COM10 flash monitor`

## License

MIT
