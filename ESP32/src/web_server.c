/**
 * @file web_server.c
 * @brief Implementação do servidor web com listagem e download
 */

#include "web_server.h"
#include "data_logger.h"
#include "can_sniffer.h"
#include "obd_can.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include "esp_spiffs.h"
#include "pid_scheduler.h"

static const char *TAG = "WEB_SERVER";

static httpd_handle_t server = NULL;
static telemetry_data_t current_telemetry = {0};
static char device_ip_display[16] = "0.0.0.0";
static int current_state = 0;
static uint32_t current_records = 0;

// DTC data for web display
static dtc_data_t web_confirmed_dtcs = {0};
static dtc_data_t web_pending_dtcs = {0};

// Nomes dos estados para exibição (incluindo estados de sniffing)
static const char *STATE_NAMES[] = {
    "INIT", "IGN_OFF", "IGN_ON", "LOGGING", "ERROR",
    "SNIFF", "SNIFF_LOW", "HYBRID"
};

/**
 * @brief Handler: Página principal com lista de arquivos
 */
static esp_err_t root_handler(httpd_req_t *req) {
    char *html = malloc(16384);
    if (!html) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    // Header HTML
    int len = snprintf(html, 16384,
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<style>"
        "body{font-family:Arial;margin:20px;background:#f5f5f5}"
        ".header{background:#2196F3;color:white;padding:20px;border-radius:8px;margin-bottom:20px}"
        ".card{background:white;padding:20px;border-radius:8px;margin:10px 0;box-shadow:0 2px 5px rgba(0,0,0,0.1)}"
        ".file{padding:10px;border-bottom:1px solid #eee;display:flex;justify-content:space-between;align-items:center}"
        ".file:hover{background:#f9f9f9}"
        ".btn{padding:8px 15px;margin:3px;border:none;border-radius:5px;cursor:pointer;text-decoration:none;display:inline-block}"
        ".blue{background:#2196F3;color:white}"
        ".red{background:#f44336;color:white}"
        ".green{background:#4CAF50;color:white}"
        "h1{margin:0}h3{color:#333}.info{color:#ddd;font-size:14px}"
        "</style></head><body>"
        "<div class='header'><h1>🚗 OBD2 Smart Logger</h1>"
        "<div class='info'>IP: %s | Access: http://obd2logger.local</div></div>"
        "<div class='card'><h3>Status</h3>"
        "<p>State: <b>%s</b> | Records in session: <b>%lu</b><br>"
        "RPM: <b>%.0f</b> | Speed: <b>%d km/h</b> | Throttle: <b>%.1f%%</b></p></div>"
        "<div class='card'><h3>📁 Session Files</h3>",
        device_ip_display,
        STATE_NAMES[current_state],
        current_records,
        current_telemetry.rpm,
        current_telemetry.speed,
        current_telemetry.throttle_pos);

        // CALCULA ESPAÇO SPIFFS
        size_t total_spiffs = 0, used_spiffs = 0;
        esp_spiffs_info(NULL, &total_spiffs, &used_spiffs);
        float percent_used = (used_spiffs * 100.0) / total_spiffs;

        len += snprintf(html + len, 16384 - len,
            "<p style='background:%s;padding:10px;border-radius:5px;color:white;margin-bottom:15px'>"
            "💾 <b>Storage:</b> %.1f KB / %.1f KB (%.1f%% usado) - %.1f KB livres</p>",
            percent_used > 80 ? "#f44336" : "#4CAF50",
            used_spiffs / 1024.0,
            total_spiffs / 1024.0,
            percent_used,
            (total_spiffs - used_spiffs) / 1024.0);

    // Lista arquivos do SPIFFS
    DIR *dir = opendir("/spiffs");
    if (!dir) {
        len += snprintf(html + len, 16384 - len, "<p>Error opening directory</p>");
    } else {
        struct dirent *entry;
        struct stat st;
        char filepath[320];
        int file_count = 0;
        size_t total_size = 0;
        
        while ((entry = readdir(dir)) != NULL && len < 7500) {
            snprintf(filepath, sizeof(filepath), "/spiffs/%s", entry->d_name);
            
            if (stat(filepath, &st) == 0 && S_ISREG(st.st_mode)) {
                file_count++;
                total_size += st.st_size;
                
                len += snprintf(html + len, 16384 - len,
                    "<div class='file'>"
                    "<div><b>%s</b><br><span style='color:#999;font-size:12px'>%.2f KB</span></div>"
                    "<div>"
                    "<a href='/download?file=%s' class='btn blue'>Download</a>"
                    "<a href='/delete?file=%s' class='btn red' onclick='return confirm(\"Delete?\")'>Delete</a>"
                    "</div></div>",
                    entry->d_name, st.st_size / 1024.0,
                    entry->d_name, entry->d_name);
            }
        }
        closedir(dir);
        
        if (file_count == 0) {
            len += snprintf(html + len, 16384 - len, "<p>No files yet. Start driving!</p>");
        } else {
            len += snprintf(html + len, 16384 - len,
                "<p style='margin-top:20px;color:#666'><b>Total:</b> %d files, %.2f KB</p>",
                file_count, total_size / 1024.0);
        }
    }
    
    // Card de Sniffing CAN
    sniff_state_t sniff_state = can_sniffer_get_state();
    sniff_stats_t sniff_stats;
    can_sniffer_get_stats(&sniff_stats);
    
    len += snprintf(html + len, 16384 - len,
        "</div><div class='card'><h3>🔍 CAN Sniffer</h3>"
        "<p>Estado: <b style='color:%s'>%s</b></p>"
        "<p>Mensagens capturadas: <b>%lu</b> | IDs únicos: <b>%lu</b></p>",
        sniff_state == SNIFF_STATE_ACTIVE ? "#4CAF50" : 
        sniff_state == SNIFF_STATE_STORAGE_LOW ? "#ff9800" : "#666",
        can_sniffer_state_name(sniff_state),
        sniff_stats.messages_captured,
        sniff_stats.unique_ids);
    
    if (sniff_state == SNIFF_STATE_IDLE) {
        len += snprintf(html + len, 16384 - len,
            "<a href='/api/sniff/start' class='btn green'>▶️ Iniciar Sniffing</a>");
    } else if (sniff_state == SNIFF_STATE_ACTIVE || sniff_state == SNIFF_STATE_STORAGE_LOW) {
        len += snprintf(html + len, 16384 - len,
            "<a href='/api/sniff/stop' class='btn red'>⏹️ Parar Sniffing</a>");
    }
    
    // Card de DTCs (Diagnostic Trouble Codes)
    len += snprintf(html + len, 16384 - len,
        "</div><div class='card'><h3>🔧 Diagnostic Trouble Codes</h3>"
        "<p>MIL (Check Engine): <b style='color:%s'>%s</b></p>",
        web_confirmed_dtcs.mil_on ? "#f44336" : "#4CAF50",
        web_confirmed_dtcs.mil_on ? "ON ⚠️" : "OFF ✓");
    
    if (web_confirmed_dtcs.count > 0) {
        len += snprintf(html + len, 16384 - len,
            "<p><b style='color:#f44336'>Confirmed DTCs (%d):</b></p><ul style='margin:5px 0'>",
            web_confirmed_dtcs.count);
        for (int i = 0; i < web_confirmed_dtcs.count && i < MAX_DTCS && len < 15000; i++) {
            const char* system_names[] = {"Powertrain", "Chassis", "Body", "Network"};
            len += snprintf(html + len, 16384 - len,
                "<li><b>%s</b> (%s)</li>",
                web_confirmed_dtcs.codes[i].code_str,
                system_names[web_confirmed_dtcs.codes[i].system]);
        }
        len += snprintf(html + len, 16384 - len, "</ul>");
    }
    
    if (web_pending_dtcs.count > 0) {
        len += snprintf(html + len, 16384 - len,
            "<p><b style='color:#ff9800'>Pending DTCs (%d):</b></p><ul style='margin:5px 0'>",
            web_pending_dtcs.count);
        for (int i = 0; i < web_pending_dtcs.count && i < MAX_DTCS && len < 15500; i++) {
            len += snprintf(html + len, 16384 - len,
                "<li>%s</li>", web_pending_dtcs.codes[i].code_str);
        }
        len += snprintf(html + len, 16384 - len, "</ul>");
    }
    
    if (web_confirmed_dtcs.count == 0 && web_pending_dtcs.count == 0) {
        len += snprintf(html + len, 16384 - len,
            "<p style='color:#4CAF50'>✓ No trouble codes found</p>");
    }
    
    if (web_confirmed_dtcs.count > 0 || web_pending_dtcs.count > 0) {
        len += snprintf(html + len, 16384 - len,
            "<a href='/api/dtc/clear' class='btn red' onclick='return confirm(\"Clear all DTCs? This will turn off the Check Engine light.\")'>🗑 Clear DTCs</a>");
    }
    
    // Botões de ação
    len += snprintf(html + len, 16384 - len,
        "</div><div class='card'><h3>Actions</h3>"
        "<a href='/download-all' class='btn green'>📥 Download All Files</a>"
        "<button class='btn red' onclick=\"if(confirm('Delete ALL files?'))location.href='/delete-all'\">🗑 Delete All</button>"
        "</div>"
        "<script>setTimeout(()=>location.reload(),10000)</script>"
        "</body></html>");
    
    httpd_resp_send(req, html, len);
    free(html);
    return ESP_OK;
}

