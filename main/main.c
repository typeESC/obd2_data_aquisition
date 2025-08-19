#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/twai.h"
#include "esp_spiffs.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_timer.h" // Adicionado para esp_log_timestamp

// --- DEFINES E GLOBAIS ---
#define WIFI_SSID      "S20_Estevan" // <-- Lembre-se de alterar
#define WIFI_PASSWORD  "123456789" // <-- Lembre-se de alterar
#define TAG "OBD_LOGGER"

const gpio_num_t CAN_RX_PIN = GPIO_NUM_27;
const gpio_num_t CAN_TX_PIN = GPIO_NUM_25;

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static int s_retry_num = 0;
char ip_address_str[16] = "0.0.0.0";

// --- ATUALIZADO: Novo cabeçalho do CSV com a lista de PIDs refinada ---
const char* CSV_HEADER = "Timestamp,RPM,Velocidade,TempAgua,CargaMotor,AvancoIgnicao,TempArAdmissao,MAF,PosAcelerador,TempoMotorLigado,DistCodApagados,NivelCombustivel,TensaoModulo,LambdaComandado,PosRelAcelerador,PercEtanol,TempOleo,TaxaCombustivel";

// --- ATUALIZADO: Estrutura de dados para a nova lista de PIDs ---
typedef struct {
    float engine_load;
    int coolant_temp;
    float rpm;
    int speed;
    float timing_advance;
    int intake_air_temp;
    float maf_rate;
    float throttle_pos;
    int run_time;
    int dist_since_clear;
    float fuel_level;
    float module_voltage;
    float commanded_lambda;
    float relative_throttle;
    float ethanol_percentage;
    int oil_temp;
    float fuel_rate;
} TelemetryData;

// --- FUNÇÕES DE LÓGICA OBD-II ---
bool queryOBD(uint8_t mode, uint8_t pid, twai_message_t *response_msg) {
    twai_message_t request_msg = { .identifier = 0x7DF, .flags = TWAI_MSG_FLAG_NONE, .data_length_code = 8, .data = {0x02, mode, pid, 0x55, 0x55, 0x55, 0x55, 0x55}};
    if (twai_transmit(&request_msg, pdMS_TO_TICKS(100)) != ESP_OK) return false;
    uint32_t startTime = esp_log_timestamp();
    while (esp_log_timestamp() - startTime < 300) {
        if (twai_receive(response_msg, pdMS_TO_TICKS(20)) == ESP_OK) {
            if (response_msg->identifier >= 0x7E8 && response_msg->identifier <= 0x7EF && response_msg->data[1] == (mode + 0x40) && response_msg->data[2] == pid) { return true; }
        }
    }
    return false;
}

