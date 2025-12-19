/**
 * @file obd_config.h
 * @brief Configuration header for OBD Data Logger with API integration
 * @date 2025
 */

#ifndef OBD_CONFIG_H
#define OBD_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_timer.h"

// === NETWORK CONFIGURATION ===
#define WIFI_SSID           "AVIVOFIBRA-WIFI6-AC40"        // Configure your WiFi SSID
#define WIFI_PASSWORD       "Mara@2025"    // Configure your WiFi password
#define WIFI_MAX_RETRY      5                       // Maximum WiFi connection retries
#define WIFI_RETRY_DELAY    5000                    // Delay between retries (ms)

// === API CONFIGURATION ===
#define API_BASE_URL        "http://192.168.15.29:8000/api/v1"  // Configure your computer IP to function as API server
#define API_SESSION_ID      "550e8400-e29b-41d4-a716-446655440001"  // Configure session ID
#define API_VEHICLE_ID      "550e8400-e29b-41d4-a716-446655440000"  // Configure vehicle ID
#define API_TIMEOUT_MS      10000                   // HTTP request timeout
#define API_MAX_RETRIES     3                       // Maximum API request retries
#define API_RETRY_DELAY     2000                    // Delay between API retries (ms)

// === DATA BATCHING CONFIGURATION ===
#define BATCH_SIZE          10                      // Number of records per batch
#define BATCH_TIMEOUT_MS    30000                   // Maximum time to wait for batch completion
#define FALLBACK_ENABLED    true                    // Enable local storage fallback

// === OBD CONFIGURATION ===
#define CAN_RX_PIN          GPIO_NUM_27             // CAN RX pin
#define CAN_TX_PIN          GPIO_NUM_25             // CAN TX pin
#define OBD_QUERY_DELAY_MS  50                      // MUDE: 100 → 50
#define OBD_SCAN_INTERVAL   500                     // OK (mantém)
#define OBD_RESPONSE_TIMEOUT 10                     // MUDE: 500 → 20 (CRÍTICO!)
#define OBD_MAX_WAIT_TIME   40                     // ADICIONE esta linha
#define OBD_STRICT_VALIDATION false                 // OK (mantém)

// === MULTI-RATE TASK CONFIGURATION ===
#define CRITICAL_TASK_DELAY_MS 50           // 50ms = 20 Hz
#define HIGH_TASK_DELAY_MS 200              // 100ms = 10 Hz
#define LOW_TASK_DELAY_MS 2000              // 2000ms = 0.5 Hz

// === LOGGING CONFIGURATION ===
#define LOG_TAG             "OBD_LOGGER"
#define LOG_LEVEL           ESP_LOG_INFO
#define ENABLE_DEBUG_LOGS   true  // Set to false to disable debug logs

// === SYSTEM CONFIGURATION ===
#define TASK_STACK_SIZE     8192                    // Task stack size
#define TASK_PRIORITY       5                       // Task priority
#define WATCHDOG_TIMEOUT    30000                   // Watchdog timeout (ms)

// === PERFORMANCE TUNING ===
#define HTTP_BUFFER_SIZE    4096                    // HTTP buffer size
#define JSON_BUFFER_SIZE    12288                    // JSON buffer size
#define SPIFFS_MAX_FILES    5                       // Maximum SPIFFS files
#define HEAP_MIN_FREE       50000                   // Minimum free heap (bytes)

// === OBD PID DEFINITIONS ===
typedef enum {
    PID_ENGINE_LOAD         = 0x04,
    PID_COOLANT_TEMP        = 0x05,
    PID_RPM                 = 0x0C,
    PID_SPEED               = 0x0D,
    PID_TIMING_ADVANCE      = 0x0E,
    PID_INTAKE_AIR_TEMP     = 0x0F,
    PID_MAF_RATE            = 0x10,
    PID_THROTTLE_POS        = 0x11,
    PID_RUN_TIME            = 0x1F,
    PID_DIST_SINCE_CLEAR    = 0x31,
    PID_FUEL_LEVEL          = 0x2F,
    PID_MODULE_VOLTAGE      = 0x42,
    PID_COMMANDED_LAMBDA    = 0x44,
    PID_RELATIVE_THROTTLE   = 0x45,
    PID_ETHANOL_PERCENTAGE  = 0x52,
    PID_OIL_TEMP            = 0x5C,
    PID_FUEL_RATE           = 0x5E
} obd_pid_t;

