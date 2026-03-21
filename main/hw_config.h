/**
 * @file hw_config.h
 * @brief Hardware pin definitions, I2C addresses, display parameters.
 *
 * Waveshare ESP32-S3-Touch-LCD-3.49 (640×172 AMOLED, QSPI AXS15231B)
 */
#ifndef HW_CONFIG_H
#define HW_CONFIG_H

/* ── Display (AXS15231B QSPI) ── */
#define LCD_QSPI_HOST       SPI3_HOST
#define LCD_PIN_CS           9
#define LCD_PIN_CLK         10
#define LCD_PIN_D0          11
#define LCD_PIN_D1          12
#define LCD_PIN_D2          13
#define LCD_PIN_D3          14
#define LCD_PIN_RST         21

/* Display geometry — physically 172×640 portrait, rotated 90° CCW */
#define LCD_H_RES           640
#define LCD_V_RES           172
#define LCD_NOROT_HRES      172
#define LCD_NOROT_VRES      640

/* Frame buffer sizing */
#define LVGL_SPIRAM_BUFF_LEN   (LCD_NOROT_HRES * LCD_NOROT_VRES * 2) /* ~220 KB */
#define LCD_DMA_BUF_LEN        (LCD_NOROT_HRES * 64 * 2)             /* ~22 KB  */

/* ── Backlight (inverted PWM) ── */
#define LCD_PIN_BL           8
#define PIN_NUM_BK_LIGHT     LCD_PIN_BL   /* alias for lcd_bl_pwm_bsp component */
#define BL_LEDC_CHANNEL      0
#define BL_LEDC_TIMER        0

/* ── I2C Bus 0 (sensors + audio codecs) ── */
#define I2C0_SDA            47
#define I2C0_SCL            48
#define I2C0_PORT           I2C_NUM_0
#define I2C0_FREQ_HZ        400000
#define ESP_SCL_NUM          I2C0_SCL     /* alias for i2c_bsp component */
#define ESP_SDA_NUM          I2C0_SDA     /* alias for i2c_bsp component */

/* ── I2C Bus 1 (touch panel) ── */
#define I2C1_SDA            17
#define I2C1_SCL            18
#define I2C1_PORT           I2C_NUM_1
#define I2C1_FREQ_HZ        400000
#define TOUCH_SCL_NUM        I2C1_SCL     /* alias for i2c_bsp component */
#define TOUCH_SDA_NUM        I2C1_SDA     /* alias for i2c_bsp component */

/* ── I2C Device Addresses ── */
#define RTC_ADDR            0x51   /* PCF85063 */
#define IMU_ADDR            0x6B   /* QMI8658  */
#define DAC_ADDR            0x18   /* ES8311   */
#define ADC_ADDR            0x40   /* ES7210   */
#define TOUCH_ADDR          0x3B   /* AXS15231B touch */
/* Aliases for i2c_bsp component */
#define RTC_I2C_ADDR         RTC_ADDR
#define IMU_I2C_ADDR         IMU_ADDR
#define ES8311_I2C_ADDR      DAC_ADDR
#define ES7210_I2C_ADDR      ADC_ADDR
#define TOUCH_I2C_ADDR       TOUCH_ADDR

/* ── I2S Audio ── */
#define I2S_PIN_MCLK         7
#define I2S_PIN_BCLK        15
#define I2S_PIN_WS          46
#define I2S_PIN_DOUT        45
#define I2S_PIN_DIN          6
#define AUDIO_SAMPLE_RATE   16000

/* ── Boot Button ── */
#define BOOT_BTN_GPIO        0

/* ── IMU Config ── */
#define IMU_ACCEL_RANGE     8      /* ±8g */
#define IMU_ACCEL_LSB_G     4096.0f
#define IMU_GYRO_RANGE      512    /* ±512 dps */
#define IMU_ODR             250    /* Hz */

/* ── TCA9554 IO Expander ── */
#define TCA9554_PA_BIT       7
#define TCA9554_CODEC_BIT    6
#define TCA9554_PA_PIN_BIT       TCA9554_PA_BIT      /* alias for i2c_bsp */
#define TCA9554_CODEC_PIN_BIT    TCA9554_CODEC_BIT   /* alias for i2c_bsp */

#endif /* HW_CONFIG_H */
