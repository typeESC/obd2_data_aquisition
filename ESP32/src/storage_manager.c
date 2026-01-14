/**
 * @file storage_manager.c
 * @brief Storage abstraction layer implementation with SD Card support
 * @date 2025
 * 
 * Implements dual storage backend:
 * - Primary: SD Card (SPI mode) for large capacity
 * - Fallback: SPIFFS for reliability
 * 
 * Auto-detection: tries SD first, falls back to SPIFFS if SD unavailable.
 */

#include "storage_manager.h"
#include "obd_config.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fnmatch.h>

static const char *TAG = "STORAGE";

// === INTERNAL STATE ===
static storage_backend_t current_backend = STORAGE_BACKEND_NONE;
static bool storage_mounted = false;
static bool sd_mounted = false;
static bool spiffs_mounted = false;
static const char *base_path = NULL;

// SD card handle
static sdmmc_card_t *sd_card = NULL;
static sdmmc_host_t sd_host = SDSPI_HOST_DEFAULT();

// ============================================================================
// SD CARD IMPLEMENTATION
// ============================================================================

#if SD_ENABLED

static esp_err_t mount_sd_card(void) {
    ESP_LOGI(TAG, "Mounting SD card (SPI mode)...");
    ESP_LOGI(TAG, "  CS=GPIO%d, MOSI=GPIO%d, MISO=GPIO%d, SCK=GPIO%d",
             SD_CS_PIN, SD_MOSI_PIN, SD_MISO_PIN, SD_SCK_PIN);
    
    // SPI bus configuration
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_MOSI_PIN,
        .miso_io_num = SD_MISO_PIN,
        .sclk_io_num = SD_SCK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    
    // Initialize SPI bus
    esp_err_t ret = spi_bus_initialize(sd_host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // SD SPI device configuration
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_CS_PIN;
    slot_config.host_id = sd_host.slot;
    
    // Mount FAT filesystem
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = SD_FORMAT_IF_FAIL,
        .max_files = SD_MAX_FILES,
        .allocation_unit_size = 16 * 1024
    };
    
    ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &sd_host, &slot_config, &mount_config, &sd_card);
    
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount SD card filesystem");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SD card: %s", esp_err_to_name(ret));
        }
        spi_bus_free(sd_host.slot);
        return ret;
    }
    
    // Print card info
    ESP_LOGI(TAG, "✓ SD card mounted at %s", SD_MOUNT_POINT);
    ESP_LOGI(TAG, "  Name: %s", sd_card->cid.name);
    ESP_LOGI(TAG, "  Capacity: %.2f GB", 
             ((uint64_t)sd_card->csd.capacity) * sd_card->csd.sector_size / (1024.0 * 1024.0 * 1024.0));
    ESP_LOGI(TAG, "  Speed: %s", (sd_card->csd.tr_speed > 25000000) ? "High Speed" : "Default Speed");
    
    sd_mounted = true;
    return ESP_OK;
}

static esp_err_t unmount_sd_card(void) {
    if (!sd_mounted) return ESP_OK;
    
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, sd_card);
    if (ret == ESP_OK) {
        spi_bus_free(sd_host.slot);
        sd_card = NULL;
        sd_mounted = false;
        ESP_LOGI(TAG, "SD card unmounted");
    }
    return ret;
}

static esp_err_t get_sd_stats(storage_stats_t *stats) {
    if (!sd_mounted || !sd_card) return ESP_ERR_INVALID_STATE;
    
    // Use FATFS to get free space
    FATFS *fs;
    DWORD fre_clust;
    
    if (f_getfree("0:", &fre_clust, &fs) != FR_OK) {
        return ESP_FAIL;
    }
    
    uint64_t total_bytes = ((uint64_t)fs->n_fatent - 2) * fs->csize * 512;
    uint64_t free_bytes = (uint64_t)fre_clust * fs->csize * 512;
    
    stats->total_bytes = (size_t)(total_bytes > SIZE_MAX ? SIZE_MAX : total_bytes);
    stats->free_bytes = (size_t)(free_bytes > SIZE_MAX ? SIZE_MAX : free_bytes);
    stats->used_bytes = stats->total_bytes - stats->free_bytes;
    stats->percent_used = (stats->used_bytes * 100.0f) / stats->total_bytes;
    stats->sd_available = true;
    
    return ESP_OK;
}

#else // SD_ENABLED = false

