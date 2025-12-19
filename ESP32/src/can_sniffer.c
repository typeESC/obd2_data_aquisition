/**
 * @file can_sniffer.c
 * @brief CAN bus sniffer implementation for raw message capture
 * @date 2025
 * 
 * Captures raw CAN messages and logs them in GVRET CSV format for 
 * SavvyCAN compatibility. Uses circular buffer for high-speed capture.
 */

#include "can_sniffer.h"
#include "obd_config.h"
#include "driver/twai.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

static const char *TAG = "CAN_SNIFF";

// === INTERNAL STATE ===
static sniff_state_t sniffer_state = SNIFF_STATE_IDLE;
static sniff_filter_t current_filter = {
    .id_min = 0x000,
    .id_max = 0x7FF,
    .exclude_obd_requests = true,    // Exclude our own requests
    .exclude_obd_responses = false   // Keep ECU responses for correlation
};
static sniff_stats_t sniffer_stats = {0};

// === CIRCULAR BUFFER ===
static can_raw_message_t message_buffer[SNIFF_BUFFER_SIZE];
static volatile uint32_t buffer_head = 0;
static volatile uint32_t buffer_tail = 0;
static volatile uint32_t buffer_count = 0;
static SemaphoreHandle_t buffer_mutex = NULL;

// === FILE HANDLING ===
static FILE *log_file = NULL;
static char current_log_path[64] = {0};
static bool file_header_written = false;

// === UNIQUE ID TRACKING ===
#define MAX_UNIQUE_IDS 256
static uint16_t unique_ids[MAX_UNIQUE_IDS];
static uint32_t unique_id_count = 0;

// GVRET CSV Header
static const char *GVRET_HEADER = "Time Stamp,ID,Extended,Dir,Bus,LEN,D1,D2,D3,D4,D5,D6,D7,D8\n";

// === STATE NAME STRINGS ===
static const char *STATE_NAMES[] = {
    [SNIFF_STATE_IDLE]         = "IDLE",
    [SNIFF_STATE_ACTIVE]       = "ACTIVE",
    [SNIFF_STATE_PAUSED]       = "PAUSED",
    [SNIFF_STATE_STORAGE_LOW]  = "STORAGE_LOW",
    [SNIFF_STATE_STORAGE_FULL] = "STORAGE_FULL",
    [SNIFF_STATE_ERROR]        = "ERROR"
};

// ============================================================================
// INTERNAL HELPER FUNCTIONS
// ============================================================================

/**
 * @brief Check if CAN ID passes the current filter
 */
static bool filter_accepts_id(uint32_t id) {
    // Check range
    if (id < current_filter.id_min || id > current_filter.id_max) {
        return false;
    }
    
    // Check OBD request exclusion
    if (current_filter.exclude_obd_requests && id == 0x7DF) {
        return false;
    }
    
    // Check OBD response exclusion
    if (current_filter.exclude_obd_responses && (id >= 0x7E8 && id <= 0x7EF)) {
        return false;
    }
    
    return true;
}

/**
 * @brief Track unique CAN IDs seen
 */
static void track_unique_id(uint32_t id) {
    // Check if already tracked
    for (uint32_t i = 0; i < unique_id_count; i++) {
        if (unique_ids[i] == (uint16_t)id) {
            return;  // Already tracked
        }
    }
    
    // Add new ID if space available
    if (unique_id_count < MAX_UNIQUE_IDS) {
        unique_ids[unique_id_count++] = (uint16_t)id;
    }
}

/**
 * @brief Update storage statistics
 */
static void update_storage_stats(void) {
    size_t total = 0, used = 0;
    esp_spiffs_info(NULL, &total, &used);
    
    sniffer_stats.storage_free = total - used;
    sniffer_stats.storage_percent_used = (used * 100.0f) / total;
}

/**
 * @brief Create new log file with GVRET header
 */