// === DATA STRUCTURES ===
typedef struct {
    uint64_t device_timestamp;      // ESP32 timestamp in milliseconds
    float rpm;                      // Engine RPM
    int speed;                      // Vehicle speed (km/h)
    int coolant_temp;               // Coolant temperature (°C)
    float engine_load;              // Engine load (%)
    float timing_advance;           // Timing advance (degrees)
    int intake_air_temp;            // Intake air temperature (°C)
    float maf_rate;                 // Mass airflow rate (g/s)
    float throttle_pos;             // Throttle position (%)
    int run_time;                   // Engine runtime (seconds)
    int dist_since_clear;           // Distance since codes cleared (km)
    float fuel_level;               // Fuel level (%)
    float module_voltage;           // Control module voltage (V)
    float commanded_lambda;         // Commanded equivalence ratio
    float relative_throttle;        // Relative throttle position (%)
    float ethanol_percentage;       // Ethanol fuel percentage (%)
    int oil_temp;                   // Oil temperature (°C)
    float fuel_rate;                // Fuel consumption rate (L/h)
    bool valid;                     // Data validity flag
} telemetry_data_t;

typedef struct {
    telemetry_data_t data[BATCH_SIZE];
    int count;                      // Current number of records in batch
    uint64_t first_timestamp;       // Timestamp of first record in batch
    bool ready_to_send;             // Flag indicating batch is ready
} telemetry_batch_t;

typedef enum {
    API_STATUS_DISCONNECTED = 0,
    API_STATUS_CONNECTING,
    API_STATUS_CONNECTED,
    API_STATUS_ERROR,
    API_STATUS_RETRYING
} api_status_t;

typedef struct {
    api_status_t status;
    int retry_count;
    uint64_t last_success;
    uint64_t last_attempt;
    int consecutive_failures;
    bool fallback_active;
} api_connection_t;

// === VALIDATION MACROS ===
#define IS_VALID_RPM(x)         ((x) >= 0 && (x) <= 10000)
#define IS_VALID_SPEED(x)       ((x) >= 0 && (x) <= 300)
#define IS_VALID_TEMP(x)        ((x) >= -40 && (x) <= 200)
#define IS_VALID_PERCENTAGE(x)  ((x) >= 0 && (x) <= 100)
#define IS_VALID_VOLTAGE(x)     ((x) >= 8.0 && (x) <= 18.0)

// === UTILITY MACROS ===
#define ARRAY_SIZE(arr)         (sizeof(arr) / sizeof((arr)[0]))
#define MIN(a, b)               ((a) < (b) ? (a) : (b))
#define MAX(a, b)               ((a) > (b) ? (a) : (b))
#define CLAMP(x, min, max)      (MIN(MAX(x, min), max))

// === DEBUG MACROS ===
#if ENABLE_DEBUG_LOGS
#define DEBUG_LOG(fmt, ...)   ESP_LOGD(LOG_TAG, fmt, ##__VA_ARGS__)

#define OBD_PERF_START() \
    int64_t _perf_start_time = esp_timer_get_time()

#define OBD_PERF_END(name) do { \
    int64_t _perf_duration_us = esp_timer_get_time() - _perf_start_time; \
    ESP_LOGD(LOG_TAG, "PERF: '%s' took %lld us (%.2f ms)", \
             name, \
             _perf_duration_us, \
             (double)_perf_duration_us / 1000.0); \
} while(0)

#else
#define DEBUG_LOG(fmt, ...)
#define OBD_PERF_START()
#define OBD_PERF_END(name)
#endif
#endif // OBD_CONFIG_H