static esp_err_t mount_sd_card(void) {
    ESP_LOGW(TAG, "SD card support disabled in config");
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t unmount_sd_card(void) {
    return ESP_OK;
}

static esp_err_t get_sd_stats(storage_stats_t *stats) {
    (void)stats;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif // SD_ENABLED

// ============================================================================
// SPIFFS IMPLEMENTATION
// ============================================================================

static esp_err_t mount_spiffs(void) {
    ESP_LOGI(TAG, "Mounting SPIFFS...");
    
    esp_vfs_spiffs_conf_t conf = {
        .base_path = SPIFFS_MOUNT_POINT,
        .partition_label = NULL,
        .max_files = SPIFFS_MAX_FILES,
        .format_if_mount_failed = true
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    size_t total = 0, used = 0;
    esp_spiffs_info(NULL, &total, &used);
    
    ESP_LOGI(TAG, "✓ SPIFFS mounted at %s", SPIFFS_MOUNT_POINT);
    ESP_LOGI(TAG, "  Capacity: %.1f KB total, %.1f KB used (%.1f%%)",
             total / 1024.0f, used / 1024.0f, (used * 100.0f) / total);
    
    spiffs_mounted = true;
    return ESP_OK;
}

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

esp_err_t storage_manager_init_auto(void) {
    if (storage_mounted) {
        ESP_LOGW(TAG, "Storage already mounted");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "=== Storage Manager Init (Auto-detect) ===");
    
    esp_err_t ret = ESP_FAIL;
    
    // Try SD card first (if enabled and preferred)
    #if SD_ENABLED && STORAGE_PREFER_SD
    ret = mount_sd_card();
    if (ret == ESP_OK) {
        current_backend = STORAGE_BACKEND_SD_SPI;
        base_path = SD_MOUNT_POINT;
        storage_mounted = true;
        ESP_LOGI(TAG, "✓ Primary storage: SD Card");
        
        // Also mount SPIFFS as backup (for config files etc)
        if (mount_spiffs() == ESP_OK) {
            ESP_LOGI(TAG, "✓ Backup storage: SPIFFS");
        }
        
        return ESP_OK;
    }
    ESP_LOGW(TAG, "SD card not available, falling back to SPIFFS");
    #endif
    
    // Fallback to SPIFFS
    ret = mount_spiffs();
    if (ret == ESP_OK) {
        current_backend = STORAGE_BACKEND_SPIFFS;
        base_path = SPIFFS_MOUNT_POINT;
        storage_mounted = true;
        ESP_LOGI(TAG, "✓ Primary storage: SPIFFS (limited capacity)");
        return ESP_OK;
    }
    
    ESP_LOGE(TAG, "✗ No storage available!");
    current_backend = STORAGE_BACKEND_NONE;
    return ESP_FAIL;
}

esp_err_t storage_manager_init(storage_backend_t backend) {
    if (storage_mounted) {
        ESP_LOGW(TAG, "Storage already mounted");
        return ESP_OK;
    }
    
    current_backend = backend;
    esp_err_t ret = ESP_FAIL;
    
    switch (backend) {
        case STORAGE_BACKEND_SPIFFS:
            ret = mount_spiffs();
            if (ret == ESP_OK) {
                base_path = SPIFFS_MOUNT_POINT;
                storage_mounted = true;
            }
            break;
        
        case STORAGE_BACKEND_SD_SPI:
            ret = mount_sd_card();
            if (ret == ESP_OK) {
                base_path = SD_MOUNT_POINT;
                storage_mounted = true;
            }
            break;
            
        case STORAGE_BACKEND_SD_SDMMC:
            ESP_LOGE(TAG, "SDMMC backend not supported on this hardware");
            return ESP_ERR_NOT_SUPPORTED;
            
        default:
            return ESP_ERR_INVALID_ARG;
    }
    
    if (ret == ESP_OK) {
        // Log storage info
        storage_stats_t stats;
        if (storage_manager_get_stats(&stats) == ESP_OK) {
            ESP_LOGI(TAG, "Storage ready: %.1f MB total, %.1f MB free (%.1f%% used)",
                     stats.total_bytes / (1024.0f * 1024.0f),
                     stats.free_bytes / (1024.0f * 1024.0f),
                     stats.percent_used);
        }
    }
    
    return ret;
}

esp_err_t storage_manager_deinit(void) {
    if (!storage_mounted) {
        return ESP_OK;
    }
    
    switch (current_backend) {
        case STORAGE_BACKEND_SPIFFS:
            esp_vfs_spiffs_unregister(NULL);
            spiffs_mounted = false;
            break;
            
        case STORAGE_BACKEND_SD_SPI:
            unmount_sd_card();
            // Also unmount SPIFFS if it was mounted as backup
            if (spiffs_mounted) {
                esp_vfs_spiffs_unregister(NULL);
                spiffs_mounted = false;
            }
            break;
            
        default:
            break;
    }
    
    storage_mounted = false;
    current_backend = STORAGE_BACKEND_NONE;
    base_path = NULL;
    ESP_LOGI(TAG, "Storage unmounted");
    return ESP_OK;
}

esp_err_t storage_manager_get_stats(storage_stats_t *stats) {
    if (!stats) return ESP_ERR_INVALID_ARG;
    if (!storage_mounted) return ESP_ERR_INVALID_STATE;
    
    memset(stats, 0, sizeof(storage_stats_t));
    stats->backend = current_backend;
    stats->mounted = storage_mounted;
    stats->sd_available = sd_mounted;
    stats->spiffs_available = spiffs_mounted;
    
    esp_err_t ret = ESP_FAIL;
    
    switch (current_backend) {
        case STORAGE_BACKEND_SPIFFS: {
            size_t total = 0, used = 0;
            ret = esp_spiffs_info(NULL, &total, &used);
            if (ret == ESP_OK) {
                stats->total_bytes = total;
                stats->used_bytes = used;
                stats->free_bytes = total - used;
                stats->percent_used = (used * 100.0f) / total;
            }
            break;
        }
        
        case STORAGE_BACKEND_SD_SPI:
            ret = get_sd_stats(stats);
            break;
            
        default:
            break;
    }
    
    // Count files
    if (base_path) {
        DIR *dir = opendir(base_path);
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                stats->file_count++;
            }
            closedir(dir);
        }
    }
    
    return ret;
}

bool storage_manager_is_available(void) {
    if (!storage_mounted) return false;
    
    storage_stats_t stats;
    if (storage_manager_get_stats(&stats) != ESP_OK) return false;
    
    return stats.percent_used < STORAGE_CRITICAL_THRESHOLD;
}

bool storage_manager_is_low(void) {
    storage_stats_t stats;
    if (storage_manager_get_stats(&stats) != ESP_OK) return true;
    
    return stats.percent_used >= STORAGE_WARNING_THRESHOLD;
}

bool storage_manager_is_critical(void) {
    storage_stats_t stats;
    if (storage_manager_get_stats(&stats) != ESP_OK) return true;
    
    return stats.percent_used >= STORAGE_CRITICAL_THRESHOLD;
}

bool storage_manager_is_sd_active(void) {
    return current_backend == STORAGE_BACKEND_SD_SPI && sd_mounted;
}

storage_backend_t storage_manager_get_active_backend(void) {
    return current_backend;
}

int storage_manager_cleanup(uint8_t target_free_percent) {
    if (!storage_mounted) return 0;
    
    int deleted = 0;
    storage_stats_t stats;
    
    while (storage_manager_get_stats(&stats) == ESP_OK) {
        float current_free = 100.0f - stats.percent_used;
        if (current_free >= target_free_percent) {
            break;  // Enough free space
        }
        
        // Find oldest file
        DIR *dir = opendir(base_path);
        if (!dir) break;
        
        char oldest_file[320] = {0};
        time_t oldest_time = 0;
        
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            char filepath[320];
            snprintf(filepath, sizeof(filepath), "%s/%s", base_path, entry->d_name);
            
            struct stat st;
            if (stat(filepath, &st) == 0 && S_ISREG(st.st_mode)) {
                if (oldest_time == 0 || st.st_mtime < oldest_time) {
                    oldest_time = st.st_mtime;
                    strncpy(oldest_file, filepath, sizeof(oldest_file) - 1);
                }
            }
        }
        closedir(dir);
        
        // Delete oldest file
        if (strlen(oldest_file) > 0) {
            if (remove(oldest_file) == 0) {
                deleted++;
                ESP_LOGI(TAG, "Cleanup: deleted %s", oldest_file);
            } else {
                break;  // Failed to delete
            }
        } else {
            break;  // No files to delete
        }
    }
    
    if (deleted > 0) {
        ESP_LOGI(TAG, "Cleanup complete: %d files deleted", deleted);
    }
    
    return deleted;
}

