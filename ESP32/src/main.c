/**
 * OBD2 Smart Logger - Auto-start/stop baseado em ignição
 * 
 * Features:
 * - Detecta ignição automaticamente (RPM > 0 ou voltagem > 12.5V)
 * - Inicia logging quando carro liga
 * - Para logging quando carro desliga
 * - LED de status (WiFi, Logging, Erro)
 * - Sessions separadas (cada ignição = arquivo novo)
 * - Preparado para MQTT
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/twai.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "obd_config.h"
#include "wifi_manager.h"
#include "obd_can.h"
#include "obd_parser.h"
#include "web_server.h"
#include "can_sniffer.h"
#include "esp_netif.h"
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include "esp_sntp.h"
#include <time.h>
#include <sys/time.h>

#define TAG "SMART_LOGGER"
#define LED_PIN GPIO_NUM_2

// Estados do sistema
typedef enum {
    STATE_INIT,           // Inicializando
    STATE_IGNITION_OFF,   // Carro desligado
    STATE_IGNITION_ON,    // Carro ligado
    STATE_LOGGING,        // Gravando dados OBD
    STATE_ERROR,          // Erro crítico
    // Sniffing states
    STATE_SNIFF_ACTIVE,   // Sniffing CAN ativo
    STATE_SNIFF_STORAGE_LOW, // Sniffing com storage baixo
    STATE_HYBRID_MODE     // OBD + Sniffing simultâneo
} system_state_t;

#define NUM_STATES 8  // Total de estados para LED

// Configurações de detecção
#define IGNITION_RPM_THRESHOLD 300        // RPM > 300 = ignição ligada
#define IGNITION_VOLTAGE_THRESHOLD 12.5f  // Voltagem > 12.5V = ignição ligada
#define IGNITION_OFF_TIMEOUT 5000         // 5s sem sinais = desligou (era 30s)

// PIDs essenciais
static const obd_pid_t ESSENTIAL_PIDS[] = {
    PID_RPM,
    PID_SPEED,
    PID_COOLANT_TEMP,
    PID_ENGINE_LOAD,
    PID_THROTTLE_POS,
    PID_MAF_RATE
};
#define NUM_PIDS (sizeof(ESSENTIAL_PIDS)/sizeof(ESSENTIAL_PIDS[0]))

// Estado global
static system_state_t current_state = STATE_INIT;
static system_state_t obd_state = STATE_INIT;  // Estado separado para OBD
static telemetry_data_t snapshot = {0};
static FILE *csv_file = NULL;
static char current_session_file[64] = {0};
static uint32_t records_in_session = 0;
static uint64_t last_ignition_signal = 0;

// Padrões de LED
typedef struct {
    int on_ms;
    int off_ms;
    int repeat;  // 0 = infinito, >0 = repetições antes de pausa
    int pause_ms; // Pausa após repetições
} led_pattern_t;

static const led_pattern_t LED_PATTERNS[] = {
    [STATE_INIT]             = {100, 100, 0, 0},    // Pisca rápido
    [STATE_IGNITION_OFF]     = {2000, 500, 0, 0},   // Pisca lento
    [STATE_IGNITION_ON]      = {50, 1950, 0, 0},    // Pulso rápido
    [STATE_LOGGING]          = {500, 500, 0, 0},    // Pisca médio (OBD)
    [STATE_ERROR]            = {100, 100, 0, 0},    // Pisca rápido (erro)
    // Sniffing patterns - distintos para identificar modo
    [STATE_SNIFF_ACTIVE]     = {100, 100, 0, 0},    // Pisca rápido constante
    [STATE_SNIFF_STORAGE_LOW]= {50, 50, 3, 500},    // 3 piscos rápidos + pausa (alerta!)
    [STATE_HYBRID_MODE]      = {200, 200, 2, 800}   // 2 piscos + pausa (híbrido)
};

/**
 * @brief Controla LED baseado no estado
 * Verifica estado a cada 50ms para resposta rápida a mudanças
 */