static esp_err_t create_log_file(void) {
    // Generate filename with timestamp
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    snprintf(current_log_path, sizeof(current_log_path),
             "%s/%s%04d%02d%02d_%02d%02d%02d.csv",
             SNIFF_LOG_DIR, SNIFF_FILE_PREFIX,
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    
    log_file = fopen(current_log_path, "w");
    if (!log_file) {
        ESP_LOGE(TAG, "Failed to create log file: %s", current_log_path);
        return ESP_FAIL;
    }
    
    // Write GVRET header
    fprintf(log_file, "%s", GVRET_HEADER);
    fflush(log_file);
    file_header_written = true;
    
    ESP_LOGI(TAG, "Created log file: %s", current_log_path);
    return ESP_OK;
}

/**
 * @brief Write a message to log file in GVRET format
 */
static esp_err_t write_message_to_file(const can_raw_message_t *msg) {
    if (!log_file) return ESP_FAIL;
    
    // GVRET format: Time Stamp,ID,Extended,Dir,Bus,LEN,D1,D2,D3,D4,D5,D6,D7,D8
    int written = fprintf(log_file, "%llu,%08lX,%s,%s,%d,%d",
                          msg->timestamp_us,
                          msg->identifier,
                          msg->extended ? "true" : "false",
                          msg->is_tx ? "Tx" : "Rx",
                          msg->bus,
                          msg->dlc);
    
    // Write all 8 data bytes (pad with 00 if needed)
    for (int i = 0; i < 8; i++) {
        written += fprintf(log_file, ",%02X", i < msg->dlc ? msg->data[i] : 0);
    }
    fprintf(log_file, "\n");
    
    sniffer_stats.bytes_written += written + 1;  // +1 for newline
    sniffer_stats.messages_logged++;
    
    return ESP_OK;
}

/**
 * @brief Flush buffer contents to file
 */
static esp_err_t flush_buffer_to_file(void) {
    if (!log_file || buffer_count == 0) return ESP_OK;
    
    if (xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    uint32_t messages_to_write = buffer_count;
    uint32_t tail = buffer_tail;
    
    for (uint32_t i = 0; i < messages_to_write; i++) {
        write_message_to_file(&message_buffer[tail]);
        tail = (tail + 1) % SNIFF_BUFFER_SIZE;
    }
    
    buffer_tail = tail;
    buffer_count -= messages_to_write;
    
    xSemaphoreGive(buffer_mutex);
    
    // Flush to disk
    fflush(log_file);
    
    return ESP_OK;
}

// ============================================================================
// PUBLIC API IMPLEMENTATION
// ============================================================================

esp_err_t can_sniffer_init(void) {
    ESP_LOGI(TAG, "Initializing CAN sniffer...");
    
    // Create buffer mutex
    buffer_mutex = xSemaphoreCreateMutex();
    if (!buffer_mutex) {
        ESP_LOGE(TAG, "Failed to create buffer mutex");
        return ESP_FAIL;
    }
    
    // Reset state
    sniffer_state = SNIFF_STATE_IDLE;
    memset(&sniffer_stats, 0, sizeof(sniffer_stats));
    memset(unique_ids, 0, sizeof(unique_ids));
    unique_id_count = 0;
    buffer_head = 0;
    buffer_tail = 0;
    buffer_count = 0;
    
    update_storage_stats();
    
    ESP_LOGI(TAG, "CAN sniffer initialized (buffer: %d msgs, storage: %.1f KB free)",
             SNIFF_BUFFER_SIZE, sniffer_stats.storage_free / 1024.0f);
    
    return ESP_OK;
}

esp_err_t can_sniffer_deinit(void) {
    can_sniffer_stop();
    
    if (buffer_mutex) {
        vSemaphoreDelete(buffer_mutex);
        buffer_mutex = NULL;
    }
    
    sniffer_state = SNIFF_STATE_IDLE;
    ESP_LOGI(TAG, "CAN sniffer deinitialized");
    
    return ESP_OK;
}

esp_err_t can_sniffer_start(const sniff_filter_t *filter) {
    if (sniffer_state == SNIFF_STATE_ACTIVE) {
        ESP_LOGW(TAG, "Sniffer already active");
        return ESP_OK;
    }
    
    // Check storage
    update_storage_stats();
    if (sniffer_stats.storage_percent_used >= 95.0f) {
        ESP_LOGE(TAG, "Storage full - cannot start sniffer");
        sniffer_state = SNIFF_STATE_STORAGE_FULL;
        return ESP_ERR_NO_MEM;
    }
    
    // Apply filter if provided
    if (filter) {
        memcpy(&current_filter, filter, sizeof(sniff_filter_t));
    }
    
    // Create log file
    if (create_log_file() != ESP_OK) {
        sniffer_state = SNIFF_STATE_ERROR;
        return ESP_FAIL;
    }
    
    // Reset stats for new session
    sniffer_stats.messages_captured = 0;
    sniffer_stats.messages_logged = 0;
    sniffer_stats.buffer_overflows = 0;
    sniffer_stats.bytes_written = strlen(GVRET_HEADER);
    sniffer_stats.start_timestamp = esp_timer_get_time();
    unique_id_count = 0;
    
    // Reset buffer
    buffer_head = 0;
    buffer_tail = 0;
    buffer_count = 0;
    
    sniffer_state = SNIFF_STATE_ACTIVE;
    
    ESP_LOGI(TAG, "╔══════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  CAN SNIFFER STARTED                 ║");
    ESP_LOGI(TAG, "╠══════════════════════════════════════╣");
    ESP_LOGI(TAG, "║  Filter: 0x%03lX - 0x%03lX              ║", 
             current_filter.id_min, current_filter.id_max);
    ESP_LOGI(TAG, "║  Exclude OBD req: %s               ║",
             current_filter.exclude_obd_requests ? "YES" : "NO ");
    ESP_LOGI(TAG, "║  File: %s  ║", current_log_path + 8);  // Skip /spiffs/
    ESP_LOGI(TAG, "╚══════════════════════════════════════╝");
    
    return ESP_OK;
}

esp_err_t can_sniffer_stop(void) {
    if (sniffer_state == SNIFF_STATE_IDLE) {
        return ESP_OK;
    }
    
    // Flush remaining buffer
    flush_buffer_to_file();
    
    // Close file
    if (log_file) {
        fflush(log_file);
        fclose(log_file);
        log_file = NULL;
        file_header_written = false;
    }
    
    sniffer_stats.unique_ids = unique_id_count;
    
    ESP_LOGI(TAG, "╔══════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  CAN SNIFFER STOPPED                 ║");
    ESP_LOGI(TAG, "╠══════════════════════════════════════╣");
    ESP_LOGI(TAG, "║  Messages captured: %8lu         ║", sniffer_stats.messages_captured);
    ESP_LOGI(TAG, "║  Messages logged:   %8lu         ║", sniffer_stats.messages_logged);
    ESP_LOGI(TAG, "║  Unique CAN IDs:    %8lu         ║", sniffer_stats.unique_ids);
    ESP_LOGI(TAG, "║  Bytes written:     %8u         ║", (unsigned)sniffer_stats.bytes_written);
    ESP_LOGI(TAG, "║  Buffer overflows:  %8lu         ║", sniffer_stats.buffer_overflows);
    ESP_LOGI(TAG, "╚══════════════════════════════════════╝");
    
    sniffer_state = SNIFF_STATE_IDLE;
    return ESP_OK;
}

esp_err_t can_sniffer_pause(void) {
    if (sniffer_state != SNIFF_STATE_ACTIVE && 
        sniffer_state != SNIFF_STATE_STORAGE_LOW) {
        return ESP_ERR_INVALID_STATE;
    }
    
    flush_buffer_to_file();
    sniffer_state = SNIFF_STATE_PAUSED;
    ESP_LOGI(TAG, "Sniffer paused");
    
    return ESP_OK;
}

esp_err_t can_sniffer_resume(void) {
    if (sniffer_state != SNIFF_STATE_PAUSED) {
        return ESP_ERR_INVALID_STATE;
    }
    
    sniffer_state = SNIFF_STATE_ACTIVE;
    ESP_LOGI(TAG, "Sniffer resumed");
    
    return ESP_OK;
}

sniff_state_t can_sniffer_get_state(void) {
    return sniffer_state;
}

esp_err_t can_sniffer_get_stats(sniff_stats_t *stats) {
    if (!stats) return ESP_ERR_INVALID_ARG;
    
    update_storage_stats();
    sniffer_stats.unique_ids = unique_id_count;
    
    memcpy(stats, &sniffer_stats, sizeof(sniff_stats_t));
    return ESP_OK;
}

esp_err_t can_sniffer_set_filter(const sniff_filter_t *filter) {
    if (!filter) return ESP_ERR_INVALID_ARG;
    
    memcpy(&current_filter, filter, sizeof(sniff_filter_t));
    ESP_LOGI(TAG, "Filter updated: 0x%03lX - 0x%03lX", 
             current_filter.id_min, current_filter.id_max);
    
    return ESP_OK;
}

esp_err_t can_sniffer_get_log_path(char *path, size_t path_len) {
    if (!path || path_len == 0) return ESP_ERR_INVALID_ARG;
    
    if (strlen(current_log_path) == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    
    strncpy(path, current_log_path, path_len - 1);
    path[path_len - 1] = '\0';
    
    return ESP_OK;
}

esp_err_t can_sniffer_process_message(const can_raw_message_t *msg) {
    // Only capture when active
    if (sniffer_state != SNIFF_STATE_ACTIVE && 
        sniffer_state != SNIFF_STATE_STORAGE_LOW) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Apply filter
    if (!filter_accepts_id(msg->identifier)) {
        return ESP_OK;  // Filtered out, not an error
    }
    
    // Track unique ID
    track_unique_id(msg->identifier);
    
    // Add to circular buffer
    if (xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(1)) == pdTRUE) {
        if (buffer_count >= SNIFF_BUFFER_SIZE) {
            // Buffer overflow - overwrite oldest
            sniffer_stats.buffer_overflows++;
            buffer_tail = (buffer_tail + 1) % SNIFF_BUFFER_SIZE;
            buffer_count--;
        }
        
        memcpy(&message_buffer[buffer_head], msg, sizeof(can_raw_message_t));
        buffer_head = (buffer_head + 1) % SNIFF_BUFFER_SIZE;
        buffer_count++;
        
        sniffer_stats.messages_captured++;
        sniffer_stats.last_message_time = msg->timestamp_us;
        
        xSemaphoreGive(buffer_mutex);
    }
    
    return ESP_OK;
}

esp_err_t can_sniffer_flush(void) {
    if (sniffer_state == SNIFF_STATE_IDLE) {
        return ESP_OK;
    }
    
    // Flush buffer to file
    esp_err_t ret = flush_buffer_to_file();
    
    // Check storage status
    update_storage_stats();
    
    if (sniffer_stats.storage_percent_used >= 95.0f) {
        ESP_LOGW(TAG, "Storage full! Stopping sniffer.");
        can_sniffer_stop();
        sniffer_state = SNIFF_STATE_STORAGE_FULL;
    } else if (sniffer_stats.storage_percent_used >= (100 - SNIFF_STORAGE_LOW_THRESHOLD)) {
        if (sniffer_state == SNIFF_STATE_ACTIVE) {
            ESP_LOGW(TAG, "Storage low: %.1f%% used", sniffer_stats.storage_percent_used);
            sniffer_state = SNIFF_STATE_STORAGE_LOW;
        }
    }
    
    return ret;
}

bool can_sniffer_is_storage_low(void) {
    update_storage_stats();
    return sniffer_stats.storage_percent_used >= (100 - SNIFF_STORAGE_LOW_THRESHOLD);
}

const char* can_sniffer_state_name(sniff_state_t state) {
    if (state >= 0 && state <= SNIFF_STATE_ERROR) {
        return STATE_NAMES[state];
    }
    return "UNKNOWN";
}
