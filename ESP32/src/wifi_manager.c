/**
 * @file wifi_manager.c
 * @brief WiFi connection management implementation
 * @date 2025
 */

#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include <string.h>

static const char *TAG = "WIFI_MANAGER";

// Event group bits
#define WIFI_CONNECTED_BIT      BIT0
#define WIFI_FAIL_BIT          BIT1

static EventGroupHandle_t wifi_event_group;
static esp_netif_t *netif_sta = NULL;
static wifi_connection_t wifi_status = {0};
static bool wifi_initialized = false;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data)
{
    static int retry_num = 0;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi station started, connecting...");
                wifi_status.status = WIFI_STATUS_CONNECTING;
                esp_wifi_connect();
                break;

            case WIFI_EVENT_STA_DISCONNECTED:
                wifi_event_sta_disconnected_t* disconnected = (wifi_event_sta_disconnected_t*) event_data;
                ESP_LOGW(TAG, "WiFi disconnected (reason: %d)", disconnected->reason);
                
                wifi_status.status = WIFI_STATUS_DISCONNECTED;
                wifi_status.total_disconnections++;
                
                if (wifi_status.auto_reconnect_enabled && retry_num < WIFI_MAX_RETRY) {
                    wifi_status.status = WIFI_STATUS_RECONNECTING;
                    wifi_status.retry_count = retry_num;
                    retry_num++;
                    
                    ESP_LOGI(TAG, "Retrying WiFi connection (%d/%d)", retry_num, WIFI_MAX_RETRY);
                    vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_DELAY));
                    esp_wifi_connect();
                } else {
                    wifi_status.status = WIFI_STATUS_ERROR;
                    xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
                    ESP_LOGE(TAG, "WiFi connection failed after %d attempts", WIFI_MAX_RETRY);
                }
                break;

            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "WiFi station connected");
                retry_num = 0;
                wifi_status.retry_count = 0;
                break;

            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP:
                ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
                ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
                
                wifi_status.status = WIFI_STATUS_CONNECTED;
                wifi_status.ip_address = event->ip_info.ip.addr;
                wifi_status.last_connected = esp_timer_get_time() / 1000;
                
                // Get RSSI
                wifi_ap_record_t ap_info;
                if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                    wifi_status.rssi = ap_info.rssi;
                }
                
                xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
                break;

            default:
                break;
        }
    }
}

esp_err_t wifi_manager_init(void)
{
    if (wifi_initialized) {
        ESP_LOGW(TAG, "WiFi manager already initialized");
        return ESP_OK;
    }

    esp_err_t ret = ESP_OK;

    // Initialize networking
    ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize netif: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(ret));
        return ret;
    }

    // Create WiFi station netif
    netif_sta = esp_netif_create_default_wifi_sta();
    if (netif_sta == NULL) {
        ESP_LOGE(TAG, "Failed to create WiFi station netif");
        return ESP_FAIL;
    }

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(ret));
        return ret;
    }

    // Create event group
    wifi_event_group = xEventGroupCreate();
    if (wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create WiFi event group");
        esp_wifi_deinit();
        return ESP_FAIL;
    }

    // Register event handlers
    ret = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WiFi event handler: %s", esp_err_to_name(ret));
        vEventGroupDelete(wifi_event_group);
        esp_wifi_deinit();
        return ret;
    }

    ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP event handler: %s", esp_err_to_name(ret));
        esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
        vEventGroupDelete(wifi_event_group);
        esp_wifi_deinit();
        return ret;
    }

    // Set WiFi mode
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi mode: %s", esp_err_to_name(ret));
        esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler);
        esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
        vEventGroupDelete(wifi_event_group);
        esp_wifi_deinit();
        return ret;
    }

    // Initialize status
    wifi_status.status = WIFI_STATUS_DISCONNECTED;
    wifi_status.rssi = 0;
    wifi_status.ip_address = 0;
    wifi_status.retry_count = 0;
    wifi_status.last_connected = 0;
    wifi_status.total_disconnections = 0;
    wifi_status.auto_reconnect_enabled = true;

    wifi_initialized = true;
    ESP_LOGI(TAG, "WiFi manager initialized successfully");
    
    return ESP_OK;
}

void wifi_manager_deinit(void)
{
    if (!wifi_initialized) {
        return;
    }

    // Stop WiFi
    esp_wifi_stop();
    
    // Unregister event handlers
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler);
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
    
    // Clean up
    vEventGroupDelete(wifi_event_group);
    esp_wifi_deinit();
    
    wifi_initialized = false;
    ESP_LOGI(TAG, "WiFi manager deinitialized");
}

esp_err_t wifi_connect(const char *ssid, const char *password)
{
    if (!wifi_initialized || ssid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK;
    wifi_config_t wifi_config = {0};

    // Configure WiFi
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password != NULL) {
        strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }

    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi config: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Connecting to WiFi network: %s", ssid);
    return ESP_OK;
}

esp_err_t wifi_disconnect(void)
{
    if (!wifi_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_status.auto_reconnect_enabled = false;
    esp_err_t ret = esp_wifi_disconnect();
    if (ret == ESP_OK) {
        wifi_status.status = WIFI_STATUS_DISCONNECTED;
        ESP_LOGI(TAG, "WiFi disconnected");
    }
    
    return ret;
}

const wifi_connection_t* wifi_get_status(void)
{
    return &wifi_status;
}

bool wifi_is_connected(void)
{
    return wifi_status.status == WIFI_STATUS_CONNECTED;
}

void wifi_set_auto_reconnect(bool enable)
{
    wifi_status.auto_reconnect_enabled = enable;
    ESP_LOGI(TAG, "Auto-reconnect %s", enable ? "enabled" : "disabled");
}

int wifi_get_rssi(void)
{
    if (!wifi_is_connected()) {
        return 0;
    }

    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        wifi_status.rssi = ap_info.rssi;
        return ap_info.rssi;
    }
    
    return wifi_status.rssi;
}

esp_err_t wifi_get_ip_string(char *ip_str, size_t max_len)
{
    if (ip_str == NULL || max_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!wifi_is_connected()) {
        strncpy(ip_str, "0.0.0.0", max_len - 1);
        ip_str[max_len - 1] = '\0';
        return ESP_ERR_INVALID_STATE;
    }

    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(netif_sta, &ip_info);
    if (ret == ESP_OK) {
        snprintf(ip_str, max_len, IPSTR, IP2STR(&ip_info.ip));
    } else {
        strncpy(ip_str, "0.0.0.0", max_len - 1);
        ip_str[max_len - 1] = '\0';
    }

    return ret;
}

esp_err_t wifi_wait_for_connection(uint32_t timeout_ms)
{
    if (!wifi_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                          WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                          pdFALSE,
                                          pdFALSE,
                                          pdMS_TO_TICKS(timeout_ms));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi network");
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to WiFi network");
        return ESP_FAIL;
    } else {
        ESP_LOGW(TAG, "WiFi connection timeout");
        return ESP_ERR_TIMEOUT;
    }
}

void wifi_reset_stats(void)
{
    wifi_status.retry_count = 0;
    wifi_status.total_disconnections = 0;
    wifi_status.last_connected = 0;
}
