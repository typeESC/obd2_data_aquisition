/**
 * @file main.c
 * @brief Enhanced OBD-II Data Logger with FastAPI Integration
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
#include "esp_mac.h"

static const char *TAG = "OBD_MAIN";

// Global state variables
static telemetry_batch_t current_batch = {0};
static SemaphoreHandle_t batch_mutex = NULL;
static TimerHandle_t batch_timer = NULL;
static TaskHandle_t obd_task_handle = NULL;
static TaskHandle_t api_task_handle = NULL;
static bool system_running = false;
static telemetry_data_t g_current_telemetry; // O "snapshot" mestre
static SemaphoreHandle_t g_telemetry_mutex = NULL; // Proteção para o snapshot

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

// === PID ARRAYS POR PRIORIDADE ===

// PIDs CRÍTICOS - 50ms (20 Hz)
static const obd_pid_t critical_pids[] = {
    PID_RPM,
    PID_THROTTLE_POS
};

// PIDs ALTA PRIORIDADE - 100ms (10 Hz)
static const obd_pid_t high_priority_pids[] = {
    PID_SPEED,
    PID_ENGINE_LOAD,
    PID_MAF_RATE,
    PID_TIMING_ADVANCE,
    PID_MODULE_VOLTAGE,
    PID_FUEL_LEVEL,
    PID_RELATIVE_THROTTLE
};

// PIDs BAIXA PRIORIDADE - 2000ms (0.5 Hz)
static const obd_pid_t low_priority_pids[] = {
    PID_COOLANT_TEMP,
    PID_INTAKE_AIR_TEMP,
    PID_OIL_TEMP,
    PID_RUN_TIME,
    PID_DIST_SINCE_CLEAR,
    PID_COMMANDED_LAMBDA,
    PID_ETHANOL_PERCENTAGE,
    PID_FUEL_RATE
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
        OBD_PERF_START(); // <--- MUDANÇA AQUI
        
        // Send batch to API
        ret = api_send_telemetry_batch(&current_batch);
        if (ret == ESP_OK) {
            stats.api_sends++;
            ESP_LOGI(TAG, "Batch sent successfully (%d records)", current_batch.count);
        } else {
            stats.api_failures++;
            ESP_LOGW(TAG, "Failed to send batch: %s", esp_err_to_name(ret));
        }

        OBD_PERF_END("Batch send operation"); // <--- MUDANÇA AQUI

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
static void obd_critical_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Critical OBD task started (100ms / 10 Hz)");
    
    uint8_t response_buffer[8];
    size_t response_len;
    int pid_index = 0;
    uint64_t last_stats_time = 0;
    uint32_t readings_in_second = 0;
    TickType_t last_wake_time = xTaskGetTickCount();

    while (system_running) {
        // ... (checagens de heap e CAN) ...
        if (!obd_can_is_ready()) {
            ESP_LOGW(TAG, "CAN interface not ready, reinitializing...");
            obd_can_deinit();
            vTaskDelay(pdMS_TO_TICKS(1000));
            if (obd_can_init() != ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
        }

        // 1. LÊ SEUS PIDS CRÍTICOS
        obd_pid_t current_pid = critical_pids[pid_index];
        response_len = sizeof(response_buffer);
        
        esp_err_t ret = obd_can_request(current_pid, response_buffer, &response_len);
        stats.total_readings++;

        if (ret == ESP_OK) {
            // 2. TRAVA O MUTEX E ATUALIZA O SNAPSHOT MESTRE
            if (xSemaphoreTake(g_telemetry_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                
                if (obd_parse_response(current_pid, response_buffer, response_len, &g_current_telemetry)) {
                    stats.successful_readings++;
                    readings_in_second++;
                    DEBUG_LOG("Critical: PID 0x%02X updated", current_pid);
                } else {
                    stats.failed_readings++;
                }
                
                xSemaphoreGive(g_telemetry_mutex);
            } else {
                ESP_LOGW(TAG, "Critical task couldn't get telemetry mutex!");
                stats.failed_readings++;
            }
        } else {
            stats.failed_readings++;
        }

        // Move para o próximo PID crítico
        pid_index = (pid_index + 1) % ARRAY_SIZE(critical_pids);

        // 3. ADICIONA O SNAPSHOT COMPLETO AO BATCH (A CADA 100ms)
        // Só faz isso se estivermos no último PID do ciclo, para enviar 1 snapshot/ciclo
        if (pid_index == 0) {
            if (xSemaphoreTake(g_telemetry_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                
                g_current_telemetry.valid = true; // Marca o snapshot como válido
                add_to_batch(&g_current_telemetry);
                
                xSemaphoreGive(g_telemetry_mutex);
            }

            // Esta tarefa agora serve como "zelador" para enviar o batch quando ele encher.
            if (current_batch.ready_to_send) {
                ESP_LOGI(TAG, "Batch está cheio, Low task iniciando envio...");
                send_and_reset_batch();
            }
        }

        // Estatísticas a cada segundo
        uint64_t now = esp_timer_get_time() / 1000;
        if (now - last_stats_time >= 1000) {
            ESP_LOGI(TAG, "Critical task: %lu readings/s", readings_in_second);
            readings_in_second = 0;
            last_stats_time = now;
        }

        // Delay preciso para 10 Hz
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(CRITICAL_TASK_DELAY_MS));
    }

    ESP_LOGI(TAG, "Critical OBD task ended");
    vTaskDelete(NULL);
}
/**
 * @brief High priority OBD task - Apenas atualiza o snapshot
 */
