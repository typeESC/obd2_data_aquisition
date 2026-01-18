/**
 * @file mpu6050.h
 * @brief MPU-6050 Accelerometer/Gyroscope driver for ESP32-S3
 * @date 2026
 * 
 * Features:
 * - 3-axis accelerometer (±2g, ±4g, ±8g, ±16g)
 * - 3-axis gyroscope (±250, ±500, ±1000, ±2000 °/s)
 * - Temperature sensor
 * - Polling mode (no INT pin required)
 * - Thread-safe with mutex protection
 */

#ifndef MPU6050_H
#define MPU6050_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// =============================================================================
// MPU-6050 REGISTER DEFINITIONS
// =============================================================================
#define MPU6050_REG_SMPLRT_DIV      0x19    // Sample Rate Divider
#define MPU6050_REG_CONFIG          0x1A    // Configuration
#define MPU6050_REG_GYRO_CONFIG     0x1B    // Gyroscope Configuration
#define MPU6050_REG_ACCEL_CONFIG    0x1C    // Accelerometer Configuration
#define MPU6050_REG_FIFO_EN         0x23    // FIFO Enable
#define MPU6050_REG_INT_PIN_CFG     0x37    // INT Pin / Bypass Enable Configuration
#define MPU6050_REG_INT_ENABLE      0x38    // Interrupt Enable
#define MPU6050_REG_INT_STATUS      0x3A    // Interrupt Status
#define MPU6050_REG_ACCEL_XOUT_H    0x3B    // Accelerometer Measurements
#define MPU6050_REG_TEMP_OUT_H      0x41    // Temperature Measurement
#define MPU6050_REG_GYRO_XOUT_H     0x43    // Gyroscope Measurements
#define MPU6050_REG_USER_CTRL       0x6A    // User Control
#define MPU6050_REG_PWR_MGMT_1      0x6B    // Power Management 1
#define MPU6050_REG_PWR_MGMT_2      0x6C    // Power Management 2
#define MPU6050_REG_WHO_AM_I        0x75    // Who Am I

#define MPU6050_WHO_AM_I_VAL        0x68    // Expected WHO_AM_I value

// =============================================================================
// CONFIGURATION ENUMS
// =============================================================================

/** Accelerometer full-scale range */
typedef enum {
    MPU6050_ACCEL_FS_2G  = 0,   // ±2g  (16384 LSB/g)
    MPU6050_ACCEL_FS_4G  = 1,   // ±4g  (8192 LSB/g)
    MPU6050_ACCEL_FS_8G  = 2,   // ±8g  (4096 LSB/g)
    MPU6050_ACCEL_FS_16G = 3    // ±16g (2048 LSB/g)
} mpu6050_accel_fs_t;

/** Gyroscope full-scale range */
typedef enum {
    MPU6050_GYRO_FS_250  = 0,   // ±250°/s  (131 LSB/°/s)
    MPU6050_GYRO_FS_500  = 1,   // ±500°/s  (65.5 LSB/°/s)
    MPU6050_GYRO_FS_1000 = 2,   // ±1000°/s (32.8 LSB/°/s)
    MPU6050_GYRO_FS_2000 = 3    // ±2000°/s (16.4 LSB/°/s)
} mpu6050_gyro_fs_t;

/** Digital Low Pass Filter bandwidth */
typedef enum {
    MPU6050_DLPF_260HZ = 0,     // Bandwidth 260Hz, Delay 0ms
    MPU6050_DLPF_184HZ = 1,     // Bandwidth 184Hz, Delay 2.0ms
    MPU6050_DLPF_94HZ  = 2,     // Bandwidth 94Hz, Delay 3.0ms
    MPU6050_DLPF_44HZ  = 3,     // Bandwidth 44Hz, Delay 4.9ms
    MPU6050_DLPF_21HZ  = 4,     // Bandwidth 21Hz, Delay 8.5ms
    MPU6050_DLPF_10HZ  = 5,     // Bandwidth 10Hz, Delay 13.8ms
    MPU6050_DLPF_5HZ   = 6      // Bandwidth 5Hz, Delay 19.0ms
} mpu6050_dlpf_t;

// =============================================================================
// DATA STRUCTURES
// =============================================================================

