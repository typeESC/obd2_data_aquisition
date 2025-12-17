/**
 * @file obd_config_template.h
 * @brief Configuration template for OBD Data Logger
 * 
 * Copy this file to obd_config.h and modify the settings below
 * according to your specific requirements.
 */

#ifndef OBD_CONFIG_H
#define OBD_CONFIG_H

// === NETWORK CONFIGURATION ===
// TODO: Replace with your actual WiFi credentials
#define WIFI_SSID           "YOUR_WIFI_SSID_HERE"
#define WIFI_PASSWORD       "YOUR_WIFI_PASSWORD_HERE"
#define WIFI_MAX_RETRY      5
#define WIFI_RETRY_DELAY    5000

// === API CONFIGURATION ===
// TODO: Replace with your FastAPI server details
#define API_BASE_URL        "http://192.168.1.100:8000/api/v1"  // Your API server IP and port
#define API_SESSION_ID      "550e8400-e29b-41d4-a716-446655440001"  // Generate unique session ID
#define API_VEHICLE_ID      "550e8400-e29b-41d4-a716-446655440000"  // Generate unique vehicle ID
#define API_TIMEOUT_MS      10000
#define API_MAX_RETRIES     3
#define API_RETRY_DELAY     2000

// === DATA BATCHING CONFIGURATION ===
#define BATCH_SIZE          10          // Recommended: 5-20 records per batch
#define BATCH_TIMEOUT_MS    30000       // 30 seconds timeout
#define FALLBACK_ENABLED    true        // Enable local storage fallback

// === OBD CONFIGURATION ===
// TODO: Adjust pins according to your ESP32 wiring
#define CAN_RX_PIN          GPIO_NUM_27
#define CAN_TX_PIN          GPIO_NUM_25
#define OBD_QUERY_DELAY_MS  50          // Recommended: 5-20ms
#define OBD_SCAN_INTERVAL   500         // Main scan loop interval
#define OBD_RESPONSE_TIMEOUT 500        // OBD response timeout

// === LOGGING CONFIGURATION ===
#define LOG_TAG             "OBD_LOGGER"
#define LOG_LEVEL           ESP_LOG_INFO    // ESP_LOG_DEBUG for verbose logging
#define ENABLE_DEBUG_LOGS   true            // Set to false for production

// === SYSTEM CONFIGURATION ===
#define TASK_STACK_SIZE     8192        // Increase if stack overflow occurs
#define TASK_PRIORITY       5
#define WATCHDOG_TIMEOUT    30000

// === PERFORMANCE TUNING ===
#define HTTP_BUFFER_SIZE    4096        // Increase for larger batches
#define JSON_BUFFER_SIZE    2048        // Increase for more telemetry fields
#define SPIFFS_MAX_FILES    5
#define HEAP_MIN_FREE       50000       // Minimum free heap in bytes

// === VALIDATION SETTINGS ===
// Adjust these ranges according to your vehicle specifications
#define MAX_VALID_RPM       8000        // Maximum expected RPM
#define MAX_VALID_SPEED     250         // Maximum expected speed (km/h)
#define MIN_VALID_TEMP      -40         // Minimum temperature (°C)
#define MAX_VALID_TEMP      150         // Maximum temperature (°C)
#define MIN_VALID_VOLTAGE   8.0         // Minimum voltage (V)
#define MAX_VALID_VOLTAGE   18.0        // Maximum voltage (V)

// === FEATURE FLAGS ===
#define ENABLE_STATISTICS   true        // Enable statistics reporting
#define ENABLE_HEALTH_CHECK true        // Enable periodic API health checks
#define ENABLE_AUTO_RECOVERY true       // Enable automatic error recovery
#define ENABLE_TELEMETRY_VALIDATION true // Enable data validation

#endif // OBD_CONFIG_H
