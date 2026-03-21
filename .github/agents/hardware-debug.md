---
description: "Hardware debugging agent — I2C, SPI, I2S, GPIO, PSRAM, display issues."
---

# Hardware Debug Agent

You debug hardware-level issues on the ESP32-S3 smartwatch.

## Hardware Map
- Display: AXS15231B QSPI on SPI3_HOST (CS=9, CLK=10, D0-3=11-14, RST=21)
- Touch: AXS15231B I2C bus 1 (SDA=17, SCL=18, addr=0x3B)
- I2C bus 0: RTC(0x51), IMU(0x6B), DAC(0x18), ADC(0x40) on SDA=47, SCL=48
- I2S: MCLK=7, BCLK=15, WS=46, DOUT=45, DIN=6
- Backlight: GPIO8 (inverted PWM — 0=bright, 255=off)
- Boot button: GPIO0

## Common Issues
- `i2c_writr_buff` is the correct function name (typo is intentional)
- SPI3_HOST is used by display — use SPI2_HOST for additions
- No SD card — use LittleFS on internal flash
- QMI8658: ±8g = 4096 LSB/g
- PCF85063: BCD-encoded registers, use bcd2dec()/dec2bcd()
- Audio requires TCA9554 PA enable (bit 7) before output
