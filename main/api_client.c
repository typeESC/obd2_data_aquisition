/**
 * @file api_client.c
 * @brief HTTP API client implementation for sending telemetry data
 * @author Mario Venere Neto
 * @date 2025
 */

#include "api_client.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "API_CLIENT";
static esp_http_client_handle_t http_client = NULL;
static api_connection_t connection_status = {0};
static SemaphoreHandle_t api_mutex = NULL;
static char response_buffer[1024];
static bool fallback_mode = false;

// HTTP event handler
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGW(TAG, "HTTP error event");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            DEBUG_LOG("HTTP connected");
            break;
        case HTTP_EVENT_HEADER_SENT:
            DEBUG_LOG("HTTP headers sent");
            break;
        case HTTP_EVENT_ON_HEADER:
            DEBUG_LOG("HTTP header: %.*s: %.*s", evt->header_key_len, evt->header_key,
                     evt->header_value_len, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            if (evt->data_len < sizeof(response_buffer)) {
                memcpy(response_buffer, evt->data, evt->data_len);
                response_buffer[evt->data_len] = '\0';
                DEBUG_LOG("HTTP response: %s", response_buffer);
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            DEBUG_LOG("HTTP request finished");
            break;
        case HTTP_EVENT_DISCONNECTED:
            DEBUG_LOG("HTTP disconnected");
            break;
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t api_client_init(void)
{
    if (http_client != NULL) {
        ESP_LOGW(TAG, "API client already initialized");
        return ESP_OK;
    }

    // Create mutex for thread safety
    api_mutex = xSemaphoreCreateMutex();
    if (api_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create API mutex");
        return ESP_FAIL;
    }

    // Configure HTTP client
    esp_http_client_config_t config = {
        .url = API_BASE_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = API_TIMEOUT_MS,
        .event_handler = http_event_handler,
        .buffer_size = HTTP_BUFFER_SIZE,
        .buffer_size_tx = HTTP_BUFFER_SIZE,
        .user_agent = "ESP32-OBD-Logger/1.0",
        .disable_auto_redirect = true,
        .max_redirection_count = 0,
    };

    http_client = esp_http_client_init(&config);
    if (http_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        vSemaphoreDelete(api_mutex);
        api_mutex = NULL;
        return ESP_FAIL;
    }

    // Initialize connection status
    connection_status.status = API_STATUS_DISCONNECTED;
    connection_status.retry_count = 0;
    connection_status.last_success = 0;
    connection_status.last_attempt = 0;
    connection_status.consecutive_failures = 0;
    connection_status.fallback_active = false;

    // Set common headers
    esp_http_client_set_header(http_client, "Content-Type", "application/json");
    esp_http_client_set_header(http_client, "Accept", "application/json");
    esp_http_client_set_header(http_client, "User-Agent", "ESP32-OBD-Logger/1.0");

    ESP_LOGI(TAG, "API client initialized successfully");
    return ESP_OK;
}

void api_client_deinit(void)
{
    if (xSemaphoreTake(api_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (http_client != NULL) {
            esp_http_client_cleanup(http_client);
            http_client = NULL;
        }
        xSemaphoreGive(api_mutex);
        
        vSemaphoreDelete(api_mutex);
        api_mutex = NULL;
        
        ESP_LOGI(TAG, "API client deinitialized");
    }
}

esp_err_t api_send_telemetry_batch(const telemetry_batch_t *batch)
{
    if (batch == NULL || batch->count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (fallback_mode) {
        ESP_LOGW(TAG, "API in fallback mode, not sending batch");
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (xSemaphoreTake(api_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire API mutex for batch send");
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;
    char *json_data = NULL;
    
    PERF_START();

    // Allocate JSON buffer
    json_data = malloc(JSON_BUFFER_SIZE);
    if (json_data == NULL) {
        ESP_LOGE(TAG, "Failed to allocate JSON buffer");
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    // Convert batch to JSON
    ret = api_batch_to_json(batch, json_data, JSON_BUFFER_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to convert batch to JSON");
        goto cleanup;
    }

    // Update connection status
    connection_status.status = API_STATUS_CONNECTING;
    connection_status.last_attempt = esp_timer_get_time() / 1000;

    // Set URL for batch endpoint
    char batch_url[256];
    snprintf(batch_url, sizeof(batch_url), "%s/telemetry/batch", API_BASE_URL);
    esp_http_client_set_url(http_client, batch_url);
    esp_http_client_set_post_field(http_client, json_data, strlen(json_data));

    // Perform HTTP request
    ret = esp_http_client_perform(http_client);
    if (ret == ESP_OK) {
        int status_code = esp_http_client_get_status_code(http_client);
        if (status_code >= 200 && status_code < 300) {
            connection_status.status = API_STATUS_CONNECTED;
            connection_status.last_success = esp_timer_get_time() / 1000;
            connection_status.consecutive_failures = 0;
            ESP_LOGI(TAG, "Batch sent successfully (%d records, status: %d)", batch->count, status_code);
        } else {
            connection_status.status = API_STATUS_ERROR;
            connection_status.consecutive_failures++;
            ESP_LOGW(TAG, "API returned error status: %d", status_code);
            ret = ESP_FAIL;
        }
    } else {
        connection_status.status = API_STATUS_ERROR;
        connection_status.consecutive_failures++;
        ESP_LOGW(TAG, "HTTP request failed: %s", esp_err_to_name(ret));
    }

    PERF_END("API batch send");

    // Check if we should enter fallback mode
    if (connection_status.consecutive_failures >= API_MAX_RETRIES && FALLBACK_ENABLED) {
        fallback_mode = true;
        connection_status.fallback_active = true;
        ESP_LOGW(TAG, "Entering fallback mode after %d failures", connection_status.consecutive_failures);
    }

cleanup:
    if (json_data) {
        free(json_data);
    }
    xSemaphoreGive(api_mutex);
    return ret;
}

esp_err_t api_send_telemetry_single(const telemetry_data_t *telemetry)
{
    if (telemetry == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (fallback_mode) {
        ESP_LOGW(TAG, "API in fallback mode, not sending single record");
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (xSemaphoreTake(api_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire API mutex for single send");
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;
    char *json_data = NULL;

    // Allocate JSON buffer
    json_data = malloc(JSON_BUFFER_SIZE);
    if (json_data == NULL) {
        ESP_LOGE(TAG, "Failed to allocate JSON buffer");
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    // Convert telemetry to JSON
    ret = api_telemetry_to_json(telemetry, json_data, JSON_BUFFER_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to convert telemetry to JSON");
        goto cleanup;
    }

    // Set URL for single record endpoint
    char single_url[256];
    snprintf(single_url, sizeof(single_url), "%s/telemetry", API_BASE_URL);
    esp_http_client_set_url(http_client, single_url);
    esp_http_client_set_post_field(http_client, json_data, strlen(json_data));

    // Perform HTTP request
    ret = esp_http_client_perform(http_client);
    if (ret == ESP_OK) {
        int status_code = esp_http_client_get_status_code(http_client);
        if (status_code >= 200 && status_code < 300) {
            DEBUG_LOG("Single telemetry record sent successfully (status: %d)", status_code);
        } else {
            ESP_LOGW(TAG, "API returned error status: %d", status_code);
            ret = ESP_FAIL;
        }
    } else {
        ESP_LOGW(TAG, "HTTP request failed: %s", esp_err_to_name(ret));
    }

cleanup:
    if (json_data) {
        free(json_data);
    }
    xSemaphoreGive(api_mutex);
    return ret;
}

esp_err_t api_test_connection(void)
{
    if (xSemaphoreTake(api_mutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;
    
    // Set URL for health check
    char health_url[256];
    snprintf(health_url, sizeof(health_url), "%s/health", API_BASE_URL);
    esp_http_client_set_url(http_client, health_url);
    esp_http_client_set_method(http_client, HTTP_METHOD_GET);
    esp_http_client_set_post_field(http_client, NULL, 0);

    ret = esp_http_client_perform(http_client);
    if (ret == ESP_OK) {
        int status_code = esp_http_client_get_status_code(http_client);
        if (status_code == 200) {
            ESP_LOGI(TAG, "API health check successful");
            connection_status.status = API_STATUS_CONNECTED;
            fallback_mode = false;
            connection_status.fallback_active = false;
        } else {
            ESP_LOGW(TAG, "API health check failed with status: %d", status_code);
            ret = ESP_FAIL;
        }
    } else {
        ESP_LOGW(TAG, "API health check failed: %s", esp_err_to_name(ret));
        connection_status.status = API_STATUS_ERROR;
    }

    // Reset method back to POST for telemetry
    esp_http_client_set_method(http_client, HTTP_METHOD_POST);
    
    xSemaphoreGive(api_mutex);
    return ret;
}

const api_connection_t* api_get_connection_status(void)
{
    return &connection_status;
}

void api_reset_stats(void)
{
    connection_status.retry_count = 0;
    connection_status.consecutive_failures = 0;
    connection_status.last_success = 0;
    connection_status.last_attempt = 0;
}

void api_set_fallback_mode(bool enable)
{
    fallback_mode = enable;
    connection_status.fallback_active = enable;
    ESP_LOGI(TAG, "Fallback mode %s", enable ? "enabled" : "disabled");
}

bool api_is_fallback_mode(void)
{
    return fallback_mode;
}

esp_err_t api_telemetry_to_json(const telemetry_data_t *telemetry, char *json_buffer, size_t buffer_size)
{
    if (telemetry == NULL || json_buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // Add required fields
    cJSON_AddStringToObject(json, "session_id", API_SESSION_ID);
    cJSON_AddStringToObject(json, "vehicle_id", API_VEHICLE_ID);
    cJSON_AddNumberToObject(json, "device_timestamp", telemetry->device_timestamp);

    // Add telemetry data
    if (telemetry->rpm >= 0) cJSON_AddNumberToObject(json, "rpm", telemetry->rpm);
    if (telemetry->speed >= 0) cJSON_AddNumberToObject(json, "speed", telemetry->speed);
    if (telemetry->coolant_temp > -999) cJSON_AddNumberToObject(json, "coolant_temp", telemetry->coolant_temp);
    if (telemetry->engine_load >= 0) cJSON_AddNumberToObject(json, "engine_load", telemetry->engine_load);
    if (telemetry->timing_advance > -999) cJSON_AddNumberToObject(json, "timing_advance", telemetry->timing_advance);
    if (telemetry->intake_air_temp > -999) cJSON_AddNumberToObject(json, "intake_air_temp", telemetry->intake_air_temp);
    if (telemetry->maf_rate >= 0) cJSON_AddNumberToObject(json, "maf_rate", telemetry->maf_rate);
    if (telemetry->throttle_pos >= 0) cJSON_AddNumberToObject(json, "throttle_pos", telemetry->throttle_pos);
    if (telemetry->run_time >= 0) cJSON_AddNumberToObject(json, "run_time", telemetry->run_time);
    if (telemetry->dist_since_clear >= 0) cJSON_AddNumberToObject(json, "dist_since_clear", telemetry->dist_since_clear);
    if (telemetry->fuel_level >= 0) cJSON_AddNumberToObject(json, "fuel_level", telemetry->fuel_level);
    if (telemetry->module_voltage >= 0) cJSON_AddNumberToObject(json, "module_voltage", telemetry->module_voltage);
    if (telemetry->commanded_lambda >= 0) cJSON_AddNumberToObject(json, "commanded_lambda", telemetry->commanded_lambda);
    if (telemetry->relative_throttle >= 0) cJSON_AddNumberToObject(json, "relative_throttle", telemetry->relative_throttle);
    if (telemetry->ethanol_percentage >= 0) cJSON_AddNumberToObject(json, "ethanol_percentage", telemetry->ethanol_percentage);
    if (telemetry->oil_temp > -999) cJSON_AddNumberToObject(json, "oil_temp", telemetry->oil_temp);
    if (telemetry->fuel_rate >= 0) cJSON_AddNumberToObject(json, "fuel_rate", telemetry->fuel_rate);

    char *json_string = cJSON_Print(json);
    if (json_string == NULL) {
        cJSON_Delete(json);
        return ESP_ERR_NO_MEM;
    }

    if (strlen(json_string) >= buffer_size) {
        free(json_string);
        cJSON_Delete(json);
        return ESP_ERR_INVALID_SIZE;
    }

    strcpy(json_buffer, json_string);
    free(json_string);
    cJSON_Delete(json);

    return ESP_OK;
}

esp_err_t api_batch_to_json(const telemetry_batch_t *batch, char *json_buffer, size_t buffer_size)
{
    if (batch == NULL || json_buffer == NULL || buffer_size == 0 || batch->count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // Add batch metadata
    cJSON_AddStringToObject(json, "session_id", API_SESSION_ID);
    cJSON_AddStringToObject(json, "vehicle_id", API_VEHICLE_ID);
    cJSON_AddNumberToObject(json, "batch_size", batch->count);
    cJSON_AddNumberToObject(json, "first_timestamp", batch->first_timestamp);

    // Create telemetry data array
    cJSON *telemetry_array = cJSON_CreateArray();
    if (telemetry_array == NULL) {
        cJSON_Delete(json);
        return ESP_ERR_NO_MEM;
    }

    // Add each telemetry record
    for (int i = 0; i < batch->count; i++) {
        cJSON *telemetry_obj = cJSON_CreateObject();
        if (telemetry_obj == NULL) {
            cJSON_Delete(telemetry_array);
            cJSON_Delete(json);
            return ESP_ERR_NO_MEM;
        }

        const telemetry_data_t *t = &batch->data[i];
        
        cJSON_AddNumberToObject(telemetry_obj, "device_timestamp", t->device_timestamp);
        if (t->rpm >= 0) cJSON_AddNumberToObject(telemetry_obj, "rpm", t->rpm);
        if (t->speed >= 0) cJSON_AddNumberToObject(telemetry_obj, "speed", t->speed);
        if (t->coolant_temp > -999) cJSON_AddNumberToObject(telemetry_obj, "coolant_temp", t->coolant_temp);
        if (t->engine_load >= 0) cJSON_AddNumberToObject(telemetry_obj, "engine_load", t->engine_load);
        if (t->timing_advance > -999) cJSON_AddNumberToObject(telemetry_obj, "timing_advance", t->timing_advance);
        if (t->intake_air_temp > -999) cJSON_AddNumberToObject(telemetry_obj, "intake_air_temp", t->intake_air_temp);
        if (t->maf_rate >= 0) cJSON_AddNumberToObject(telemetry_obj, "maf_rate", t->maf_rate);
        if (t->throttle_pos >= 0) cJSON_AddNumberToObject(telemetry_obj, "throttle_pos", t->throttle_pos);
        if (t->run_time >= 0) cJSON_AddNumberToObject(telemetry_obj, "run_time", t->run_time);
        if (t->dist_since_clear >= 0) cJSON_AddNumberToObject(telemetry_obj, "dist_since_clear", t->dist_since_clear);
        if (t->fuel_level >= 0) cJSON_AddNumberToObject(telemetry_obj, "fuel_level", t->fuel_level);
        if (t->module_voltage >= 0) cJSON_AddNumberToObject(telemetry_obj, "module_voltage", t->module_voltage);
        if (t->commanded_lambda >= 0) cJSON_AddNumberToObject(telemetry_obj, "commanded_lambda", t->commanded_lambda);
        if (t->relative_throttle >= 0) cJSON_AddNumberToObject(telemetry_obj, "relative_throttle", t->relative_throttle);
        if (t->ethanol_percentage >= 0) cJSON_AddNumberToObject(telemetry_obj, "ethanol_percentage", t->ethanol_percentage);
        if (t->oil_temp > -999) cJSON_AddNumberToObject(telemetry_obj, "oil_temp", t->oil_temp);
        if (t->fuel_rate >= 0) cJSON_AddNumberToObject(telemetry_obj, "fuel_rate", t->fuel_rate);

        cJSON_AddItemToArray(telemetry_array, telemetry_obj);
    }

    cJSON_AddItemToObject(json, "telemetry_data", telemetry_array);

    char *json_string = cJSON_Print(json);
    if (json_string == NULL) {
        cJSON_Delete(json);
        return ESP_ERR_NO_MEM;
    }

    if (strlen(json_string) >= buffer_size) {
        free(json_string);
        cJSON_Delete(json);
        return ESP_ERR_INVALID_SIZE;
    }

    strcpy(json_buffer, json_string);
    free(json_string);
    cJSON_Delete(json);

    return ESP_OK;
}
