/**
 * @file board_config.h
 * @brief Hardware configuration for LilyGO T-SIM7670G S3 V1.1
 * @date 2026
 * 
 * Board: LilyGO T-SIM7670G S3 V1.1
 * MCU: ESP32-S3-WROOM-1 (16MB Flash, 8MB PSRAM)
 * 
 * Integrated Components:
 * - SIM7670G 4G LTE Modem + GPS
 * - 18650 Battery holder + Charger
 * - Solar panel input (5-6V)
 * 
 * External Modules:
 * - SN65HVD230 CAN Transceiver (3.3V native)
 * - SD Card Module (SPI - uses board reserved pins)
 * - OLED SSD1306 0.91" Display (I2C)
 * - MPU-6050 Accelerometer/Gyroscope (I2C)
 */

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "driver/gpio.h"

// =============================================================================
// BOARD IDENTIFICATION
// =============================================================================
#define BOARD_NAME              "LilyGO T-SIM7670G S3"
#define BOARD_VERSION           "V1.1"
#define MCU_TYPE                "ESP32-S3-WROOM-1"
#define FLASH_SIZE_MB           16
#define PSRAM_SIZE_MB           8

// =============================================================================
// MODEM SIM7670G (Reserved - DO NOT USE)
// =============================================================================
#define MODEM_TX_PIN            GPIO_NUM_11
#define MODEM_RX_PIN            GPIO_NUM_10
#define MODEM_PWRKEY_PIN        GPIO_NUM_18
#define MODEM_RESET_PIN         GPIO_NUM_17
#define MODEM_RING_PIN          GPIO_NUM_3
#define MODEM_DTR_PIN           GPIO_NUM_9

// =============================================================================
// BOARD LED (Reserved)
// =============================================================================
#define BOARD_LED_PIN           GPIO_NUM_12

// =============================================================================
// BATTERY & SOLAR ADC (Reserved)
// =============================================================================
#define BATTERY_ADC_PIN         GPIO_NUM_4
#define SOLAR_ADC_PIN           GPIO_NUM_5

// =============================================================================
// SD CARD (SPI) - Board Reserved Pins
// =============================================================================
#define SD_CS_PIN               GPIO_NUM_13
#define SD_MOSI_PIN             GPIO_NUM_14
#define SD_CLK_PIN              GPIO_NUM_21
#define SD_MISO_PIN             GPIO_NUM_47

// =============================================================================
// CAN BUS (TWAI) - SN65HVD230 Transceiver
// =============================================================================
#define CAN_TX_PIN              GPIO_NUM_6
#define CAN_RX_PIN              GPIO_NUM_7
#define CAN_BITRATE             500000      // 500 kbps (OBD-II standard)

// =============================================================================
// I2C BUS - OLED SSD1306 + MPU-6050
// =============================================================================
#define I2C_SDA_PIN             GPIO_NUM_15
#define I2C_SCL_PIN             GPIO_NUM_16
#define I2C_FREQ_HZ             400000      // 400 kHz (Fast Mode)

// I2C Device Addresses
#define OLED_I2C_ADDR           0x3C        // SSD1306 default
#define MPU6050_I2C_ADDR        0x68        // AD0 = GND

// =============================================================================
// MPU-6050 CONFIGURATION
// =============================================================================
#define MPU6050_INT_PIN         GPIO_NUM_NC // Not connected (polling mode)
#define MPU6050_SAMPLE_RATE_HZ  50          // 50 Hz sampling
#define MPU6050_ACCEL_RANGE     2           // ±2g (0=2g, 1=4g, 2=8g, 3=16g)
#define MPU6050_GYRO_RANGE      250         // ±250°/s

// =============================================================================
// OLED DISPLAY CONFIGURATION
// =============================================================================
#define OLED_WIDTH              128
#define OLED_HEIGHT             32          // 0.91" display = 128x32
#define OLED_ROTATION           0           // 0=normal, 2=180°
#define OLED_REFRESH_MS         100         // Display refresh rate

// =============================================================================
// AVAILABLE GPIO PINS (Free to use)
// =============================================================================
// GPIO 1  - Available (ADC capable)
// GPIO 2  - Available (ADC capable)
// GPIO 8  - Available 
// GPIO 19 - Available
// GPIO 20 - Available
// GPIO 38 - Available (can be used for MPU6050 INT if needed)
// GPIO 39 - Available
// GPIO 40 - Available
// GPIO 41 - Available
// GPIO 42 - Available
// GPIO 48 - Available

// Strapping pins (use with caution):
// GPIO 0  - Boot button (strapping)
// GPIO 45 - Strapping pin
// GPIO 46 - Strapping pin

// =============================================================================
// FEATURE FLAGS
// =============================================================================
#define FEATURE_4G_LTE          1           // SIM7670G modem available
#define FEATURE_GPS             1           // GPS via SIM7670G
#define FEATURE_BATTERY         1           // 18650 battery support
#define FEATURE_SOLAR           1           // Solar charging support
#define FEATURE_SD_CARD         1           // External SD card module
#define FEATURE_OLED            1           // OLED display
#define FEATURE_IMU             1           // MPU-6050 IMU
#define FEATURE_CAN             1           // CAN bus (OBD-II)

#endif // BOARD_CONFIG_H