static void obd_high_task(void *pvParameters)
{
    ESP_LOGI(TAG, "High priority OBD task started (200ms / 5 Hz)");
    
    uint8_t response_buffer[8];
    size_t response_len;
    int pid_index = 0;
    TickType_t last_wake_time = xTaskGetTickCount();

    while (system_running) {
        if (!obd_can_is_ready()) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        obd_pid_t current_pid = high_priority_pids[pid_index];
        response_len = sizeof(response_buffer);
        
        esp_err_t ret = obd_can_request(current_pid, response_buffer, &response_len);
        stats.total_readings++;

        if (ret == ESP_OK) {
            // Trava o mutex para atualizar o snapshot global
            if (xSemaphoreTake(g_telemetry_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                
                if (obd_parse_response(current_pid, response_buffer, response_len, &g_current_telemetry)) {
                    stats.successful_readings++;
                    DEBUG_LOG("High: PID 0x%02X updated", current_pid);
                } else {
                    stats.failed_readings++;
                }
                
                xSemaphoreGive(g_telemetry_mutex);
            } else {
                ESP_LOGW(TAG, "High task couldn't get telemetry mutex!");
                stats.failed_readings++;
            }
        } else {
            stats.failed_readings++;
        }

        pid_index = (pid_index + 1) % ARRAY_SIZE(high_priority_pids);
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(HIGH_TASK_DELAY_MS));
    }

    ESP_LOGI(TAG, "High priority OBD task ended");
    vTaskDelete(NULL);
}

/**
 * @brief Low priority OBD task - Apenas atualiza o snapshot e gerencia o envio
 */
static void obd_low_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Low priority OBD task started (2000ms / 0.5 Hz)");
    
    uint8_t response_buffer[8];
    size_t response_len;
    int pid_index = 0;
    TickType_t last_wake_time = xTaskGetTickCount();

    while (system_running) {
        // ... (checagem de heap) ...
        
        obd_pid_t current_pid = low_priority_pids[pid_index];
        response_len = sizeof(response_buffer);
        
        esp_err_t ret = obd_can_request(current_pid, response_buffer, &response_len);
        stats.total_readings++;
        
        if (ret == ESP_OK) {
            // Trava o mutex para atualizar o snapshot global
            if (xSemaphoreTake(g_telemetry_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                
                if (obd_parse_response(current_pid, response_buffer, response_len, &g_current_telemetry)) {
                    stats.successful_readings++;
                    DEBUG_LOG("Low: PID 0x%02X updated", current_pid);
                } else {
                    stats.failed_readings++;
                }
                
                xSemaphoreGive(g_telemetry_mutex);
            } else {
                ESP_LOGW(TAG, "Low task couldn't get telemetry mutex!");
                stats.failed_readings++;
            }
        } else {
            stats.failed_readings++;
        }

        pid_index = (pid_index + 1) % ARRAY_SIZE(low_priority_pids);
    

        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(LOW_TASK_DELAY_MS));
    }

    ESP_LOGI(TAG, "Low priority OBD task ended");
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
    
    ESP_LOGI(TAG, "--- Current Telemetry Snapshot ---");
    if (xSemaphoreTake(g_telemetry_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        obd_log_telemetry(&g_current_telemetry);
        xSemaphoreGive(g_telemetry_mutex);
    } else {
        ESP_LOGW(TAG, "Failed to get telemetry mutex to log stats.");
    }

    stats.last_stats_print = now;

    if (arb_lost > 100) {
    ESP_LOGW(TAG, "⚠️  HIGH CAN ARBITRATION LOST: %lu (bus overloaded!)", arb_lost);
    }

    ESP_LOGI(TAG, "CAN Arbitration lost: %lu/min", arb_lost);
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

    if (xTimerStart(batch_timer, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start batch timer");
        return ESP_FAIL;
    }

    // ✅ Create CRITICAL task (50ms)
    TaskHandle_t critical_task_handle;
    BaseType_t ret = xTaskCreate(obd_critical_task, "OBD_Critical", TASK_STACK_SIZE, NULL, 
                                TASK_PRIORITY + 3, &critical_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create critical OBD task");
        return ESP_FAIL;
    }

    // ✅ Create HIGH task (100ms)
    TaskHandle_t high_task_handle;
    ret = xTaskCreate(obd_high_task, "OBD_High", TASK_STACK_SIZE, NULL, 
                     TASK_PRIORITY + 2, &high_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create high priority OBD task");
        return ESP_FAIL;
    }

    // ✅ Create LOW task (2000ms)
    ret = xTaskCreate(obd_low_task, "OBD_Low", TASK_STACK_SIZE, NULL, 
                     TASK_PRIORITY, &obd_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create low priority OBD task");
        return ESP_FAIL;
    }

    // Create API task
    ret = xTaskCreate(api_task, "API_Task", TASK_STACK_SIZE, NULL, 
                     TASK_PRIORITY - 1, &api_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create API task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "All tasks started successfully");
    ESP_LOGI(TAG, "  - Critical: 2 PIDs @ 20 Hz (50ms)");
    ESP_LOGI(TAG, "  - High:     7 PIDs @ 10 Hz (100ms)");
    ESP_LOGI(TAG, "  - Low:      8 PIDs @ 0.5 Hz (2s)");
    
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