/**
 * @brief Handler: Download de arquivo específico
 */
static esp_err_t download_handler(httpd_req_t *req) {
    char filename[320] = {0};
    
    // Extrai parâmetro "file" da query string
    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        char *buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char param[256];
            if (httpd_query_key_value(buf, "file", param, sizeof(param)) == ESP_OK) {
                snprintf(filename, sizeof(filename), "/spiffs/%s", param);
            }
        }
        free(buf);
    }
    
    if (strlen(filename) == 0) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    
    FILE *f = fopen(filename, "r");
    if (!f) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "text/csv");
    
    char disposition[384];
    snprintf(disposition, sizeof(disposition), "attachment; filename=%s", 
             strrchr(filename, '/') + 1);
    httpd_resp_set_hdr(req, "Content-Disposition", disposition);
    
    char buffer[512];
    size_t read;
    while ((read = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        httpd_resp_send_chunk(req, buffer, read);
    }
    
    httpd_resp_send_chunk(req, NULL, 0);
    fclose(f);
    
    ESP_LOGI(TAG, "Downloaded: %s", filename);
    return ESP_OK;
}

/**
 * @brief Handler: Deletar arquivo
 */
static esp_err_t delete_file_handler(httpd_req_t *req) {
    char filename[320] = {0};
    
    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        char *buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char param[256];
            if (httpd_query_key_value(buf, "file", param, sizeof(param)) == ESP_OK) {
                snprintf(filename, sizeof(filename), "/spiffs/%s", param);
                remove(filename);
                ESP_LOGI(TAG, "Deleted: %s", filename);
            }
        }
        free(buf);
    }
    
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/**
 * @brief Handler: Deletar todos os arquivos
 */
