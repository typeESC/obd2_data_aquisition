/**
 * @file web_server.c
 * @brief Implementação do servidor web com listagem e download
 */

#include "web_server.h"
#include "data_logger.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include "esp_spiffs.h" 

static const char *TAG = "WEB_SERVER";

static httpd_handle_t server = NULL;
static telemetry_data_t current_telemetry = {0};
static char device_ip_display[16] = "0.0.0.0";
static int current_state = 0;
static uint32_t current_records = 0;

// Nomes dos estados para exibição
static const char *STATE_NAMES[] = {"INIT", "IGN_OFF", "IGN_ON", "LOGGING", "ERROR"};

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

esp_err_t web_server_start(const char *device_ip) {
    if (device_ip) {
        strncpy(device_ip_display, device_ip, sizeof(device_ip_display) - 1);
    }
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 16384;
    config.max_uri_handlers = 10;
    config.lru_purge_enable = true;
    
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }
    
    // Registra handlers
    httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = root_handler};
    httpd_uri_t download = {.uri = "/download", .method = HTTP_GET, .handler = download_handler};
    httpd_uri_t delete_file = {.uri = "/delete", .method = HTTP_GET, .handler = delete_file_handler};
    httpd_uri_t delete_all = {.uri = "/delete-all", .method = HTTP_GET, .handler = delete_all_handler};
    httpd_uri_t download_all = {.uri = "/download-all", .method = HTTP_GET, .handler = download_all_handler};
    
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &download);
    httpd_register_uri_handler(server, &delete_file);
    httpd_register_uri_handler(server, &delete_all);
    httpd_register_uri_handler(server, &download_all);
    
    ESP_LOGI(TAG, "Web server started");
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
    if (state >= 0 && state <= 4) {
        current_state = state;
    }
    current_records = records_count;
}