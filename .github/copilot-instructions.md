# ESPro AI — Copilot Instructions

## Project Overview

AI-first multi-modal smartwatch OS for ESP32-S3 (Waveshare 3.49" AMOLED).
Built with **ESP-IDF v5.5.1**, **LVGL v8.4**, **FreeRTOS**.

**Design Philosophy:** Every interaction is AI-mediated. Voice is the primary
input. The agent orchestrator routes requests to specialized agents that can
run concurrently in background.

---

## Architecture Layers

```
┌─────────────────────────────────────────────────┐
│  AI Layer (agent_orchestrator, voice_pipeline,   │
│            llm_client, stt_client, intent_engine)│
├─────────────────────────────────────────────────┤
│  UI Layer (ui_manager, theme, screens, widgets)  │
├─────────────────────────────────────────────────┤
│  Services (wifi_manager, storage)                │
├─────────────────────────────────────────────────┤
│  Core OS (os_kernel, event_bus, perf_monitor)    │
├─────────────────────────────────────────────────┤
│  HAL (display, touch, audio, imu, rtc)           │
├─────────────────────────────────────────────────┤
│  Hardware (ESP32-S3 + peripherals)               │
└─────────────────────────────────────────────────┘
```

## Task Architecture (FreeRTOS)

| Task | Core | Prio | Stack | Purpose |
|------|------|------|-------|---------|
| lvgl_render | 0 | 5 | 8192 | lv_timer_handler() |
| touch_input | 0 | 4 | 3072 | Touch polling + gesture |
| sensor_fusion | 0 | 3 | 4096 | IMU + steps + gestures |
| perf_monitor | 0 | 1 | 3072 | Stats logging |
| event_dispatcher | 1 | 5 | 4096 | Event bus delivery |
| app_manager | 1 | 4 | 4096 | Screen state machine |
| voice_pipeline | 1 | 3 | 24K PSRAM | Mic→STT→Intent |
| agent_orchestrator | 1 | 2 | 8192 | Multi-agent routing |

## Communication: Event Bus

All inter-task communication uses the event bus (`event_bus.h`).
Tasks **never** call each other directly. They post events and subscribe
to categories. This enables:
- Decoupled architecture
- Performance logging on every event
- Replay/testing capability

## AI Agents

Agents register with the orchestrator and declare:
- `can_handle(input)` — whether they can process this input
- `process(req, resp)` — synchronous processing
- `background_tick()` — periodic background work

Current agents: Voice, Proactive, Wellness, Task.

## Hardware

Same as existing ESPro project:
- Display: 640×172 AMOLED (AXS15231B QSPI)
- Touch: AXS15231B (I2C bus 1, 0x3B)
- Audio: ES8311 DAC + ES7210 ADC (I2S full-duplex)
- IMU: QMI8658 (±8g, ±512dps)
- RTC: PCF85063

See `hw_config.h` for all pin definitions.

## Performance Monitoring

Every subsystem logs latency metrics via `perf_log_event()`.
The perf monitor:
- Prints console report every 30s
- Flushes CSV to `/littlefs/perf_log.csv` every 5 min
- Tracks: min/max/avg/count per named slot

## Coding Rules

1. **C only** — ESP-IDF + FreeRTOS
2. **LVGL v8.4** — not v9. Use `lvgl_lock()`/`lvgl_unlock()` mutex
3. **Event bus** — never call between tasks directly
4. **Perf logging** — log latency in every I/O operation
5. **PSRAM** for large buffers (>1KB): `heap_caps_malloc(sz, MALLOC_CAP_SPIRAM)`
6. **Theme vars** — use `th_bg`, `th_text`, etc. Never hardcode colors
7. **640×172** display — all layouts for this widescreen
8. **i2c_writr_buff** — the typo is intentional, do not fix
9. **snprintf** always — never sprintf
10. **Secrets** in `secrets.h` (gitignored)
