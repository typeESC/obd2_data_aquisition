/**
 * @file main.c
 * @brief Enhanced OBD-II Data Logger with FastAPI Integration
 * @author Mario Venere Neto
 * @date 2025
 * 
 * Features:
 * - Real-time OBD-II data acquisition via CAN bus
 * - WiFi connectivity with auto-reconnect
 * - HTTP API integration with batch processing
 * - Fallback mode for offline operation
 * - Comprehensive error handling and logging
 * - Performance optimized for ESP32
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"

// Local includes
#include "obd_config.h"
#include "obd_can.h"
#include "obd_parser.h"
#include "api_client.h"
#include "wifi_manager.h"

static const char *TAG = "OBD_MAIN";

// Global state variables
static telemetry_batch_t current_batch = {0};
static SemaphoreHandle_t batch_mutex = NULL;
static TimerHandle_t batch_timer = NULL;
static TaskHandle_t obd_task_handle = NULL;
static TaskHandle_t api_task_handle = NULL;
static bool system_running = false;

// Statistics
static struct {
    uint32_t total_readings;
    uint32_t successful_readings;
    uint32_t failed_readings;
    uint32_t api_sends;
    uint32_t api_failures;
    uint64_t start_time;
    uint64_t last_stats_print;
} stats = {0};

// OBD PIDs to scan (in order of priority)
static const obd_pid_t obd_scan_pids[] = {
    PID_RPM,                    // Most critical
    PID_SPEED,
    PID_COOLANT_TEMP,
    PID_ENGINE_LOAD,
    PID_THROTTLE_POS,
    PID_MAF_RATE,
    PID_TIMING_ADVANCE,
    PID_INTAKE_AIR_TEMP,
    PID_FUEL_LEVEL,
    PID_MODULE_VOLTAGE,
    PID_RUN_TIME,
    PID_DIST_SINCE_CLEAR,
    PID_COMMANDED_LAMBDA,
    PID_RELATIVE_THROTTLE,
    PID_ETHANOL_PERCENTAGE,
    PID_OIL_TEMP,
    PID_FUEL_RATE               // Least critical
};

/**
 * @brief Add telemetry data to current batch
 * @param telemetry Pointer to telemetry data to add
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t add_to_batch(const telemetry_data_t *telemetry)
{
    if (telemetry == NULL || !telemetry->valid) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(batch_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire batch mutex");
        return ESP_ERR_TIMEOUT;
    }

    // Initialize batch if empty
    if (current_batch.count == 0) {
        current_batch.first_timestamp = telemetry->device_timestamp;
    }

    // Add data to batch
    memcpy(&current_batch.data[current_batch.count], telemetry, sizeof(telemetry_data_t));
    current_batch.count++;

    // Check if batch is full
    if (current_batch.count >= BATCH_SIZE) {
        current_batch.ready_to_send = true;
        ESP_LOGI(TAG, "Batch full (%d records), ready to send", current_batch.count);
    }

    xSemaphoreGive(batch_mutex);
    return ESP_OK;
}

/**
 * @brief Send current batch to API and reset
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t send_and_reset_batch(void)
{
    if (xSemaphoreTake(batch_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire batch mutex for send");
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;

    if (current_batch.count > 0) {
        PERF_START();
        
        // Send batch to API
        ret = api_send_telemetry_batch(&current_batch);
        if (ret == ESP_OK) {
            stats.api_sends++;
            ESP_LOGI(TAG, "Batch sent successfully (%d records)", current_batch.count);
        } else {
            stats.api_failures++;
            ESP_LOGW(TAG, "Failed to send batch: %s", esp_err_to_name(ret));
        }

        PERF_END("Batch send operation");

        // Reset batch regardless of send result
        memset(&current_batch, 0, sizeof(telemetry_batch_t));
    }

    xSemaphoreGive(batch_mutex);
    return ret;
}

/**
 * @brief Timer callback for batch timeout
 * @param xTimer Timer handle
 */