static esp_err_t delete_all_handler(httpd_req_t *req) {
    DIR *dir = opendir("/spiffs");
    if (dir) {
        struct dirent *entry;
        char filepath[320];
        
        while ((entry = readdir(dir)) != NULL) {
            snprintf(filepath, sizeof(filepath), "/spiffs/%s", entry->d_name);
            struct stat st;
            if (stat(filepath, &st) == 0 && S_ISREG(st.st_mode)) {
                remove(filepath);
            }
        }
        closedir(dir);
    }
    
    ESP_LOGI(TAG, "All files deleted");
    
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/**
 * @brief Handler: Download all (concatena todos os CSVs)
 */
static esp_err_t download_all_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=all_sessions.csv");
    
    DIR *dir = opendir("/spiffs");
    if (!dir) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    
    struct dirent *entry;
    char filepath[320];
    bool first_file = true;
    
    while ((entry = readdir(dir)) != NULL) {
        snprintf(filepath, sizeof(filepath), "/spiffs/%s", entry->d_name);
        
        struct stat st;
        if (stat(filepath, &st) == 0 && S_ISREG(st.st_mode)) {
            FILE *f = fopen(filepath, "r");
            if (f) {
                char buffer[512];
                bool skip_header = !first_file;
                
                while (fgets(buffer, sizeof(buffer), f)) {
                    if (skip_header && strncmp(buffer, "timestamp", 9) == 0) {
                        skip_header = false;
                        continue;
                    }
                    httpd_resp_send_chunk(req, buffer, strlen(buffer));
                }
                
                fclose(f);
                first_file = false;
            }
        }
    }
    
    closedir(dir);
    httpd_resp_send_chunk(req, NULL, 0);
    
    ESP_LOGI(TAG, "Downloaded all files");
    return ESP_OK;
}

