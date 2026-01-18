/**
 * @file mpu6050.c
 * @brief MPU-6050 Accelerometer/Gyroscope driver implementation
 * @date 2026
 */

#include "mpu6050.h"
#include "board_config.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <math.h>

#define TAG "MPU6050"

// I2C Configuration
#define I2C_MASTER_NUM          I2C_NUM_0
#define I2C_MASTER_TIMEOUT_MS   100

// Sensitivity scale factors
static const float ACCEL_SENSITIVITY[] = {16384.0f, 8192.0f, 4096.0f, 2048.0f};
static const float GYRO_SENSITIVITY[] = {131.0f, 65.5f, 32.8f, 16.4f};

// Module state
static struct {
    bool initialized;
    bool i2c_initialized;
    uint8_t i2c_addr;
    mpu6050_accel_fs_t accel_fs;
    mpu6050_gyro_fs_t gyro_fs;
    float accel_offset_x;
    float accel_offset_y;
    float accel_offset_z;
    float gyro_offset_x;
    float gyro_offset_y;
    float gyro_offset_z;
    SemaphoreHandle_t mutex;
} mpu6050_state = {0};

// =============================================================================
// INTERNAL FUNCTIONS
// =============================================================================

// Note: I2C bus MUST be initialized by main.c before calling mpu6050_init()
// This function just marks the state as ready
static esp_err_t i2c_master_init(void) {
    if (mpu6050_state.i2c_initialized) {
        return ESP_OK;
    }
    
    // Assume I2C is already initialized by main.c
    // Just mark as initialized and return success
    mpu6050_state.i2c_initialized = true;
    ESP_LOGI(TAG, "Using I2C bus initialized by main.c");
    return ESP_OK;
}