const char* storage_manager_get_base_path(void) {
    return base_path;
}

int storage_manager_list_files(const char *pattern, file_callback_t callback, void *user_data) {
    if (!storage_mounted || !callback) return 0;
    
    int count = 0;
    DIR *dir = opendir(base_path);
    if (!dir) return 0;
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Check pattern match
        if (pattern && fnmatch(pattern, entry->d_name, 0) != 0) {
            continue;
        }
        
        char filepath[320];
        snprintf(filepath, sizeof(filepath), "%s/%s", base_path, entry->d_name);
        
        struct stat st;
        if (stat(filepath, &st) == 0 && S_ISREG(st.st_mode)) {
            callback(filepath, st.st_size, user_data);
            count++;
        }
    }
    
    closedir(dir);
    return count;
}

size_t storage_manager_get_free(void) {
    storage_stats_t stats;
    if (storage_manager_get_stats(&stats) != ESP_OK) return 0;
    
    return stats.free_bytes;
}

esp_err_t storage_manager_format(void) {
    if (!storage_mounted) return ESP_ERR_INVALID_STATE;
    
    ESP_LOGW(TAG, "Formatting storage - all data will be lost!");
    
    switch (current_backend) {
        case STORAGE_BACKEND_SPIFFS:
            return esp_spiffs_format(NULL);
            
        case STORAGE_BACKEND_SD_SPI:
            // Format SD requires unmount/remount - not recommended
            ESP_LOGW(TAG, "SD card formatting not supported (use PC to format as FAT32)");
            return ESP_ERR_NOT_SUPPORTED;
            
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

// === HELPER FUNCTION TO CHECK IF SD IS AVAILABLE ===
bool storage_manager_sd_available(void) {
    #if SD_ENABLED
    return sd_mounted;
    #else
    return false;
    #endif
}
