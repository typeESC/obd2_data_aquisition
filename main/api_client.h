/**
 * @file api_client.h
 * @brief HTTP API client for sending telemetry data to FastAPI backend
 * @author Mario Venere Neto
 * @date 2025
 */

#ifndef API_CLIENT_H
#define API_CLIENT_H

#include "obd_config.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"

/**
 * @brief Initialize API client
 * @return ESP_OK on success, error code on failure
 */
esp_err_t api_client_init(void);

/**
 * @brief Deinitialize API client
 */
void api_client_deinit(void);

/**
 * @brief Send telemetry batch to API server
 * @param batch Pointer to telemetry batch to send
 * @return ESP_OK on success, error code on failure
 */
esp_err_t api_send_telemetry_batch(const telemetry_batch_t *batch);

/**
 * @brief Send single telemetry record to API server
 * @param telemetry Pointer to telemetry data to send
 * @return ESP_OK on success, error code on failure
 */
esp_err_t api_send_telemetry_single(const telemetry_data_t *telemetry);

/**
 * @brief Test API connection by sending a health check
 * @return ESP_OK if API is reachable, error code otherwise
 */
esp_err_t api_test_connection(void);

/**
 * @brief Get current API connection status
 * @return Pointer to current API connection status
 */
const api_connection_t* api_get_connection_status(void);

/**
 * @brief Reset API connection statistics
 */
void api_reset_stats(void);

/**
 * @brief Enable or disable fallback mode
 * @param enable true to enable fallback, false to disable
 */
void api_set_fallback_mode(bool enable);

/**
 * @brief Check if API client is in fallback mode
 * @return true if in fallback mode, false otherwise
 */
bool api_is_fallback_mode(void);

/**
 * @brief Convert telemetry data to JSON string
 * @param telemetry Pointer to telemetry data
 * @param json_buffer Buffer to store JSON string
 * @param buffer_size Size of JSON buffer
 * @return ESP_OK on success, error code on failure
 */
esp_err_t api_telemetry_to_json(const telemetry_data_t *telemetry, char *json_buffer, size_t buffer_size);

/**
 * @brief Convert telemetry batch to JSON string
 * @param batch Pointer to telemetry batch
 * @param json_buffer Buffer to store JSON string
 * @param buffer_size Size of JSON buffer
 * @return ESP_OK on success, error code on failure
 */
esp_err_t api_batch_to_json(const telemetry_batch_t *batch, char *json_buffer, size_t buffer_size);

#endif // API_CLIENT_H