/** Raw sensor data (16-bit signed) */
typedef struct {
    int16_t accel_x;    // Accelerometer X (raw)
    int16_t accel_y;    // Accelerometer Y (raw)
    int16_t accel_z;    // Accelerometer Z (raw)
    int16_t gyro_x;     // Gyroscope X (raw)
    int16_t gyro_y;     // Gyroscope Y (raw)
    int16_t gyro_z;     // Gyroscope Z (raw)
    int16_t temp_raw;   // Temperature (raw)
} mpu6050_raw_data_t;

/** Converted sensor data (engineering units) */
typedef struct {
    float accel_x_g;    // Accelerometer X (g)
    float accel_y_g;    // Accelerometer Y (g)
    float accel_z_g;    // Accelerometer Z (g)
    float gyro_x_dps;   // Gyroscope X (degrees/second)
    float gyro_y_dps;   // Gyroscope Y (degrees/second)
    float gyro_z_dps;   // Gyroscope Z (degrees/second)
    float temp_c;       // Temperature (°C)
    uint64_t timestamp; // Timestamp in microseconds
} mpu6050_data_t;

/** Motion event detection */
typedef struct {
    bool is_moving;         // Vehicle in motion
    bool impact_detected;   // Sudden deceleration detected
    bool rollover_risk;     // Excessive tilt detected
    float impact_g;         // Peak G-force during impact
    float tilt_angle;       // Current tilt angle (degrees)
    float acceleration_mag; // Total acceleration magnitude (g)
} mpu6050_motion_t;

/** MPU-6050 configuration */
typedef struct {
    uint8_t i2c_addr;           // I2C address (0x68 or 0x69)
    mpu6050_accel_fs_t accel_fs; // Accelerometer range
    mpu6050_gyro_fs_t gyro_fs;   // Gyroscope range
    mpu6050_dlpf_t dlpf;         // Low-pass filter bandwidth
    uint8_t sample_rate_div;     // Sample rate = 1kHz / (1 + div)
} mpu6050_config_t;

// =============================================================================
// PUBLIC FUNCTIONS
// =============================================================================

/**
 * @brief Initialize MPU-6050 sensor
 * @param config Configuration structure (NULL for defaults)
 * @return ESP_OK on success
 */
esp_err_t mpu6050_init(const mpu6050_config_t *config);

/**
 * @brief Deinitialize MPU-6050 sensor
 * @return ESP_OK on success
 */
esp_err_t mpu6050_deinit(void);

/**
 * @brief Check if MPU-6050 is connected and responding
 * @return true if sensor is available
 */
bool mpu6050_is_available(void);

/**
 * @brief Read raw sensor data
 * @param data Pointer to raw data structure
 * @return ESP_OK on success
 */
esp_err_t mpu6050_read_raw(mpu6050_raw_data_t *data);

/**
 * @brief Read converted sensor data (engineering units)
 * @param data Pointer to data structure
 * @return ESP_OK on success
 */
esp_err_t mpu6050_read(mpu6050_data_t *data);

/**
 * @brief Get motion detection status
 * @param motion Pointer to motion structure
 * @return ESP_OK on success
 */
esp_err_t mpu6050_get_motion(mpu6050_motion_t *motion);

/**
 * @brief Calibrate accelerometer offset (place sensor flat!)
 * @param samples Number of samples to average (default 100)
 * @return ESP_OK on success
 */
esp_err_t mpu6050_calibrate(uint16_t samples);

/**
 * @brief Set accelerometer full-scale range
 * @param fs Full-scale range
 * @return ESP_OK on success
 */
esp_err_t mpu6050_set_accel_range(mpu6050_accel_fs_t fs);

/**
 * @brief Set gyroscope full-scale range
 * @param fs Full-scale range
 * @return ESP_OK on success
 */
esp_err_t mpu6050_set_gyro_range(mpu6050_gyro_fs_t fs);

/**
 * @brief Enter low-power mode
 * @return ESP_OK on success
 */
esp_err_t mpu6050_sleep(void);

/**
 * @brief Exit low-power mode
 * @return ESP_OK on success
 */
esp_err_t mpu6050_wake(void);

/**
 * @brief Get sensor temperature
 * @param temp_c Pointer to store temperature in °C
 * @return ESP_OK on success
 */
esp_err_t mpu6050_get_temperature(float *temp_c);

// =============================================================================
// THRESHOLD CONFIGURATION
// =============================================================================
#define MPU6050_IMPACT_THRESHOLD_G      2.5f    // G-force for impact detection
#define MPU6050_MOVING_THRESHOLD_G      0.1f    // G-force for motion detection
#define MPU6050_TILT_WARNING_DEG        45.0f   // Tilt angle warning threshold

#endif // MPU6050_H