// --- TAREFA DE LOGGING OBD-II ---
void obd_logging_task(void *pvParameters) {
    ESP_LOGI(TAG, "Iniciando tarefa de logging OBD.");
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK || twai_start() != ESP_OK) { ESP_LOGE(TAG, "Falha ao iniciar driver TWAI!"); vTaskDelete(NULL); return; }
    
    FILE* f = fopen("/spiffs/datalog.csv", "a");
    if (f == NULL) { ESP_LOGE(TAG, "Falha ao abrir datalog.csv"); vTaskDelete(NULL); return; }
    struct stat st;
    if (stat("/spiffs/datalog.csv", &st) == 0 && st.st_size == 0) {
        fprintf(f, "%s\n", CSV_HEADER);
    }
    fclose(f); 

    TelemetryData carData = {0};
    uint8_t pids_to_query[] = {0x04, 0x05, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x1F, 0x31, 0x2F, 0x42, 0x44, 0x45, 0x52, 0x5C, 0x5E};
    size_t num_pids = sizeof(pids_to_query) / sizeof(pids_to_query[0]);
    twai_message_t response;

    while (1) {
        for (int i = 0; i < num_pids; i++) {
            uint8_t pid = pids_to_query[i];
            if (queryOBD(0x01, pid, &response)) {
                int byteA = response.data[3]; int byteB = response.data[4];
                switch (pid) {
                    case 0x04: carData.engine_load = (byteA * 100.0) / 255.0; break;
                    case 0x05: carData.coolant_temp = byteA - 40; break;
                    case 0x0C: carData.rpm = ((byteA * 256) + byteB) / 4.0; break;
                    case 0x0D: carData.speed = byteA; break;
                    case 0x0E: carData.timing_advance = (byteA / 2.0) - 64.0; break;
                    case 0x0F: carData.intake_air_temp = byteA - 40; break;
                    case 0x10: carData.maf_rate = ((byteA * 256) + byteB) / 100.0; break;
                    case 0x11: carData.throttle_pos = (byteA * 100.0) / 255.0; break;
                    case 0x1F: carData.run_time = (byteA * 256) + byteB; break;
                    case 0x31: carData.dist_since_clear = (byteA * 256) + byteB; break;
                    case 0x2F: carData.fuel_level = (byteA * 100.0) / 255.0; break;
                    case 0x42: carData.module_voltage = ((byteA * 256) + byteB) / 1000.0; break;
                    case 0x44: carData.commanded_lambda = ((byteA * 256) + byteB) / 32768.0; break;
                    case 0x45: carData.relative_throttle = (byteA * 100.0) / 255.0; break;
                    case 0x52: carData.ethanol_percentage = (byteA * 100.0) / 255.0; break;
                    case 0x5C: carData.oil_temp = byteA - 40; break;
                    case 0x5E: carData.fuel_rate = ((byteA * 256) + byteB) / 20.0; break;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        
        if (carData.rpm > 0) {
            f = fopen("/spiffs/datalog.csv", "a");
            if(f != NULL) {
                fprintf(f, "%lu,%.0f,%d,%d,%.1f,%.1f,%d,%.2f,%.1f,%d,%d,%.1f,%.2f,%.4f,%.1f,%.1f,%d,%.2f\n", 
                    (unsigned long)esp_log_timestamp(), carData.rpm, carData.speed, carData.coolant_temp, carData.engine_load,
                    carData.timing_advance, carData.intake_air_temp, carData.maf_rate, carData.throttle_pos,
                    carData.run_time, carData.dist_since_clear, carData.fuel_level, carData.module_voltage,
                    carData.commanded_lambda, carData.relative_throttle, carData.ethanol_percentage,
                    carData.oil_temp, carData.fuel_rate);
                fclose(f);
                ESP_LOGI(TAG, "RPM: %.0f, Vel: %d km/h -> Snapshot gravado.", carData.rpm, carData.speed);
            } else { 
                ESP_LOGE(TAG, "Falha ao reabrir o datalog.csv"); 
            }
        } else {
            ESP_LOGI(TAG, "RPM: %.0f. Motor desligado, dados nao gravados.", carData.rpm);
        }
        
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

// --- LÓGICA DO SERVIDOR WEB E INICIALIZAÇÃO ---
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 5) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Tentando reconectar ao Wi-Fi...");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Endereco de IP obtido: " IPSTR, IP2STR(&event->ip_info.ip));
        sprintf(ip_address_str, IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t root_get_handler(httpd_req_t *req) {
    char html_buffer[1024];
    snprintf(html_buffer, sizeof(html_buffer), "<!DOCTYPE html><html><head><title>ESP32 OBD Logger</title><style>body{font-family:sans-serif;text-align:center;padding-top:50px;} button{padding:15px;font-size:16px;margin:10px;cursor:pointer;}</style></head><body><h1>ESP32 OBD-II Datalogger</h1><p>Dispositivo conectado com o IP: <strong>%s</strong></p><p>O dispositivo esta lendo os dados do seu carro e gravando em um arquivo CSV.</p><p><a href='/download'><button style='background-color:#4CAF50;color:white;'>Baixar datalog.csv</button></a><a href='/clear' onclick=\"return confirm('Tem certeza que deseja apagar todos os dados do log?');\"><button style='background-color:#f44336;color:white;'>Limpar Log</button></a></p></body></html>", ip_address_str);
    httpd_resp_send(req, html_buffer, strlen(html_buffer));
    return ESP_OK;
}

esp_err_t clear_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Recebida requisicao para limpar o arquivo de log.");
    FILE* f = fopen("/spiffs/datalog.csv", "w");
    if (f == NULL) { ESP_LOGE(TAG, "Falha ao abrir o arquivo para limpar."); httpd_resp_send_500(req); return ESP_FAIL; }
    fprintf(f, "%s\n", CSV_HEADER);
    fclose(f);
    ESP_LOGI(TAG, "Arquivo de log limpo com sucesso.");
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t download_get_handler(httpd_req_t *req) {
    char filepath[] = "/spiffs/datalog.csv";
    FILE* f = fopen(filepath, "r");
    if (f == NULL) { httpd_resp_send_404(req); return ESP_OK; }
    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"datalog.csv\"");
    char buffer[256];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        httpd_resp_send_chunk(req, buffer, bytes_read);
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

httpd_handle_t start_webserver(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root_uri = {.uri = "/", .method = HTTP_GET, .handler = root_get_handler};
        httpd_register_uri_handler(server, &root_uri);
        httpd_uri_t download_uri = {.uri = "/download", .method = HTTP_GET, .handler = download_get_handler};
        httpd_register_uri_handler(server, &download_uri);
        httpd_uri_t clear_uri = {.uri = "/clear", .method = HTTP_GET, .handler = clear_get_handler};
        httpd_register_uri_handler(server, &clear_uri);
    }
    return server;
}

void wifi_init_sta(void) {
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));
    wifi_config_t wifi_config = { .sta = { .ssid = WIFI_SSID, .password = WIFI_PASSWORD, }, };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Conectando ao Wi-Fi: %s", WIFI_SSID);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Conectado com sucesso!");
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Falha ao conectar ao Wi-Fi.");
    }
}

void spiffs_init(void) {
    esp_vfs_spiffs_conf_t conf = { .base_path = "/spiffs", .partition_label = NULL, .max_files = 5, .format_if_mount_failed = true };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao inicializar o SPIFFS (%s)", esp_err_to_name(ret));
    }
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    spiffs_init();
    wifi_init_sta();
    start_webserver();
    xTaskCreate(obd_logging_task, "OBD Logging Task", 4096, NULL, 5, NULL);
}