static void led_task(void *arg) {
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    
    system_state_t last_state = current_state;
    uint32_t cycle_time = 0;
    bool led_on = false;
    int repeat_count = 0;
    bool in_pause = false;
    
    while (1) {
        led_pattern_t pattern = LED_PATTERNS[current_state];
        
        // Detecta mudança de estado - reinicia ciclo
        if (current_state != last_state) {
            ESP_LOGI(TAG, "LED pattern changed (state %d -> %d)", last_state, current_state);
            last_state = current_state;
            cycle_time = 0;
            repeat_count = 0;
            in_pause = false;
            gpio_set_level(LED_PIN, 0);
            led_on = false;
        }
        
        // Padrões com repetições (sniffing)
        if (pattern.repeat > 0) {
            if (in_pause) {
                // Está na pausa entre ciclos
                if (cycle_time >= pattern.pause_ms) {
                    cycle_time = 0;
                    repeat_count = 0;
                    in_pause = false;
                }
            } else {
                // Executando as repetições
                int blink_cycle = pattern.on_ms + pattern.off_ms;
                int position_in_blink = cycle_time % blink_cycle;
                
                if (position_in_blink < pattern.on_ms) {
                    if (!led_on) {
                        gpio_set_level(LED_PIN, 1);
                        led_on = true;
                    }
                } else {
                    if (led_on) {
                        gpio_set_level(LED_PIN, 0);
                        led_on = false;
                        repeat_count++;
                    }
                }
                
                // Completou todas as repetições?
                if (repeat_count >= pattern.repeat) {
                    in_pause = true;
                    cycle_time = 0;
                    gpio_set_level(LED_PIN, 0);
                    led_on = false;
                }
            }
        } else {
            // Padrões simples
            int blink_cycle = pattern.on_ms + pattern.off_ms;
            int position_in_blink = cycle_time % blink_cycle;
            
            if (position_in_blink < pattern.on_ms) {
                if (!led_on) {
                    gpio_set_level(LED_PIN, 1);
                    led_on = true;
                }
            } else {
                if (led_on) {
                    gpio_set_level(LED_PIN, 0);
                    led_on = false;
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(50));
        cycle_time += 50;
    }
}

/**
 * @brief Inicializa SPIFFS
 */
static esp_err_t init_storage() {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 10,
        .format_if_mount_failed = true
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    size_t total = 0, used = 0;
    esp_spiffs_info(NULL, &total, &used);
    ESP_LOGI(TAG, "SPIFFS: %d KB total, %d KB used", total/1024, used/1024);
    
    return ESP_OK;
}

/**
 * @brief Cria novo arquivo de sessão
 */
static esp_err_t create_session_file() {
    // Nome do arquivo: session_2024-12-18_14-30.csv
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    snprintf(current_session_file, sizeof(current_session_file), 
            "/spiffs/session_%04d-%02d-%02d_%02d-%02d.csv",
            timeinfo.tm_year + 1900,
            timeinfo.tm_mon + 1,
            timeinfo.tm_mday,
            timeinfo.tm_hour,
            timeinfo.tm_min);
    
    csv_file = fopen(current_session_file, "w");
    if (!csv_file) {
        ESP_LOGE(TAG, "Failed to create session file");
        return ESP_FAIL;
    }
    
    // Header CSV
    fprintf(csv_file, "timestamp,rpm,speed,coolant,load,throttle,maf\n");
    fflush(csv_file);
    
    records_in_session = 0;
    ESP_LOGI(TAG, "Session file created: %s", current_session_file);
    
    return ESP_OK;
}

/**
 * @brief Fecha arquivo de sessão
 */
static void close_session_file() {
    if (csv_file) {
        fflush(csv_file);
        fclose(csv_file);
        csv_file = NULL;
        
        ESP_LOGI(TAG, "Session closed: %lu records saved", records_in_session);
        
        // Lista arquivos na SPIFFS
        struct stat st;
        if (stat(current_session_file, &st) == 0) {
            ESP_LOGI(TAG, "File size: %.2f KB", st.st_size / 1024.0f);
        }
    }
}

/**
 * @brief Salva registro no CSV
 */
static void save_record() {
    if (!csv_file) return;
    
    fprintf(csv_file, "%llu,%.0f,%d,%d,%.1f,%.1f,%.2f\n",
            snapshot.device_timestamp,
            snapshot.rpm,
            snapshot.speed,
            snapshot.coolant_temp,
            snapshot.engine_load,
            snapshot.throttle_pos,
            snapshot.maf_rate);
    
    records_in_session++;
    
    // Flush a cada 10 registros
    if (records_in_session % 10 == 0) {
        fflush(csv_file);
    }
}

/**
 * @brief Detecta se ignição está ligada
 */
static bool is_ignition_on() {
    // Critério 1: RPM > threshold
    if (snapshot.rpm > IGNITION_RPM_THRESHOLD) {
        return true;
    }
    
    // Critério 2: Voltagem > threshold (se disponível)
    if (snapshot.module_voltage > IGNITION_VOLTAGE_THRESHOLD) {
        return true;
    }
    
    // Critério 3: Qualquer PID respondendo recentemente
    uint64_t now = esp_timer_get_time() / 1000;
    if (now - last_ignition_signal < IGNITION_OFF_TIMEOUT) {
        return true;
    }
    
    return false;
}

/**
 * @brief Task principal de aquisição OBD
 */
static void obd_task(void *arg) {
    ESP_LOGI(TAG, "OBD task started");
    
    // Inicializa snapshot
    obd_init_telemetry(&snapshot);
    
    uint8_t response[8];
    size_t response_len;
    uint64_t last_print = 0;
    uint32_t cycle_count = 0;
    
    vTaskDelay(pdMS_TO_TICKS(2000)); // Aguarda estabilização
    
    while (1) {
        snapshot.device_timestamp = esp_timer_get_time() / 1000;
        
        // Lê PIDs essenciais
        for (int i = 0; i < NUM_PIDS; i++) {
            response_len = sizeof(response);
            
            if (obd_can_request(ESSENTIAL_PIDS[i], response, &response_len) == ESP_OK) {
                if (obd_parse_response(ESSENTIAL_PIDS[i], response, response_len, &snapshot)) {
                    last_ignition_signal = esp_timer_get_time() / 1000;
                }
            }
        }
        
        // Máquina de estados
        bool ignition = is_ignition_on();
        
        switch (current_state) {
            case STATE_INIT:
                current_state = ignition ? STATE_IGNITION_ON : STATE_IGNITION_OFF;
                break;
                
            case STATE_IGNITION_OFF:
                if (ignition) {
                    ESP_LOGI(TAG, "🔑 Ignition ON detected");
                    current_state = STATE_IGNITION_ON;
                    
                    // Cria novo arquivo de sessão
                    if (create_session_file() == ESP_OK) {
                        current_state = STATE_LOGGING;
                    } else {
                        current_state = STATE_ERROR;
                    }
                }
                break;
                
            case STATE_IGNITION_ON:
            case STATE_LOGGING:
                if (!ignition) {
                    ESP_LOGI(TAG, "🔑 Ignition OFF detected");
                    close_session_file();
                    current_state = STATE_IGNITION_OFF;
                } else {
                    // Salva dados
                    save_record();
                }
                break;
                
            case STATE_ERROR:
                // Tenta recuperar
                vTaskDelay(pdMS_TO_TICKS(5000));
                current_state = STATE_INIT;
                break;
            
            // Estados de sniffing - tratados pela sniffer_task
            case STATE_SNIFF_ACTIVE:
            case STATE_SNIFF_STORAGE_LOW:
            case STATE_HYBRID_MODE:
                // Continua lendo PIDs em modo híbrido
                if (obd_state == STATE_LOGGING) {
                    save_record();
                }
                // NÃO atualiza obd_state aqui - preserva estado OBD anterior
                break;
        }
        
        // Salva estado OBD somente se NÃO for estado de sniffing
        if (current_state != STATE_SNIFF_ACTIVE && 
            current_state != STATE_SNIFF_STORAGE_LOW && 
            current_state != STATE_HYBRID_MODE) {
            obd_state = current_state;
        }
        
        cycle_count++;
        
        // Print status a cada 10 segundos
        uint64_t now = esp_timer_get_time() / 1000;
        if (now - last_print > 10000) {
            last_print = now;
            
            const char *state_names[] = {"INIT", "IGN_OFF", "IGN_ON", "LOGGING", "ERROR",
                                         "SNIFF", "SNIFF_LOW", "HYBRID"};
            
            ESP_LOGI(TAG, "[%lu] State:%s | RPM:%.0f Spd:%d Tmp:%d°C | Records:%lu | Heap:%luKB",
                     cycle_count,
                     state_names[current_state],
                     snapshot.rpm,
                     snapshot.speed,
                     snapshot.coolant_temp,
                     records_in_session,
                     esp_get_free_heap_size() / 1024);
            
            // Atualiza web server
            web_server_update_telemetry(&snapshot);
            web_server_update_state(current_state, records_in_session);
        }
        
        vTaskDelay(pdMS_TO_TICKS(50)); // 20Hz quando logando
    }
}

/**
 * @brief Task de sniffing CAN - roda em paralelo com OBD task
 * Captura mensagens CAN brutas e salva em formato GVRET
 */
static void sniffer_task(void *arg) {
    ESP_LOGI(TAG, "Sniffer task started (waiting for activation via web API)");
    
    twai_message_t rx_msg;
    can_raw_message_t raw_msg;
    uint32_t flush_counter = 0;
    uint32_t msg_batch = 0;
    
    while (1) {
        sniff_state_t sniff_state = can_sniffer_get_state();
        
        // Só processa se sniffer estiver ativo
        if (sniff_state == SNIFF_STATE_ACTIVE || 
            sniff_state == SNIFF_STATE_STORAGE_LOW) {
            
            // Atualiza LED state baseado no sniffer
            if (obd_state == STATE_LOGGING) {
                current_state = STATE_HYBRID_MODE;
            } else if (sniff_state == SNIFF_STATE_STORAGE_LOW) {
                current_state = STATE_SNIFF_STORAGE_LOW;
            } else {
                current_state = STATE_SNIFF_ACTIVE;
            }
            
            // Processa até 10 mensagens por iteração
            msg_batch = 0;
            while (msg_batch < 10) {
                // Timeout de 1ms - não bloqueia muito
                esp_err_t ret = obd_can_receive_raw(&rx_msg, 1);
                if (ret != ESP_OK) {
                    break;  // Sem mais mensagens
                }
                
                // Converte para formato raw
                raw_msg.timestamp_us = esp_timer_get_time();
                raw_msg.identifier = rx_msg.identifier;
                raw_msg.dlc = rx_msg.data_length_code;
                raw_msg.extended = rx_msg.extd;
                raw_msg.is_tx = false;
                raw_msg.bus = 0;
                memcpy(raw_msg.data, rx_msg.data, 8);
                
                // Processa mensagem (adiciona ao buffer)
                can_sniffer_process_message(&raw_msg);
                msg_batch++;
            }
            
            // Flush buffer periodicamente
            flush_counter++;
            if (flush_counter >= 50) {
                flush_counter = 0;
                can_sniffer_flush();
                
                // Verifica se storage ficou crítico
                if (can_sniffer_get_state() == SNIFF_STATE_STORAGE_FULL) {
                    ESP_LOGW(TAG, "Storage full - sniffer stopped automatically");
                    current_state = obd_state;
                }
            }
            
            // IMPORTANTE: Delay obrigatório para alimentar watchdog
            vTaskDelay(pdMS_TO_TICKS(10));
            
        } else {
            // Sniffer não ativo - volta para estado OBD
            if (current_state == STATE_SNIFF_ACTIVE || 
                current_state == STATE_SNIFF_STORAGE_LOW ||
                current_state == STATE_HYBRID_MODE) {
                // Restaura estado baseado na ignição
                current_state = obd_state;
                ESP_LOGI(TAG, "Sniffer inactive - LED restored to OBD state");
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            flush_counter = 0;
        }
    }
}

/**
 * @brief Lista arquivos de sessão salvos
 */
static void list_session_files() {
    ESP_LOGI(TAG, "=== Session Files ===");
    
    DIR *dir = opendir("/spiffs");
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open SPIFFS directory");
        return;
    }
    
    struct dirent *entry;
    struct stat st;
    char filepath[320];  // Aumentado: 255 (NAME_MAX) + 64 (/spiffs/) + margem
    int file_count = 0;
    size_t total_size = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        snprintf(filepath, sizeof(filepath), "/spiffs/%s", entry->d_name);
        
        if (stat(filepath, &st) == 0 && S_ISREG(st.st_mode)) {
            file_count++;
            total_size += st.st_size;
            ESP_LOGI(TAG, "  %s - %.2f KB", entry->d_name, st.st_size / 1024.0f);
        }
    }
    
    closedir(dir);
    
    ESP_LOGI(TAG, "Total: %d files, %.2f KB", file_count, total_size / 1024.0f);
    ESP_LOGI(TAG, "====================");
}

/**
 * @brief Task que espera WiFi e inicia web server
 */
static void wifi_web_task(void *arg) {
    // Espera WiFi conectar (ou timeout 10s)
    if (wifi_wait_for_connection(10000) == ESP_OK) {
        char device_ip[16];
        wifi_get_ip_string(device_ip, sizeof(device_ip));
        ESP_LOGI(TAG, "✓ WiFi connected: %s", device_ip);
        
        // Sincroniza hora via NTP
        setenv("TZ", "BRT+3", 1);
        tzset();
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        sntp_setservername(0, "pool.ntp.org");
        sntp_init();
        ESP_LOGI(TAG, "✓ NTP started");
        vTaskDelay(pdMS_TO_TICKS(2000));  // Aguarda 2s
        
        // Inicia web server
        if (web_server_start(device_ip) == ESP_OK) {
            ESP_LOGI(TAG, "✓ Access at: http://%s/ or http://obd2logger.local/", device_ip);
        }
    } else {
        ESP_LOGW(TAG, "✗ WiFi timeout - web disabled (logging works offline)");
    }
    
    // Task termina
    vTaskDelete(NULL);
}


void app_main(void) {
    // Configura logs
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set(TAG, ESP_LOG_INFO);
    esp_log_level_set("CAN_SNIFF", ESP_LOG_INFO);
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   Smart OBD2 Logger v2.0           ║");
    ESP_LOGI(TAG, "║   OBD + CAN Sniffer Hybrid Mode    ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
    
    // Init
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(init_storage());
    
    // Lista sessões anteriores
    list_session_files();

    
    // Init CAN PRIMEIRO! (antes do WiFi)
    ESP_LOGI(TAG, "Initializing CAN...");
    if (obd_can_init() != ESP_OK) {
        ESP_LOGE(TAG, "CAN init failed");
        current_state = STATE_ERROR;
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }
    ESP_LOGI(TAG, "✓ CAN ready (OBD mode)");

    // Init CAN sniffer module
    ESP_LOGI(TAG, "Initializing CAN Sniffer...");
    if (can_sniffer_init() != ESP_OK) {
        ESP_LOGW(TAG, "Sniffer init failed - continuing without sniffer");
    } else {
        ESP_LOGI(TAG, "✓ Sniffer ready (activate via /api/sniff/start)");
    }

    // Init WiFi (não-bloqueante)
    ESP_LOGI(TAG, "Initializing WiFi (background)...");
    ESP_ERROR_CHECK(wifi_manager_init());
    wifi_connect(WIFI_SSID, WIFI_PASSWORD);

    // Start tasks IMEDIATAMENTE (não espera WiFi!)
    ESP_LOGI(TAG, "✓ System ready - Starting tasks");
    xTaskCreate(led_task, "LED", 2048, NULL, 5, NULL);
    xTaskCreate(obd_task, "OBD", 8192, NULL, 6, NULL);
    xTaskCreate(sniffer_task, "SNIFF", 4096, NULL, 7, NULL);  // Alta prioridade para sniffing

    // Task separada para iniciar web server quando WiFi conectar
    xTaskCreate(wifi_web_task, "WIFI_WEB", 4096, NULL, 3, NULL);

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   LED Patterns:                    ║");
    ESP_LOGI(TAG, "║   - Slow blink: Ignition OFF       ║");
    ESP_LOGI(TAG, "║   - Medium blink: OBD Logging      ║");
    ESP_LOGI(TAG, "║   - Fast blink: Sniffing Active    ║");
    ESP_LOGI(TAG, "║   - 3 quick + pause: Storage Low   ║");
    ESP_LOGI(TAG, "║   - 2 blinks + pause: Hybrid Mode  ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════╝");
}