static esp_err_t mpu6050_write_byte(uint8_t reg, uint8_t data) {
    uint8_t write_buf[2] = {reg, data};
    return i2c_master_write_to_device(I2C_MASTER_NUM, mpu6050_state.i2c_addr,
                                      write_buf, sizeof(write_buf),
                                      pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
}

static esp_err_t mpu6050_read_bytes(uint8_t reg, uint8_t *data, size_t len) {
    return i2c_master_write_read_device(I2C_MASTER_NUM, mpu6050_state.i2c_addr,
                                        &reg, 1, data, len,
                                        pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
}

static esp_err_t mpu6050_read_byte(uint8_t reg, uint8_t *data) {
    return mpu6050_read_bytes(reg, data, 1);
}

// =============================================================================
// PUBLIC FUNCTIONS
// =============================================================================

esp_err_t mpu6050_init(const mpu6050_config_t *config) {
    esp_err_t ret;
    
    if (mpu6050_state.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }
    
    // Create mutex
    mpu6050_state.mutex = xSemaphoreCreateMutex();
    if (!mpu6050_state.mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // Apply configuration
    if (config) {
        mpu6050_state.i2c_addr = config->i2c_addr;
        mpu6050_state.accel_fs = config->accel_fs;
        mpu6050_state.gyro_fs = config->gyro_fs;
    } else {
        // Default configuration
        mpu6050_state.i2c_addr = MPU6050_I2C_ADDR;
        mpu6050_state.accel_fs = MPU6050_ACCEL_FS_2G;
        mpu6050_state.gyro_fs = MPU6050_GYRO_FS_250;
    }
    
    // Initialize I2C
    ret = i2c_master_init();
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Check WHO_AM_I register
    uint8_t who_am_i;
    ret = mpu6050_read_byte(MPU6050_REG_WHO_AM_I, &who_am_i);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read WHO_AM_I register");
        return ESP_ERR_NOT_FOUND;
    }
    
    if (who_am_i != MPU6050_WHO_AM_I_VAL) {
        ESP_LOGE(TAG, "Invalid WHO_AM_I: 0x%02X (expected 0x%02X)", 
                 who_am_i, MPU6050_WHO_AM_I_VAL);
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    ESP_LOGI(TAG, "MPU-6050 detected (WHO_AM_I = 0x%02X)", who_am_i);
    
    // Reset device
    ret = mpu6050_write_byte(MPU6050_REG_PWR_MGMT_1, 0x80);  // Reset bit
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Wake up (clear sleep bit, use internal 8MHz oscillator)
    ret = mpu6050_write_byte(MPU6050_REG_PWR_MGMT_1, 0x00);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Configure sample rate (1kHz / (1 + div) = sample rate)
    uint8_t sample_rate_div = config ? config->sample_rate_div : 19;  // 50Hz
    ret = mpu6050_write_byte(MPU6050_REG_SMPLRT_DIV, sample_rate_div);
    if (ret != ESP_OK) return ret;
    
    // Configure DLPF
    mpu6050_dlpf_t dlpf = config ? config->dlpf : MPU6050_DLPF_44HZ;
    ret = mpu6050_write_byte(MPU6050_REG_CONFIG, dlpf);
    if (ret != ESP_OK) return ret;
    
    // Configure gyroscope
    ret = mpu6050_write_byte(MPU6050_REG_GYRO_CONFIG, mpu6050_state.gyro_fs << 3);
    if (ret != ESP_OK) return ret;
    
    // Configure accelerometer
    ret = mpu6050_write_byte(MPU6050_REG_ACCEL_CONFIG, mpu6050_state.accel_fs << 3);
    if (ret != ESP_OK) return ret;
    
    // Disable FIFO
    ret = mpu6050_write_byte(MPU6050_REG_FIFO_EN, 0x00);
    if (ret != ESP_OK) return ret;
    
    // Disable interrupts
    ret = mpu6050_write_byte(MPU6050_REG_INT_ENABLE, 0x00);
    if (ret != ESP_OK) return ret;
    
    mpu6050_state.initialized = true;
    
    ESP_LOGI(TAG, "MPU-6050 initialized successfully");
    ESP_LOGI(TAG, "  Accel range: ±%dg", 2 << mpu6050_state.accel_fs);
    ESP_LOGI(TAG, "  Gyro range: ±%d°/s", 250 << mpu6050_state.gyro_fs);
    ESP_LOGI(TAG, "  Sample rate: %d Hz", 1000 / (1 + sample_rate_div));
    
    return ESP_OK;
}

esp_err_t mpu6050_deinit(void) {
    if (!mpu6050_state.initialized) {
        return ESP_OK;
    }
    
    // Put device to sleep
    mpu6050_write_byte(MPU6050_REG_PWR_MGMT_1, 0x40);
    
    if (mpu6050_state.mutex) {
        vSemaphoreDelete(mpu6050_state.mutex);
        mpu6050_state.mutex = NULL;
    }
    
    mpu6050_state.initialized = false;
    ESP_LOGI(TAG, "MPU-6050 deinitialized");
    return ESP_OK;
}

bool mpu6050_is_available(void) {
    if (!mpu6050_state.initialized) {
        return false;
    }
    
    uint8_t who_am_i;
    esp_err_t ret = mpu6050_read_byte(MPU6050_REG_WHO_AM_I, &who_am_i);
    return (ret == ESP_OK && who_am_i == MPU6050_WHO_AM_I_VAL);
}

esp_err_t mpu6050_read_raw(mpu6050_raw_data_t *data) {
    if (!mpu6050_state.initialized || !data) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(mpu6050_state.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    // Read all sensor data in one burst (14 bytes)
    uint8_t buffer[14];
    esp_err_t ret = mpu6050_read_bytes(MPU6050_REG_ACCEL_XOUT_H, buffer, 14);
    
    xSemaphoreGive(mpu6050_state.mutex);
    
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Convert to 16-bit signed values (big-endian)
    data->accel_x = (int16_t)((buffer[0] << 8) | buffer[1]);
    data->accel_y = (int16_t)((buffer[2] << 8) | buffer[3]);
    data->accel_z = (int16_t)((buffer[4] << 8) | buffer[5]);
    data->temp_raw = (int16_t)((buffer[6] << 8) | buffer[7]);
    data->gyro_x = (int16_t)((buffer[8] << 8) | buffer[9]);
    data->gyro_y = (int16_t)((buffer[10] << 8) | buffer[11]);
    data->gyro_z = (int16_t)((buffer[12] << 8) | buffer[13]);
    
    return ESP_OK;
}

esp_err_t mpu6050_read(mpu6050_data_t *data) {
    if (!data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    mpu6050_raw_data_t raw;
    esp_err_t ret = mpu6050_read_raw(&raw);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Get sensitivity factors
    float accel_sens = ACCEL_SENSITIVITY[mpu6050_state.accel_fs];
    float gyro_sens = GYRO_SENSITIVITY[mpu6050_state.gyro_fs];
    
    // Convert to engineering units with offset correction
    data->accel_x_g = (raw.accel_x / accel_sens) - mpu6050_state.accel_offset_x;
    data->accel_y_g = (raw.accel_y / accel_sens) - mpu6050_state.accel_offset_y;
    data->accel_z_g = (raw.accel_z / accel_sens) - mpu6050_state.accel_offset_z;
    
    data->gyro_x_dps = (raw.gyro_x / gyro_sens) - mpu6050_state.gyro_offset_x;
    data->gyro_y_dps = (raw.gyro_y / gyro_sens) - mpu6050_state.gyro_offset_y;
    data->gyro_z_dps = (raw.gyro_z / gyro_sens) - mpu6050_state.gyro_offset_z;
    
    // Temperature: Temp_degC = (TEMP_OUT / 340) + 36.53
    data->temp_c = (raw.temp_raw / 340.0f) + 36.53f;
    
    data->timestamp = esp_timer_get_time();
    
    return ESP_OK;
}

esp_err_t mpu6050_get_motion(mpu6050_motion_t *motion) {
    if (!motion) {
        return ESP_ERR_INVALID_ARG;
    }
    
    mpu6050_data_t data;
    esp_err_t ret = mpu6050_read(&data);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Calculate total acceleration magnitude
    motion->acceleration_mag = sqrtf(data.accel_x_g * data.accel_x_g +
                                     data.accel_y_g * data.accel_y_g +
                                     data.accel_z_g * data.accel_z_g);
    
    // Motion detection (deviation from 1g at rest)
    float deviation = fabsf(motion->acceleration_mag - 1.0f);
    motion->is_moving = deviation > MPU6050_MOVING_THRESHOLD_G;
    
    // Impact detection
    motion->impact_detected = motion->acceleration_mag > MPU6050_IMPACT_THRESHOLD_G;
    motion->impact_g = motion->impact_detected ? motion->acceleration_mag : 0.0f;
    
    // Tilt angle (angle from vertical)
    motion->tilt_angle = acosf(data.accel_z_g / motion->acceleration_mag) * 180.0f / M_PI;
    motion->rollover_risk = motion->tilt_angle > MPU6050_TILT_WARNING_DEG;
    
    return ESP_OK;
}

esp_err_t mpu6050_calibrate(uint16_t samples) {
    if (!mpu6050_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (samples == 0) samples = 100;
    
    ESP_LOGI(TAG, "Starting calibration (%d samples)...", samples);
    ESP_LOGW(TAG, "Keep sensor flat and stationary!");
    
    float accel_sum_x = 0, accel_sum_y = 0, accel_sum_z = 0;
    float gyro_sum_x = 0, gyro_sum_y = 0, gyro_sum_z = 0;
    
    // Reset offsets
    mpu6050_state.accel_offset_x = 0;
    mpu6050_state.accel_offset_y = 0;
    mpu6050_state.accel_offset_z = 0;
    mpu6050_state.gyro_offset_x = 0;
    mpu6050_state.gyro_offset_y = 0;
    mpu6050_state.gyro_offset_z = 0;
    
    vTaskDelay(pdMS_TO_TICKS(500));  // Let sensor stabilize
    
    for (int i = 0; i < samples; i++) {
        mpu6050_data_t data;
        if (mpu6050_read(&data) == ESP_OK) {
            accel_sum_x += data.accel_x_g;
            accel_sum_y += data.accel_y_g;
            accel_sum_z += data.accel_z_g;
            gyro_sum_x += data.gyro_x_dps;
            gyro_sum_y += data.gyro_y_dps;
            gyro_sum_z += data.gyro_z_dps;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // Calculate offsets (accel Z should be 1g when flat)
    mpu6050_state.accel_offset_x = accel_sum_x / samples;
    mpu6050_state.accel_offset_y = accel_sum_y / samples;
    mpu6050_state.accel_offset_z = (accel_sum_z / samples) - 1.0f;  // Expect 1g
    mpu6050_state.gyro_offset_x = gyro_sum_x / samples;
    mpu6050_state.gyro_offset_y = gyro_sum_y / samples;
    mpu6050_state.gyro_offset_z = gyro_sum_z / samples;
    
    ESP_LOGI(TAG, "Calibration complete:");
    ESP_LOGI(TAG, "  Accel offset: X=%.3f, Y=%.3f, Z=%.3f g",
             mpu6050_state.accel_offset_x, mpu6050_state.accel_offset_y, 
             mpu6050_state.accel_offset_z);
    ESP_LOGI(TAG, "  Gyro offset: X=%.2f, Y=%.2f, Z=%.2f °/s",
             mpu6050_state.gyro_offset_x, mpu6050_state.gyro_offset_y,
             mpu6050_state.gyro_offset_z);
    
    return ESP_OK;
}

esp_err_t mpu6050_set_accel_range(mpu6050_accel_fs_t fs) {
    if (!mpu6050_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = mpu6050_write_byte(MPU6050_REG_ACCEL_CONFIG, fs << 3);
    if (ret == ESP_OK) {
        mpu6050_state.accel_fs = fs;
        ESP_LOGI(TAG, "Accel range set to ±%dg", 2 << fs);
    }
    return ret;
}

esp_err_t mpu6050_set_gyro_range(mpu6050_gyro_fs_t fs) {
    if (!mpu6050_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = mpu6050_write_byte(MPU6050_REG_GYRO_CONFIG, fs << 3);
    if (ret == ESP_OK) {
        mpu6050_state.gyro_fs = fs;
        ESP_LOGI(TAG, "Gyro range set to ±%d°/s", 250 << fs);
    }
    return ret;
}

esp_err_t mpu6050_sleep(void) {
    if (!mpu6050_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return mpu6050_write_byte(MPU6050_REG_PWR_MGMT_1, 0x40);  // Sleep bit
}

esp_err_t mpu6050_wake(void) {
    if (!mpu6050_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return mpu6050_write_byte(MPU6050_REG_PWR_MGMT_1, 0x00);  // Clear sleep bit
}

esp_err_t mpu6050_get_temperature(float *temp_c) {
    if (!temp_c) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t buffer[2];
    esp_err_t ret = mpu6050_read_bytes(MPU6050_REG_TEMP_OUT_H, buffer, 2);
    if (ret != ESP_OK) {
        return ret;
    }
    
    int16_t temp_raw = (int16_t)((buffer[0] << 8) | buffer[1]);
    *temp_c = (temp_raw / 340.0f) + 36.53f;
    
    return ESP_OK;
}