// ============================================================================
// SNIFFER API HANDLERS
// ============================================================================

/**
 * @brief Handler: Start CAN sniffing
 * GET /api/sniff/start?exclude_obd=1&id_min=0x000&id_max=0x7FF
 */
static esp_err_t sniff_start_handler(httpd_req_t *req) {
    sniff_filter_t filter = {
        .id_min = 0x000,
        .id_max = 0x7FF,
        .exclude_obd_requests = true,
        .exclude_obd_responses = false
    };
    
    // Parse query parameters
    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        char *buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char param[32];
            if (httpd_query_key_value(buf, "exclude_obd", param, sizeof(param)) == ESP_OK) {
                filter.exclude_obd_responses = (atoi(param) == 1);
            }
            if (httpd_query_key_value(buf, "id_min", param, sizeof(param)) == ESP_OK) {
                filter.id_min = strtol(param, NULL, 0);
            }
            if (httpd_query_key_value(buf, "id_max", param, sizeof(param)) == ESP_OK) {
                filter.id_max = strtol(param, NULL, 0);
            }
        }
        free(buf);
    }
    
    // Switch CAN to hybrid mode if not already
    if (obd_can_get_mode() == CAN_MODE_OBD_ONLY) {
        obd_can_set_mode(CAN_MODE_HYBRID);
    }
    
    // Start sniffer
    esp_err_t ret = can_sniffer_start(&filter);
    
    // Redirect back to main page for better UX
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Sniffer started via web API");
    }
    
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    
    return ESP_OK;
}

/**
 * @brief Handler: Stop CAN sniffing
 * GET /api/sniff/stop
 */
static esp_err_t sniff_stop_handler(httpd_req_t *req) {
    (void)can_sniffer_stop();  // Ignore return value
    
    // Return to OBD-only mode
    obd_can_set_mode(CAN_MODE_OBD_ONLY);
    
    ESP_LOGI(TAG, "Sniffer stopped via web API");
    
    // Redirect back to main page
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    
    return ESP_OK;
}

/**
 * @brief Handler: Get sniffer status
 * GET /api/sniff/status
 */
static esp_err_t sniff_status_handler(httpd_req_t *req) {
    sniff_stats_t stats;
    can_sniffer_get_stats(&stats);
    sniff_state_t state = can_sniffer_get_state();
    
    char log_path[64] = "";
    can_sniffer_get_log_path(log_path, sizeof(log_path));
    
    httpd_resp_set_type(req, "application/json");
    
    char response[768];
    snprintf(response, sizeof(response),
             "{"
             "\"state\":\"%s\","
             "\"can_mode\":\"%s\","
             "\"messages_captured\":%lu,"
             "\"messages_logged\":%lu,"
             "\"unique_ids\":%lu,"
             "\"bytes_written\":%u,"
             "\"buffer_overflows\":%lu,"
             "\"storage_free_kb\":%.1f,"
             "\"storage_percent_used\":%.1f,"
             "\"current_file\":\"%s\""
             "}",
             can_sniffer_state_name(state),
             obd_can_get_mode() == CAN_MODE_OBD_ONLY ? "OBD_ONLY" :
             obd_can_get_mode() == CAN_MODE_SNIFF_ONLY ? "SNIFF_ONLY" : "HYBRID",
             stats.messages_captured,
             stats.messages_logged,
             stats.unique_ids,
             (unsigned)stats.bytes_written,
             stats.buffer_overflows,
             stats.storage_free / 1024.0f,
             stats.storage_percent_used,
             log_path);
    
    httpd_resp_sendstr(req, response);
    return ESP_OK;
}

/**
 * @brief Handler: Get DTC status
 * GET /api/dtc/status
 */
