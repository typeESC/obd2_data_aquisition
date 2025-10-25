/**
 * @file obd_can.c
 * @brief CAN communication implementation for OBD-II
 * @author Mario Venere Neto
 * @date 2025
 */

#include "obd_can.h"
#include "obd_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "OBD_CAN";
static bool can_initialized = false;
static SemaphoreHandle_t can_mutex = NULL;
static uint32_t tx_error_count = 0;
static uint32_t rx_error_count = 0;
static uint32_t arb_lost_count = 0;
static uint32_t bus_error_count = 0;

esp_err_t obd_can_init(void)
{
    esp_err_t ret = ESP_OK;

    if (can_initialized) {
        ESP_LOGW(TAG, "CAN already initialized");
        return ESP_OK;
    }

    // Create mutex for thread safety
    can_mutex = xSemaphoreCreateMutex();
    if (can_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create CAN mutex");
        return ESP_FAIL;
    }

    // Configure CAN timing (500kbps)
    twai_timing_config_t timing_config = CAN_TIMING_CONFIG_500KBITS();
    
    // Configure CAN filter (accept all)
    twai_filter_config_t filter_config = CAN_FILTER_CONFIG_ACCEPT_ALL();
    
    // Configure CAN general settings
    twai_general_config_t general_config = CAN_GENERAL_CONFIG_DEFAULT(
        CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
    
    // Set larger queue sizes for better performance
    general_config.tx_queue_len = 10;
    general_config.rx_queue_len = 20;

    // Install TWAI driver
    ret = twai_driver_install(&general_config, &timing_config, &filter_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install TWAI driver: %s", esp_err_to_name(ret));
        vSemaphoreDelete(can_mutex);
        can_mutex = NULL;
        return ret;
    }

    // Start TWAI driver
    ret = twai_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start TWAI driver: %s", esp_err_to_name(ret));
        twai_driver_uninstall();
        vSemaphoreDelete(can_mutex);
        can_mutex = NULL;
        return ret;
    }

    can_initialized = true;
    ESP_LOGI(TAG, "CAN interface initialized successfully");
    
    // Reset error counters
    obd_can_reset_stats();
    
    return ESP_OK;
}

esp_err_t obd_can_deinit(void)
{
    if (!can_initialized) {
        return ESP_OK;
    }

    if (xSemaphoreTake(can_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        // Stop TWAI driver
        esp_err_t ret = twai_stop();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to stop TWAI driver: %s", esp_err_to_name(ret));
        }

        // Uninstall TWAI driver
        ret = twai_driver_uninstall();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to uninstall TWAI driver: %s", esp_err_to_name(ret));
        }

        can_initialized = false;
        xSemaphoreGive(can_mutex);
        
        vSemaphoreDelete(can_mutex);
        can_mutex = NULL;
        
        ESP_LOGI(TAG, "CAN interface deinitialized");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to acquire CAN mutex for deinit");
        return ESP_FAIL;
    }
}

esp_err_t obd_can_request(uint8_t pid, uint8_t *response, size_t *response_len)
{
    if (!can_initialized || response == NULL || response_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(can_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire CAN mutex");
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;
    twai_message_t tx_msg = {0};
    twai_message_t rx_msg = {0};

    // Prepare OBD request message
    tx_msg.identifier = OBD_REQUEST_ID;
    tx_msg.data_length_code = 8;
    tx_msg.data[0] = 0x02;                    // Number of additional bytes
    tx_msg.data[1] = OBD_MODE_CURRENT;     // Mode 01 - current data
    tx_msg.data[2] = pid;                  // PID to request
    tx_msg.data[3] = 0xCC;                 // Padding
    tx_msg.data[4] = 0xCC;
    tx_msg.data[5] = 0xCC;
    tx_msg.data[6] = 0xCC;
    tx_msg.data[7] = 0xCC;

    OBD_PERF_START(); // <--- MUDANÇA AQUI

    // Send request
    ret = twai_transmit(&tx_msg, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        tx_error_count++;
        ESP_LOGW(TAG, "Failed to send OBD request for PID 0x%02X: %s", pid, esp_err_to_name(ret));
        goto cleanup;
    }

    // Wait for response
    ret = twai_receive(&rx_msg, pdMS_TO_TICKS(OBD_RESPONSE_TIMEOUT));
    if (ret != ESP_OK) {
        rx_error_count++;
        if (ret == ESP_ERR_TIMEOUT) {
            DEBUG_LOG("Timeout waiting for OBD response for PID 0x%02X", pid);
        } else {
            ESP_LOGW(TAG, "Failed to receive OBD response for PID 0x%02X: %s", pid, esp_err_to_name(ret));
        }
        goto cleanup;
    }

    OBD_PERF_END("Batch send operation"); // <--- MUDANÇA AQUI

    // Validate response
    if (rx_msg.data_length_code < 3) {
        rx_error_count++;
        ESP_LOGW(TAG, "Invalid OBD response length: %d", rx_msg.data_length_code);
        ret = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    // Check if response is for our PID
    if (rx_msg.data[1] != (OBD_MODE_CURRENT + 0x40) || rx_msg.data[2] != pid) {
        rx_error_count++;
        ESP_LOGW(TAG, "OBD response mismatch - Expected PID 0x%02X, got 0x%02X", pid, rx_msg.data[2]);
        ret = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    // Copy response data (skip length, mode, and PID bytes)
    size_t data_len = rx_msg.data_length_code - 3;
    if (data_len > *response_len) {
        ESP_LOGW(TAG, "Response buffer too small: need %d, have %d", data_len, *response_len);
        data_len = *response_len;
    }

    memcpy(response, &rx_msg.data[3], data_len);
    *response_len = data_len;

    DEBUG_LOG("OBD PID 0x%02X response received (%d bytes)", pid, data_len);

cleanup:
    xSemaphoreGive(can_mutex);
    return ret;
}

bool obd_can_is_ready(void)
{
    if (!can_initialized) {
        return false;
    }

    twai_status_info_t status;
    esp_err_t ret = twai_get_status_info(&status);
    
    if (ret != ESP_OK) {
        return false;
    }

    // Check if driver is in running state and bus is not in error state
    return (status.state == TWAI_STATE_RUNNING) && 
           !(status.bus_error_count > 100 || status.tx_error_counter > 100 || status.rx_error_counter > 100);
}

void obd_can_get_stats(uint32_t *tx_errors, uint32_t *rx_errors, uint32_t *arb_lost, uint32_t *bus_errors)
{
    if (tx_errors) *tx_errors = tx_error_count;
    if (rx_errors) *rx_errors = rx_error_count;
    if (arb_lost) *arb_lost = arb_lost_count;
    if (bus_errors) *bus_errors = bus_error_count;

    // Also get driver statistics if available
    if (can_initialized) {
        twai_status_info_t status;
        if (twai_get_status_info(&status) == ESP_OK) {
            if (bus_errors) *bus_errors += status.bus_error_count;
        }
    }
}

void obd_can_reset_stats(void)
{
    tx_error_count = 0;
    rx_error_count = 0;
    arb_lost_count = 0;
    bus_error_count = 0;

    // Clear driver error counters if possible
    if (can_initialized) {
        twai_clear_transmit_queue();
        twai_clear_receive_queue();
    }
}
