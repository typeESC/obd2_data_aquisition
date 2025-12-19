/**
 * @file storage_manager.c
 * @brief Storage abstraction layer implementation
 * @date 2025
 * 
 * Currently implements SPIFFS backend. Ready for SD card extension.
 */

#include "storage_manager.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fnmatch.h>

static const char *TAG = "STORAGE";

// === INTERNAL STATE ===
static storage_backend_t current_backend = STORAGE_BACKEND_SPIFFS;
static bool storage_mounted = false;
static const char *base_path = "/spiffs";

// === SPIFFS IMPLEMENTATION ===

esp_err_t storage_manager_init(storage_backend_t backend) {
    if (storage_mounted) {
        ESP_LOGW(TAG, "Storage already mounted");
        return ESP_OK;
    }
    
    current_backend = backend;
    
    switch (backend) {
        case STORAGE_BACKEND_SPIFFS: {
            esp_vfs_spiffs_conf_t conf = {
                .base_path = "/spiffs",
                .partition_label = NULL,
                .max_files = 10,
                .format_if_mount_failed = true
            };
            
            esp_err_t ret = esp_vfs_spiffs_register(&conf);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
                return ret;
            }
            
            base_path = "/spiffs";
            storage_mounted = true;
            ESP_LOGI(TAG, "SPIFFS mounted at %s", base_path);
            break;
        }
        
        case STORAGE_BACKEND_SD_SPI:
        case STORAGE_BACKEND_SD_SDMMC:
            // TODO: Implement SD card support
            ESP_LOGE(TAG, "SD card backend not yet implemented");
            return ESP_ERR_NOT_SUPPORTED;
            
        default:
            return ESP_ERR_INVALID_ARG;
    }
    
    // Log storage info
    storage_stats_t stats;
    if (storage_manager_get_stats(&stats) == ESP_OK) {
        ESP_LOGI(TAG, "Storage: %.1f KB total, %.1f KB used (%.1f%%)",
                 stats.total_bytes / 1024.0f,
                 stats.used_bytes / 1024.0f,
                 stats.percent_used);
    }
    
    return ESP_OK;
}

esp_err_t storage_manager_deinit(void) {
    if (!storage_mounted) {
        return ESP_OK;
    }
    
    switch (current_backend) {
        case STORAGE_BACKEND_SPIFFS:
            esp_vfs_spiffs_unregister(NULL);
            break;
            
        case STORAGE_BACKEND_SD_SPI:
        case STORAGE_BACKEND_SD_SDMMC:
            // TODO: Unmount SD card
            break;
            
        default:
            break;
    }
    
    storage_mounted = false;
    ESP_LOGI(TAG, "Storage unmounted");
    return ESP_OK;
}

esp_err_t storage_manager_get_stats(storage_stats_t *stats) {
    if (!stats) return ESP_ERR_INVALID_ARG;
    if (!storage_mounted) return ESP_ERR_INVALID_STATE;
    
    memset(stats, 0, sizeof(storage_stats_t));
    stats->backend = current_backend;
    stats->mounted = storage_mounted;
    
    switch (current_backend) {
        case STORAGE_BACKEND_SPIFFS: {
            size_t total = 0, used = 0;
            esp_err_t ret = esp_spiffs_info(NULL, &total, &used);
            if (ret != ESP_OK) return ret;
            
            stats->total_bytes = total;
            stats->used_bytes = used;
            stats->free_bytes = total - used;
            stats->percent_used = (used * 100.0f) / total;
            break;
        }
        
        case STORAGE_BACKEND_SD_SPI:
        case STORAGE_BACKEND_SD_SDMMC:
            // TODO: Get SD card info
            break;
            
        default:
            break;
    }
    
    // Count files
    DIR *dir = opendir(base_path);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            stats->file_count++;
        }
        closedir(dir);
    }
    
    return ESP_OK;
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
        case STORAGE_BACKEND_SD_SDMMC:
            // TODO: Format SD card
            return ESP_ERR_NOT_SUPPORTED;
            
        default:
            return ESP_ERR_INVALID_ARG;
    }
}