static esp_err_t dtc_status_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    
    char response[1024];
    int len = snprintf(response, sizeof(response),
             "{"
             "\"mil_on\":%s,"
             "\"confirmed_count\":%d,"
             "\"pending_count\":%d,"
             "\"confirmed\":[",
             web_confirmed_dtcs.mil_on ? "true" : "false",
             web_confirmed_dtcs.count,
             web_pending_dtcs.count);
    
    // Add confirmed DTCs
    for (int i = 0; i < web_confirmed_dtcs.count && i < MAX_DTCS; i++) {
        if (i > 0) len += snprintf(response + len, sizeof(response) - len, ",");
        len += snprintf(response + len, sizeof(response) - len,
                       "\"%s\"", web_confirmed_dtcs.codes[i].code_str);
    }
    
    len += snprintf(response + len, sizeof(response) - len, "],\"pending\":[");
    
    // Add pending DTCs
    for (int i = 0; i < web_pending_dtcs.count && i < MAX_DTCS; i++) {
        if (i > 0) len += snprintf(response + len, sizeof(response) - len, ",");
        len += snprintf(response + len, sizeof(response) - len,
                       "\"%s\"", web_pending_dtcs.codes[i].code_str);
    }
    
    len += snprintf(response + len, sizeof(response) - len, "]}");
    
    httpd_resp_sendstr(req, response);
    return ESP_OK;
}

/**
 * @brief Handler: Clear all DTCs
 * GET /api/dtc/clear
 */
static esp_err_t dtc_clear_handler(httpd_req_t *req) {
    esp_err_t ret = pid_scheduler_clear_dtcs();
    
    if (ret == ESP_OK) {
        // Clear local copies
        memset(&web_confirmed_dtcs, 0, sizeof(dtc_data_t));
        memset(&web_pending_dtcs, 0, sizeof(dtc_data_t));
        ESP_LOGW(TAG, "DTCs cleared via web API");
    }
    
    httpd_resp_set_type(req, "application/json");
    
    char response[128];
    snprintf(response, sizeof(response),
             "{\"success\":%s,\"message\":\"%s\"}",
             ret == ESP_OK ? "true" : "false",
             ret == ESP_OK ? "DTCs cleared successfully" : "Failed to clear DTCs");
    
    httpd_resp_sendstr(req, response);
    return ESP_OK;
}

// ============================================================================
// TELEMETRY & STATUS API HANDLERS (JSON)
// ============================================================================

// Extended telemetry storage for API
static extended_telemetry_t web_ext_telemetry = {0};

/**
 * @brief Handler: Get extended telemetry JSON
 * GET /api/telemetry
 */
static esp_err_t telemetry_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    
    char response[1536];
    snprintf(response, sizeof(response),
        "{"
        "\"timestamp_us\":%llu,"
        "\"tier1\":{\"rpm\":%.1f,\"speed\":%d,\"throttle\":%.1f,\"load\":%.1f,\"maf\":%.2f},"
        "\"tier2\":{\"coolant\":%d,\"manifold\":%d,\"fuel_level\":%.1f,\"intake_temp\":%d,\"runtime\":%d},"
        "\"tier3\":{\"voltage\":%.2f,\"oil_temp\":%d,\"fuel_trim_short\":%.1f,\"fuel_trim_long\":%.1f,"
                   "\"distance\":%d,\"ambient\":%d,\"timing\":%.1f},"
        "\"tier4\":{\"mil_status\":%d,\"dtc_count\":%d,\"pending_dtc\":%d,\"o2_b1s1\":%.2f,\"o2_b1s2\":%.2f},"
        "\"valid_mask\":%lu"
        "}",
        web_ext_telemetry.timestamp_us,
        web_ext_telemetry.rpm, web_ext_telemetry.speed, web_ext_telemetry.throttle_pos,
        web_ext_telemetry.engine_load, web_ext_telemetry.maf_rate,
        web_ext_telemetry.coolant_temp, web_ext_telemetry.manifold_pressure,
        web_ext_telemetry.fuel_level, web_ext_telemetry.intake_air_temp, web_ext_telemetry.run_time,
        web_ext_telemetry.control_voltage, web_ext_telemetry.oil_temp,
        web_ext_telemetry.fuel_trim_short_b1, web_ext_telemetry.fuel_trim_long_b1,
        web_ext_telemetry.distance_since_clear, web_ext_telemetry.ambient_temp, web_ext_telemetry.timing_advance,
        web_ext_telemetry.mil_status, web_ext_telemetry.dtc_count, web_ext_telemetry.pending_dtc_count,
        web_ext_telemetry.o2_voltage_b1s1, web_ext_telemetry.o2_voltage_b1s2,
        web_ext_telemetry.valid_mask);
    
    httpd_resp_sendstr(req, response);
    return ESP_OK;
}

