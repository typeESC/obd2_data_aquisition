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
                
                // Log detalhado do motivo
                const char *reason_str = "Unknown";
                switch(disconnected->reason) {
                    case WIFI_REASON_AUTH_EXPIRE: reason_str = "Auth expired"; break;
                    case WIFI_REASON_AUTH_LEAVE: reason_str = "Auth leave"; break;
                    case WIFI_REASON_ASSOC_EXPIRE: reason_str = "Assoc expired"; break;
                    case WIFI_REASON_ASSOC_TOOMANY: reason_str = "Too many connections"; break;
                    case WIFI_REASON_NOT_AUTHED: reason_str = "Not authenticated"; break;
                    case WIFI_REASON_NOT_ASSOCED: reason_str = "Not associated"; break;
                    case WIFI_REASON_ASSOC_LEAVE: reason_str = "Assoc leave"; break;
                    case WIFI_REASON_ASSOC_NOT_AUTHED: reason_str = "Assoc not authed"; break;
                    case WIFI_REASON_DISASSOC_PWRCAP_BAD: reason_str = "Bad power cap"; break;
                    case WIFI_REASON_DISASSOC_SUPCHAN_BAD: reason_str = "Bad channel"; break;
                    case WIFI_REASON_IE_INVALID: reason_str = "Invalid IE"; break;
                    case WIFI_REASON_MIC_FAILURE: reason_str = "MIC failure"; break;
                    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: reason_str = "4-way handshake timeout (check password)"; break;
                    case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT: reason_str = "Group key timeout"; break;
                    case WIFI_REASON_IE_IN_4WAY_DIFFERS: reason_str = "IE differs in 4-way"; break;
                    case WIFI_REASON_GROUP_CIPHER_INVALID: reason_str = "Invalid group cipher"; break;
                    case WIFI_REASON_PAIRWISE_CIPHER_INVALID: reason_str = "Invalid pairwise cipher"; break;
                    case WIFI_REASON_AKMP_INVALID: reason_str = "Invalid AKMP"; break;
                    case WIFI_REASON_UNSUPP_RSN_IE_VERSION: reason_str = "Unsupported RSN version"; break;
                    case WIFI_REASON_INVALID_RSN_IE_CAP: reason_str = "Invalid RSN cap"; break;
                    case WIFI_REASON_802_1X_AUTH_FAILED: reason_str = "802.1X auth failed"; break;
                    case WIFI_REASON_CIPHER_SUITE_REJECTED: reason_str = "Cipher suite rejected"; break;
                    case WIFI_REASON_BEACON_TIMEOUT: reason_str = "Beacon timeout"; break;
                    case WIFI_REASON_NO_AP_FOUND: reason_str = "AP not found"; break;
                    case WIFI_REASON_AUTH_FAIL: reason_str = "Authentication failed (wrong password?)"; break;
                    case WIFI_REASON_ASSOC_FAIL: reason_str = "Association failed"; break;
                    case WIFI_REASON_HANDSHAKE_TIMEOUT: reason_str = "Handshake timeout"; break;
                    case WIFI_REASON_CONNECTION_FAIL: reason_str = "Connection failed"; break;
                }
                
                ESP_LOGW(TAG, "WiFi disconnected - Reason %d: %s", disconnected->reason, reason_str);
                
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

    // Aceita WPA, WPA2 e WPA3 - compatibilidade máxima
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

    // Debug: mostra configuração (sem senha)
    ESP_LOGI(TAG, "WiFi Config:");
    ESP_LOGI(TAG, "  SSID: %s (len=%d)", wifi_config.sta.ssid, strlen((char*)wifi_config.sta.ssid));
    ESP_LOGI(TAG, "  Password: %s (len=%d)", password ? "***" : "NONE", 
             password ? strlen(password) : 0);
    ESP_LOGI(TAG, "  Auth mode: WPA/WPA2/WPA3");

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
