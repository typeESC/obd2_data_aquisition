/**
 * @file wifi_manager.h
 * @brief WiFi connection management for ESP32
 * @author Mario Venere Neto
 * @date 2025
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "obd_config.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"

typedef enum {
    WIFI_STATUS_DISCONNECTED = 0,
    WIFI_STATUS_CONNECTING,
    WIFI_STATUS_CONNECTED,
    WIFI_STATUS_ERROR,
    WIFI_STATUS_RECONNECTING
} wifi_status_t;

typedef struct {
    wifi_status_t status;
    int rssi;
    uint32_t ip_address;
    int retry_count;
    uint64_t last_connected;
    uint64_t total_disconnections;
    bool auto_reconnect_enabled;
} wifi_connection_t;

/**
 * @brief Initialize WiFi manager
 * @return ESP_OK on success, error code on failure
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Deinitialize WiFi manager
 */
void wifi_manager_deinit(void);

/**
 * @brief Connect to WiFi network
 * @param ssid WiFi network SSID
 * @param password WiFi network password
 * @return ESP_OK on success, error code on failure
 */
esp_err_t wifi_connect(const char *ssid, const char *password);

/**
 * @brief Disconnect from WiFi network
 * @return ESP_OK on success, error code on failure
 */
esp_err_t wifi_disconnect(void);

/**
 * @brief Get current WiFi connection status
 * @return Pointer to WiFi connection status
 */
const wifi_connection_t* wifi_get_status(void);

/**
 * @brief Check if WiFi is connected
 * @return true if connected, false otherwise
 */
bool wifi_is_connected(void);

/**
 * @brief Enable or disable auto-reconnect
 * @param enable true to enable auto-reconnect, false to disable
 */
void wifi_set_auto_reconnect(bool enable);

/**
 * @brief Get WiFi signal strength (RSSI)
 * @return RSSI value in dBm, or 0 if not connected
 */
int wifi_get_rssi(void);

/**
 * @brief Get IP address as string
 * @param ip_str Buffer to store IP address string
 * @param max_len Maximum length of buffer
 * @return ESP_OK on success, error code on failure
 */
esp_err_t wifi_get_ip_string(char *ip_str, size_t max_len);

/**
 * @brief Wait for WiFi connection with timeout
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK if connected within timeout, ESP_ERR_TIMEOUT otherwise
 */
esp_err_t wifi_wait_for_connection(uint32_t timeout_ms);

/**
 * @brief Reset WiFi statistics
 */
void wifi_reset_stats(void);

#endif // WIFI_MANAGER_H
