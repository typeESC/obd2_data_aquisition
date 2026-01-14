/**
 * @file storage_manager.h
 * @brief Storage abstraction layer for SD Card and SPIFFS
 * @date 2025
 * 
 * Provides a unified interface for storage operations with automatic fallback:
 * - Primary: SD Card (SPI mode) - large capacity for long trips
 * - Fallback: SPIFFS - limited but always available (~2MB)
 * 
 * Hardware v2.0:
 * - SD Card: SPI mode on VSPI (GPIO 5/18/19/23)
 * - Auto-detection: tries SD first, falls back to SPIFFS
 */

#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// === STORAGE BACKEND TYPE ===
typedef enum {
    STORAGE_BACKEND_NONE = 0,   // No storage available
    STORAGE_BACKEND_SPIFFS,     // Internal SPIFFS flash (~2MB)
    STORAGE_BACKEND_SD_SPI,     // SD card via SPI (recommended for long trips)
    STORAGE_BACKEND_SD_SDMMC    // SD card via SDMMC (not on all ESP32 variants)
} storage_backend_t;

// === STORAGE STATISTICS ===
typedef struct {
    size_t total_bytes;         // Total storage capacity
    size_t used_bytes;          // Used storage
    size_t free_bytes;          // Free storage
    float percent_used;         // Usage percentage
    uint32_t file_count;        // Number of files
    storage_backend_t backend;  // Current backend type
    bool mounted;               // Is storage mounted
    bool sd_available;          // SD card detected
    bool spiffs_available;      // SPIFFS mounted (fallback)
} storage_stats_t;

// === STORAGE THRESHOLDS ===
#define STORAGE_WARNING_THRESHOLD   80  // Percentage - trigger warning
#define STORAGE_CRITICAL_THRESHOLD  95  // Percentage - stop logging
#define STORAGE_CLEANUP_THRESHOLD   90  // Percentage - trigger auto-cleanup

// === PUBLIC FUNCTIONS ===

/**
 * @brief Initialize storage manager with automatic backend selection
 * 
 * Tries SD card first (if enabled in obd_config.h), falls back to SPIFFS.
 * This is the recommended initialization method for auto-detection.
 * 
 * @return ESP_OK on success, ESP_FAIL if no storage available
 */
esp_err_t storage_manager_init_auto(void);

/**
 * @brief Initialize storage manager with specific backend
 * @param backend Backend type to use
 * @return ESP_OK on success
 */
esp_err_t storage_manager_init(storage_backend_t backend);

/**
 * @brief Deinitialize and unmount storage
 * @return ESP_OK on success
 */
esp_err_t storage_manager_deinit(void);

/**
 * @brief Get storage statistics
 * @param stats Pointer to stats structure to fill
 * @return ESP_OK on success
 */
esp_err_t storage_manager_get_stats(storage_stats_t *stats);

/**
 * @brief Check if storage is available and has free space
 * @return true if storage is ready for writing
 */
bool storage_manager_is_available(void);

/**
 * @brief Check if storage usage is above warning threshold
 * @return true if storage is low
 */
bool storage_manager_is_low(void);

/**
 * @brief Check if storage usage is above critical threshold
 * @return true if storage is critically low
 */
bool storage_manager_is_critical(void);

/**
 * @brief Delete oldest log files to free space
 * @param target_free_percent Target free space percentage
 * @return Number of files deleted
 */
int storage_manager_cleanup(uint8_t target_free_percent);

/**
 * @brief Get base path for storage
 * @return Base path string (e.g., "/spiffs" or "/sdcard")
 */
const char* storage_manager_get_base_path(void);

/**
 * @brief List all files matching a pattern
 * @param pattern Filename pattern (e.g., "session_*.csv")
 * @param callback Function called for each matching file
 * @param user_data User data passed to callback
 * @return Number of files found
 */
typedef void (*file_callback_t)(const char *filepath, size_t size, void *user_data);
int storage_manager_list_files(const char *pattern, file_callback_t callback, void *user_data);

/**
 * @brief Get free space in bytes
 * @return Free space in bytes
 */
size_t storage_manager_get_free(void);

/**
 * @brief Format storage (DANGEROUS - deletes all data)
 * @return ESP_OK on success
 */
esp_err_t storage_manager_format(void);

/**
 * @brief Check if SD card is the active backend
 * @return true if SD card is mounted and active
 */
bool storage_manager_is_sd_active(void);

/**
 * @brief Check if SD card is available (even if not primary)
 * @return true if SD card is mounted
 */
bool storage_manager_sd_available(void);

/**
 * @brief Get active storage backend type
 * @return Current storage_backend_t
 */
storage_backend_t storage_manager_get_active_backend(void);

#endif // STORAGE_MANAGER_H