/**
 * @brief Handler: Get system status JSON
 * GET /api/status
 */
static esp_err_t status_handler(httpd_req_t *req) {
    size_t total_spiffs = 0, used_spiffs = 0;
    esp_spiffs_info(NULL, &total_spiffs, &used_spiffs);
    
    sniff_state_t sniff_state = can_sniffer_get_state();
    
    httpd_resp_set_type(req, "application/json");
    
    char response[512];
    snprintf(response, sizeof(response),
        "{"
        "\"state\":\"%s\","
        "\"state_code\":%d,"
        "\"records\":%lu,"
        "\"heap_free\":%lu,"
        "\"storage_total\":%u,"
        "\"storage_used\":%u,"
        "\"storage_free\":%u,"
        "\"sniff_state\":\"%s\","
        "\"uptime_ms\":%llu"
        "}",
        STATE_NAMES[current_state],
        current_state,
        current_records,
        esp_get_free_heap_size(),
        (unsigned)total_spiffs,
        (unsigned)used_spiffs,
        (unsigned)(total_spiffs - used_spiffs),
        can_sniffer_state_name(sniff_state),
        esp_timer_get_time() / 1000);
    
    httpd_resp_sendstr(req, response);
    return ESP_OK;
}

/**
 * @brief Handler: Get files list JSON
 * GET /api/files
 */
static esp_err_t files_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    
    char *response = malloc(4096);
    if (!response) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    int len = snprintf(response, 4096, "{\"files\":[");
    
    DIR *dir = opendir("/spiffs");
    if (dir) {
        struct dirent *entry;
        struct stat st;
        char filepath[320];
        bool first = true;
        
        while ((entry = readdir(dir)) != NULL && len < 3800) {
            snprintf(filepath, sizeof(filepath), "/spiffs/%s", entry->d_name);
            
            if (stat(filepath, &st) == 0 && S_ISREG(st.st_mode)) {
                if (!first) len += snprintf(response + len, 4096 - len, ",");
                first = false;
                
                len += snprintf(response + len, 4096 - len,
                    "{\"name\":\"%s\",\"size\":%ld}",
                    entry->d_name, st.st_size);
            }
        }
        closedir(dir);
    }
    
    len += snprintf(response + len, 4096 - len, "]}");
    
    httpd_resp_sendstr(req, response);
    free(response);
    return ESP_OK;
}

/**
 * @brief Handler: Get PID configuration JSON
 * GET /api/config/pids
 */
static esp_err_t pids_config_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    
    char *response = malloc(4096);
    if (!response) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    int len = snprintf(response, 4096, 
        "{\"tiers\":[{\"name\":\"CRITICAL\",\"interval_ms\":%d,\"pids\":[",
        TIER_CRITICAL_INTERVAL_MS);
    
    // Add PIDs for each tier
    for (int tier = 0; tier < POLL_TIER_COUNT && len < 3500; tier++) {
        uint8_t count = 0;
        const pid_config_t* pids = pid_scheduler_get_tier_pids((poll_tier_t)tier, &count);
        
        if (tier > 0) {
            const char* tier_names[] = {"CRITICAL", "HIGH", "MEDIUM", "LOW"};
            const int tier_intervals[] = {TIER_CRITICAL_INTERVAL_MS, TIER_HIGH_INTERVAL_MS, 
                                          TIER_MEDIUM_INTERVAL_MS, TIER_LOW_INTERVAL_MS};
            len += snprintf(response + len, 4096 - len,
                "]},{\"name\":\"%s\",\"interval_ms\":%d,\"pids\":[",
                tier_names[tier], tier_intervals[tier]);
        }
        
        for (int i = 0; i < count && len < 3700; i++) {
            if (i > 0) len += snprintf(response + len, 4096 - len, ",");
            len += snprintf(response + len, 4096 - len,
                "{\"pid\":\"0x%02X\",\"name\":\"%s\",\"unit\":\"%s\",\"enabled\":true}",
                pids[i].pid, pids[i].name, pids[i].unit);
        }
    }
    
    len += snprintf(response + len, 4096 - len, "]}]}");
    
    httpd_resp_sendstr(req, response);
    free(response);
    return ESP_OK;
}

