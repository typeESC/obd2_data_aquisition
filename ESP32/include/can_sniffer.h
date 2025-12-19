/**
 * @file can_sniffer.h
 * @brief CAN bus sniffer for raw message capture and logging
 * @date 2025
 * 
 * Captures raw CAN messages in GVRET CSV format for SavvyCAN compatibility.
 * Operates in parallel with OBD-II PID reading for hybrid data acquisition.
 */

#ifndef CAN_SNIFFER_H
#define CAN_SNIFFER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// === SNIFFER CONFIGURATION ===
#define SNIFF_BUFFER_SIZE       200         // Circular buffer capacity (messages)
#define SNIFF_LOG_DIR           "/spiffs"   // Log directory
#define SNIFF_FILE_PREFIX       "canlog_"   // Log file prefix
#define SNIFF_STORAGE_LOW_THRESHOLD 10      // Percentage - trigger warning LED

// === RAW CAN MESSAGE STRUCTURE ===
typedef struct {
    uint64_t timestamp_us;      // Microsecond precision timestamp
    uint32_t identifier;        // 11-bit or 29-bit CAN ID
    uint8_t data[8];            // Raw payload
    uint8_t dlc;                // Data length code (0-8)
    uint8_t bus;                // Bus number (always 0 for single bus)
    bool extended;              // Extended frame flag (29-bit ID)
    bool is_tx;                 // True if transmitted, false if received
} can_raw_message_t;

// === SNIFFER STATE ===
typedef enum {
    SNIFF_STATE_IDLE,           // Ready but not capturing
    SNIFF_STATE_ACTIVE,         // Actively capturing messages
    SNIFF_STATE_PAUSED,         // Temporarily paused
    SNIFF_STATE_STORAGE_LOW,    // Capturing but storage is low
    SNIFF_STATE_STORAGE_FULL,   // Stopped - storage full
    SNIFF_STATE_ERROR           // Error state
} sniff_state_t;

// === SNIFFER STATISTICS ===
typedef struct {
    uint32_t messages_captured;     // Total messages captured
    uint32_t messages_logged;       // Messages written to file
    uint32_t buffer_overflows;      // Times buffer overflowed
    uint32_t unique_ids;            // Unique CAN IDs seen
    uint64_t start_timestamp;       // Capture start time
    uint64_t last_message_time;     // Last message timestamp
    size_t   bytes_written;         // Total bytes written to file
    size_t   storage_free;          // Free storage space
    float    storage_percent_used;  // Storage usage percentage
} sniff_stats_t;

// === FILTER CONFIGURATION ===
typedef struct {
    uint32_t id_min;                // Minimum CAN ID to capture (0x000 for all)
    uint32_t id_max;                // Maximum CAN ID to capture (0x7FF for all)
    bool exclude_obd_requests;      // Exclude OBD requests (0x7DF)
    bool exclude_obd_responses;     // Exclude OBD responses (0x7E8-0x7EF)
} sniff_filter_t;

// === PUBLIC FUNCTIONS ===

/**
 * @brief Initialize the CAN sniffer module
 * @return ESP_OK on success
 */
esp_err_t can_sniffer_init(void);

/**
 * @brief Deinitialize the CAN sniffer module
 * @return ESP_OK on success
 */
esp_err_t can_sniffer_deinit(void);

/**
 * @brief Start capturing CAN messages
 * @param filter Optional filter configuration (NULL for accept all)
 * @return ESP_OK on success
 */
esp_err_t can_sniffer_start(const sniff_filter_t *filter);

/**
 * @brief Stop capturing CAN messages
 * @return ESP_OK on success
 */
esp_err_t can_sniffer_stop(void);

/**
 * @brief Pause capturing (keeps file open)
 * @return ESP_OK on success
 */
esp_err_t can_sniffer_pause(void);

/**
 * @brief Resume capturing after pause
 * @return ESP_OK on success
 */
esp_err_t can_sniffer_resume(void);

/**
 * @brief Get current sniffer state
 * @return Current sniff_state_t
 */
sniff_state_t can_sniffer_get_state(void);

/**
 * @brief Get sniffer statistics
 * @param stats Pointer to stats structure to fill
 * @return ESP_OK on success
 */
esp_err_t can_sniffer_get_stats(sniff_stats_t *stats);

/**
 * @brief Set message filter
 * @param filter Filter configuration
 * @return ESP_OK on success
 */
esp_err_t can_sniffer_set_filter(const sniff_filter_t *filter);

/**
 * @brief Get current log file path
 * @param path Buffer to store path
 * @param path_len Buffer length
 * @return ESP_OK if file exists
 */
esp_err_t can_sniffer_get_log_path(char *path, size_t path_len);

/**
 * @brief Process incoming CAN message (called from CAN receive task)
 * @param msg Raw CAN message to process
 * @return ESP_OK if message was captured
 */
esp_err_t can_sniffer_process_message(const can_raw_message_t *msg);

/**
 * @brief Flush buffer to file (call periodically)
 * @return ESP_OK on success
 */
esp_err_t can_sniffer_flush(void);

/**
 * @brief Check if storage is low
 * @return true if storage below threshold
 */
bool can_sniffer_is_storage_low(void);

/**
 * @brief Get state name string
 * @param state State enum value
 * @return String representation
 */
const char* can_sniffer_state_name(sniff_state_t state);

#endif // CAN_SNIFFER_H