static void batch_timer_callback(TimerHandle_t xTimer)
{
    if (current_batch.count > 0) {
        ESP_LOGI(TAG, "Batch timeout - sending partial batch (%d records)", current_batch.count);
        send_and_reset_batch();
    }
}

/**
 * @brief OBD data acquisition task
 * @param pvParameters Task parameters
 */
static void obd_task(void *pvParameters)
{
    ESP_LOGI(TAG, "OBD task started");
    
    telemetry_data_t telemetry;
    uint8_t response_buffer[8];
    size_t response_len;
    int pid_index = 0;
    uint64_t last_reading_time = 0;

    while (system_running) {
        // Check if we have enough heap memory
        size_t free_heap = esp_get_free_heap_size();
        if (free_heap < HEAP_MIN_FREE) {
            ESP_LOGW(TAG, "Low heap memory: %d bytes", free_heap);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // Check CAN interface health
        if (!obd_can_is_ready()) {
            ESP_LOGW(TAG, "CAN interface not ready, reinitializing...");
            obd_can_deinit();
            vTaskDelay(pdMS_TO_TICKS(1000));
            if (obd_can_init() != ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
        }

        // Initialize telemetry structure
        obd_init_telemetry(&telemetry);

        // Request OBD data for current PID
        obd_pid_t current_pid = obd_scan_pids[pid_index];
        response_len = sizeof(response_buffer);
        
        esp_err_t ret = obd_can_request(current_pid, response_buffer, &response_len);
        stats.total_readings++;

        if (ret == ESP_OK) {
            // Parse the response
            if (obd_parse_response(current_pid, response_buffer, response_len, &telemetry)) {
                stats.successful_readings++;
                
                // Validate telemetry data
                if (obd_validate_telemetry(&telemetry)) {
                    telemetry.valid = true;
                    
                    // Add to batch
                    if (add_to_batch(&telemetry) == ESP_OK) {
                        DEBUG_LOG("Added PID 0x%02X to batch", current_pid);
                    }
                    
                    last_reading_time = esp_timer_get_time() / 1000;
                } else {
                    ESP_LOGW(TAG, "Telemetry validation failed for PID 0x%02X", current_pid);
                    stats.failed_readings++;
                }
            } else {
                ESP_LOGW(TAG, "Failed to parse response for PID 0x%02X", current_pid);
                stats.failed_readings++;
            }
        } else {
            stats.failed_readings++;
            DEBUG_LOG("Failed to read PID 0x%02X: %s", current_pid, esp_err_to_name(ret));
        }

        // Move to next PID
        pid_index = (pid_index + 1) % ARRAY_SIZE(obd_scan_pids);

        // Send batch if ready
        if (current_batch.ready_to_send) {
            send_and_reset_batch();
        }

        // Delay between readings
        vTaskDelay(pdMS_TO_TICKS(OBD_QUERY_DELAY_MS));
    }

    ESP_LOGI(TAG, "OBD task ended");
    vTaskDelete(NULL);
}

/**
 * @brief API management task
 * @param pvParameters Task parameters
 */
static void api_task(void *pvParameters)
{
    ESP_LOGI(TAG, "API task started");
    
    uint64_t last_health_check = 0;
    const uint64_t health_check_interval = 60000; // 1 minute

    while (system_running) {
        uint64_t now = esp_timer_get_time() / 1000;

        // Periodic health check
        if (now - last_health_check > health_check_interval) {
            if (wifi_is_connected()) {
                esp_err_t ret = api_test_connection();
                if (ret == ESP_OK) {
                    DEBUG_LOG("API health check passed");
                } else {
                    ESP_LOGW(TAG, "API health check failed");
                }
            }
            last_health_check = now;
        }

        // Monitor batch timer
        if (current_batch.count > 0) {
            uint64_t batch_age = now - current_batch.first_timestamp;
            if (batch_age > BATCH_TIMEOUT_MS) {
                ESP_LOGI(TAG, "Batch timeout exceeded, forcing send");
                send_and_reset_batch();
            }
        }

        // Check WiFi status and attempt reconnection if needed
        const wifi_connection_t *wifi_status = wifi_get_status();
        if (wifi_status->status == WIFI_STATUS_DISCONNECTED && 
            wifi_status->auto_reconnect_enabled) {
            ESP_LOGI(TAG, "Attempting WiFi reconnection...");
            wifi_connect(WIFI_SSID, WIFI_PASSWORD);
        }

        vTaskDelay(pdMS_TO_TICKS(5000)); // Check every 5 seconds
    }

    ESP_LOGI(TAG, "API task ended");
    vTaskDelete(NULL);
}

/**
 * @brief Print system statistics
 */
static void print_statistics(void)
{
    uint64_t now = esp_timer_get_time() / 1000;
    uint64_t uptime = (now - stats.start_time) / 1000; // Convert to seconds
    
    ESP_LOGI(TAG, "=== System Statistics ===");
    ESP_LOGI(TAG, "Uptime: %llu seconds (%.1f minutes)", uptime, uptime / 60.0f);
    ESP_LOGI(TAG, "Total OBD readings: %u", stats.total_readings);
    ESP_LOGI(TAG, "Successful readings: %u (%.1f%%)", stats.successful_readings, 
             stats.total_readings ? (stats.successful_readings * 100.0f / stats.total_readings) : 0);
    ESP_LOGI(TAG, "Failed readings: %u (%.1f%%)", stats.failed_readings,
             stats.total_readings ? (stats.failed_readings * 100.0f / stats.total_readings) : 0);
    ESP_LOGI(TAG, "API sends: %u", stats.api_sends);
    ESP_LOGI(TAG, "API failures: %u", stats.api_failures);
    ESP_LOGI(TAG, "Current batch size: %d/%d", current_batch.count, BATCH_SIZE);
    ESP_LOGI(TAG, "Free heap: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Min free heap: %d bytes", esp_get_minimum_free_heap_size());

    // WiFi statistics
    const wifi_connection_t *wifi_status = wifi_get_status();
    if (wifi_is_connected()) {
        char ip_str[16];
        wifi_get_ip_string(ip_str, sizeof(ip_str));
        ESP_LOGI(TAG, "WiFi: Connected to %s, IP: %s, RSSI: %d dBm", 
                WIFI_SSID, ip_str, wifi_status->rssi);
    } else {
        ESP_LOGI(TAG, "WiFi: Disconnected (status: %d)", wifi_status->status);
    }

    // API status
    const api_connection_t *api_status = api_get_connection_status();
    ESP_LOGI(TAG, "API: Status %d, Consecutive failures: %d, Fallback: %s",
             api_status->status, api_status->consecutive_failures,
             api_status->fallback_active ? "YES" : "NO");

    // CAN statistics
    uint32_t tx_errors, rx_errors, arb_lost, bus_errors;
    obd_can_get_stats(&tx_errors, &rx_errors, &arb_lost, &bus_errors);
    ESP_LOGI(TAG, "CAN: TX errors: %u, RX errors: %u, Bus errors: %u",
             tx_errors, rx_errors, bus_errors);
    
    ESP_LOGI(TAG, "========================");
    
    stats.last_stats_print = now;
}

/**
 * @brief Initialize system components
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t system_init(void)
{
    esp_err_t ret = ESP_OK;

    ESP_LOGI(TAG, "Initializing OBD Data Logger System...");

    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Create synchronization objects
    batch_mutex = xSemaphoreCreateMutex();
    if (batch_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create batch mutex");
        return ESP_FAIL;
    }

    // Create batch timer
    batch_timer = xTimerCreate("BatchTimer", 
                              pdMS_TO_TICKS(BATCH_TIMEOUT_MS),
                              pdTRUE,  // Auto-reload
                              NULL,
                              batch_timer_callback);
    if (batch_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create batch timer");
        return ESP_FAIL;
    }

    // Initialize WiFi
    ret = wifi_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi manager: %s", esp_err_to_name(ret));
        return ret;
    }

    // Initialize API client
    ret = api_client_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize API client: %s", esp_err_to_name(ret));
        return ret;
    }

    // Initialize CAN interface
    ret = obd_can_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize CAN interface: %s", esp_err_to_name(ret));
        return ret;
    }

    // Initialize statistics
    stats.start_time = esp_timer_get_time() / 1000;
    stats.last_stats_print = stats.start_time;

    ESP_LOGI(TAG, "System initialization completed successfully");
    return ESP_OK;
}

/**
 * @brief Start system tasks
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t start_tasks(void)
{
    system_running = true;

    // Start batch timer
    if (xTimerStart(batch_timer, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start batch timer");
        return ESP_FAIL;
    }

    // Create OBD task
    BaseType_t ret = xTaskCreate(obd_task, "OBD_Task", TASK_STACK_SIZE, NULL, 
                                TASK_PRIORITY + 1, &obd_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create OBD task");
        return ESP_FAIL;
    }

    // Create API task
    ret = xTaskCreate(api_task, "API_Task", TASK_STACK_SIZE, NULL, 
                     TASK_PRIORITY, &api_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create API task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "All tasks started successfully");
    return ESP_OK;
}

/**
 * @brief Main application entry point
 */
void app_main(void)
{
    ESP_LOGI(TAG, "Starting Enhanced OBD-II Data Logger v1.0");
    ESP_LOGI(TAG, "Build date: %s %s", __DATE__, __TIME__);
    ESP_LOGI(TAG, "ESP-IDF version: %s", esp_get_idf_version());

    // Set log level
    esp_log_level_set("*", LOG_LEVEL);

    // Initialize system
    if (system_init() != ESP_OK) {
        ESP_LOGE(TAG, "System initialization failed, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }

    // Connect to WiFi
    ESP_LOGI(TAG, "Connecting to WiFi: %s", WIFI_SSID);
    esp_err_t ret = wifi_connect(WIFI_SSID, WIFI_PASSWORD);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start WiFi connection: %s", esp_err_to_name(ret));
    }

    // Wait for WiFi connection (with timeout)
    ret = wifi_wait_for_connection(30000); // 30 seconds timeout
    if (ret == ESP_OK) {
        char ip_str[16];
        wifi_get_ip_string(ip_str, sizeof(ip_str));
        ESP_LOGI(TAG, "WiFi connected successfully, IP: %s", ip_str);
        
        // Test API connection
        ret = api_test_connection();
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "API connection test successful");
        } else {
            ESP_LOGW(TAG, "API connection test failed, will retry later");
        }
    } else {
        ESP_LOGW(TAG, "WiFi connection timeout, continuing without network");
        api_set_fallback_mode(true);
    }

    // Start main tasks
    if (start_tasks() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start tasks, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }

    // Main monitoring loop
    uint64_t last_stats_time = 0;
    const uint64_t stats_interval = 60000; // Print stats every minute

    while (true) {
        uint64_t now = esp_timer_get_time() / 1000;

        // Print periodic statistics
        if (now - last_stats_time > stats_interval) {
            print_statistics();
            last_stats_time = now;
        }

        // Monitor system health
        size_t free_heap = esp_get_free_heap_size();
        if (free_heap < HEAP_MIN_FREE) {
            ESP_LOGW(TAG, "Critical heap memory low: %d bytes", free_heap);
            
            // Try to free some memory by sending current batch
            if (current_batch.count > 0) {
                send_and_reset_batch();
            }
        }

        // Check for stack overflow
        UBaseType_t obd_stack_left = uxTaskGetStackHighWaterMark(obd_task_handle);
        UBaseType_t api_stack_left = uxTaskGetStackHighWaterMark(api_task_handle);
        
        if (obd_stack_left < 1024 || api_stack_left < 1024) {
            ESP_LOGW(TAG, "Low stack space - OBD: %u, API: %u", obd_stack_left, api_stack_left);
        }

        vTaskDelay(pdMS_TO_TICKS(10000)); // Main loop every 10 seconds
    }
}