esp_err_t web_server_start(const char *device_ip) {
    if (device_ip) {
        strncpy(device_ip_display, device_ip, sizeof(device_ip_display) - 1);
    }
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 16384;
    config.max_uri_handlers = 20;  // Increased for sniffer + DTC endpoints
    config.lru_purge_enable = true;
    
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }
    
    // Registra handlers - páginas principais
    httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = root_handler};
    httpd_uri_t download = {.uri = "/download", .method = HTTP_GET, .handler = download_handler};
    httpd_uri_t delete_file = {.uri = "/delete", .method = HTTP_GET, .handler = delete_file_handler};
    httpd_uri_t delete_all = {.uri = "/delete-all", .method = HTTP_GET, .handler = delete_all_handler};
    httpd_uri_t download_all = {.uri = "/download-all", .method = HTTP_GET, .handler = download_all_handler};
    
    // Registra handlers - API de sniffing
    httpd_uri_t sniff_start = {.uri = "/api/sniff/start", .method = HTTP_GET, .handler = sniff_start_handler};
    httpd_uri_t sniff_stop = {.uri = "/api/sniff/stop", .method = HTTP_GET, .handler = sniff_stop_handler};
    httpd_uri_t sniff_status = {.uri = "/api/sniff/status", .method = HTTP_GET, .handler = sniff_status_handler};
    
    // Registra handlers - API de DTCs
    httpd_uri_t dtc_status = {.uri = "/api/dtc/status", .method = HTTP_GET, .handler = dtc_status_handler};
    httpd_uri_t dtc_clear = {.uri = "/api/dtc/clear", .method = HTTP_GET, .handler = dtc_clear_handler};
    
    // Registra handlers - API JSON (telemetry, status, files, config)
    httpd_uri_t telemetry = {.uri = "/api/telemetry", .method = HTTP_GET, .handler = telemetry_handler};
    httpd_uri_t status = {.uri = "/api/status", .method = HTTP_GET, .handler = status_handler};
    httpd_uri_t files = {.uri = "/api/files", .method = HTTP_GET, .handler = files_handler};
    httpd_uri_t pids_config = {.uri = "/api/config/pids", .method = HTTP_GET, .handler = pids_config_get_handler};
    
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &download);
    httpd_register_uri_handler(server, &delete_file);
    httpd_register_uri_handler(server, &delete_all);
    httpd_register_uri_handler(server, &download_all);
    httpd_register_uri_handler(server, &sniff_start);
    httpd_register_uri_handler(server, &sniff_stop);
    httpd_register_uri_handler(server, &sniff_status);
    httpd_register_uri_handler(server, &dtc_status);
    httpd_register_uri_handler(server, &dtc_clear);
    httpd_register_uri_handler(server, &telemetry);
    httpd_register_uri_handler(server, &status);
    httpd_register_uri_handler(server, &files);
    httpd_register_uri_handler(server, &pids_config);
    
    ESP_LOGI(TAG, "Web server started with full JSON API");
    return ESP_OK;
}

esp_err_t web_server_stop(void) {
    if (server) {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "Web server stopped");
    }
    return ESP_OK;
}

void web_server_update_telemetry(const telemetry_data_t *data) {
    if (data) {
        memcpy(&current_telemetry, data, sizeof(telemetry_data_t));
    }
}

void web_server_update_state(int state, uint32_t records_count) {
    if (state >= 0 && state <= 7) {  // Updated for all states including sniffing
        current_state = state;
    }
    current_records = records_count;
}

void web_server_update_dtcs(const dtc_data_t *confirmed, const dtc_data_t *pending) {
    if (confirmed) {
        memcpy(&web_confirmed_dtcs, confirmed, sizeof(dtc_data_t));
    }
    if (pending) {
        memcpy(&web_pending_dtcs, pending, sizeof(dtc_data_t));
    }
}

void web_server_update_ext_telemetry(const extended_telemetry_t *data) {
    if (data) {
        memcpy(&web_ext_telemetry, data, sizeof(extended_telemetry_t));
    }
}