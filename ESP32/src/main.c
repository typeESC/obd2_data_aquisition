/**
 * OBD2 Smart Logger v2.0 - Auto-start/stop baseado em ignição
 * 
 * Hardware v2.0:
 * - CAN transceiver: SN65HVD230 (3.3V native - no level shifter!)
 * - Storage: SD Card (SPI) with SPIFFS fallback
 * - Auto-start OBD + Sniffer on ignition detection
 * 
 * Features:
 * - Detecta ignição automaticamente (RPM > 0 ou voltagem > 12.5V)
 * - Inicia logging OBD + CAN sniffing quando carro liga (AUTO!)
 * - Para logging quando carro desliga
 * - LED de status (WiFi, Logging, Erro)
 * - Sessions separadas (cada ignição = arquivo novo)
 * - SD Card para longas viagens (fallback para SPIFFS)
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
#include "pid_scheduler.h"
#include "storage_manager.h"
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
#define BOOT_BUTTON_PIN GPIO_NUM_0  // Botão BOOT do ESP32

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

// PIDs essenciais - DEPRECATED: Agora usa pid_scheduler
// Removido LEGACY_PIDS pois não é mais utilizado

// Estado global
static system_state_t current_state = STATE_INIT;
static system_state_t obd_state = STATE_INIT;  // Estado separado para OBD
static telemetry_data_t snapshot = {0};
static extended_telemetry_t ext_telemetry = {0};  // Telemetria estendida do scheduler
static FILE *csv_file = NULL;
static char current_session_file[64] = {0};
static uint32_t records_in_session = 0;
static uint64_t last_ignition_signal = 0;
static bool sniffer_auto_started = false;  // Track if sniffer was auto-started

// DTC (Diagnostic Trouble Codes) data
static dtc_data_t confirmed_dtcs = {0};    // DTCs confirmados (MIL aceso)
static dtc_data_t pending_dtcs = {0};      // DTCs pendentes (em monitoramento)
static uint64_t last_dtc_check = 0;        // Última verificação de DTCs
#define DTC_CHECK_INTERVAL_MS  30000       // Verificar DTCs a cada 30 segundos

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
 * @brief Task que monitora botão BOOT para controlar sniffer
 * Pressionar BOOT: liga/desliga sniffer (modo híbrido se carro ligado)
 */
static void button_task(void *arg) {
    // Configura GPIO0 (botão BOOT) como input com pull-up
    gpio_config_t btn_config = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&btn_config);
    
    bool last_button_state = true;  // Pull-up = HIGH quando não pressionado
    uint32_t debounce_count = 0;
    const uint32_t DEBOUNCE_THRESHOLD = 3;  // 150ms (50ms * 3)
    
    ESP_LOGI(TAG, "Button task started - Press BOOT to toggle sniffer");
    
    while (1) {
        bool button_state = gpio_get_level(BOOT_BUTTON_PIN);
        
        // Detecta borda de descida (botão pressionado)
        if (button_state == false && last_button_state == true) {
            debounce_count++;
            
            if (debounce_count >= DEBOUNCE_THRESHOLD) {
                // Botão pressionado confirmado
                sniff_state_t sniff_state = can_sniffer_get_state();
                
                if (sniff_state == SNIFF_STATE_IDLE) {
                    // Inicia sniffer
                    sniff_filter_t filter = {
                        .id_min = 0x000,
                        .id_max = 0x7FF,
                        .exclude_obd_requests = true
                    };
                    
                    if (can_sniffer_start(&filter) == ESP_OK) {
                        ESP_LOGI(TAG, "🔴 Sniffer started by BOOT button");
                        if (obd_state == STATE_LOGGING) {
                            ESP_LOGI(TAG, "   ⚡ HYBRID MODE active (OBD + Sniffing)");
                        }
                    }
                } else {
                    // Para sniffer
                    can_sniffer_stop();
                    ESP_LOGI(TAG, "⚫ Sniffer stopped by BOOT button");
                }
                
                debounce_count = 0;
                // Aguarda soltar o botão
                while (gpio_get_level(BOOT_BUTTON_PIN) == false) {
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
            }
        } else if (button_state == true) {
            debounce_count = 0;
        }
        
        last_button_state = button_state;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/**
 * @brief Inicializa storage (SD Card com fallback para SPIFFS)
 */
static esp_err_t init_storage() {
    // Use auto-detection: SD card first, fallback to SPIFFS
    esp_err_t ret = storage_manager_init_auto();
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Storage initialization failed!");
        return ret;
    }
    
    // Log storage type
    if (storage_manager_is_sd_active()) {
        ESP_LOGI(TAG, "📁 Storage: SD Card (large capacity for long trips)");
    } else {
        ESP_LOGW(TAG, "📁 Storage: SPIFFS (limited ~2MB - insert SD for long trips)");
    }
    
    return ESP_OK;
}

/**
 * @brief Cria novo arquivo de sessão
 */
static esp_err_t create_session_file() {
    // Get base path from storage manager
    const char *base_path = storage_manager_get_base_path();
    if (!base_path) {
        ESP_LOGE(TAG, "Storage not initialized");
        return ESP_FAIL;
    }
    
    // Nome do arquivo: session_2024-12-18_14-30.csv
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    snprintf(current_session_file, sizeof(current_session_file), 
            "%s/session_%04d-%02d-%02d_%02d-%02d.csv",
            base_path,
            timeinfo.tm_year + 1900,
            timeinfo.tm_mon + 1,
            timeinfo.tm_mday,
            timeinfo.tm_hour,
            timeinfo.tm_min);
    
    csv_file = fopen(current_session_file, "w");
    if (!csv_file) {
        ESP_LOGE(TAG, "Failed to create session file: %s", current_session_file);
        return ESP_FAIL;
    }
    
    // Header CSV estendido (timestamp em microssegundos para sincronização com CAN)
    // Tier1: rpm, speed, throttle, load, maf
    // Tier2: coolant, manifold, fuel_level, intake_temp, runtime
    // Tier3: voltage, oil_temp, fuel_trim_short, fuel_trim_long, distance, ambient, timing
    // Tier4: mil_status, dtc_count
    fprintf(csv_file, "timestamp_us,rpm,speed,throttle,load,maf,"
                      "coolant,manifold,fuel_level,intake_temp,runtime,"
                      "voltage,oil_temp,fuel_trim_short,fuel_trim_long,distance,ambient,timing,"
                      "mil_status,dtc_count,valid_mask\n");
    fflush(csv_file);
    
    records_in_session = 0;
    ESP_LOGI(TAG, "Session file created: %s (on %s)", current_session_file,
             storage_manager_is_sd_active() ? "SD Card" : "SPIFFS");
    
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
 * @brief Salva DTCs encontrados em arquivo
 */
static void save_dtcs_to_file(const dtc_data_t* dtcs, bool confirmed) {
    if (dtcs->count == 0) return;
    
    // Get base path from storage manager
    const char *base_path = storage_manager_get_base_path();
    if (!base_path) return;
    
    char dtc_path[80];
    snprintf(dtc_path, sizeof(dtc_path), "%s/dtc_history.csv", base_path);
    
    // Arquivo de DTCs (append mode)
    FILE* dtc_file = fopen(dtc_path, "a");
    if (!dtc_file) {
        // Primeira vez - cria com header
        dtc_file = fopen(dtc_path, "w");
        if (!dtc_file) {
            ESP_LOGE(TAG, "Failed to create DTC history file");
            return;
        }
        fprintf(dtc_file, "timestamp_us,type,code,system,description\n");
    }
    
    uint64_t now = esp_timer_get_time();
    const char* type_str = confirmed ? "CONFIRMED" : "PENDING";
    
    for (int i = 0; i < dtcs->count; i++) {
        const dtc_code_t* dtc = &dtcs->codes[i];
        const char* system_names[] = {"Powertrain", "Chassis", "Body", "Network"};
        
        fprintf(dtc_file, "%llu,%s,%s,%s,\n",
                now,
                type_str,
                dtc->code_str,
                system_names[dtc->system]);
    }
    
    fflush(dtc_file);
    fclose(dtc_file);
    
    ESP_LOGI(TAG, "Saved %d %s DTCs to history", dtcs->count, type_str);
}

/**
 * @brief Lê e processa DTCs (confirmados e pendentes)
 */
static void check_and_log_dtcs(void) {
    static uint8_t last_confirmed_count = 0;
    static uint8_t last_pending_count = 0;
    
    // Lê DTCs confirmados (MIL ligado)
    if (pid_scheduler_read_dtcs(&confirmed_dtcs, true) == ESP_OK) {
        ext_telemetry.dtc_count = confirmed_dtcs.count;
        
        // Novo DTC detectado?
        if (confirmed_dtcs.count > last_confirmed_count) {
            ESP_LOGW(TAG, "⚠️  NEW CONFIRMED DTC DETECTED!");
            for (int i = 0; i < confirmed_dtcs.count; i++) {
                ESP_LOGW(TAG, "   🔴 %s (%s)", 
                         confirmed_dtcs.codes[i].code_str,
                         confirmed_dtcs.codes[i].system == DTC_SYSTEM_POWERTRAIN ? "Powertrain" :
                         confirmed_dtcs.codes[i].system == DTC_SYSTEM_CHASSIS ? "Chassis" :
                         confirmed_dtcs.codes[i].system == DTC_SYSTEM_BODY ? "Body" : "Network");
            }
            save_dtcs_to_file(&confirmed_dtcs, true);
        }
        last_confirmed_count = confirmed_dtcs.count;
        
        // Atualiza MIL status
        confirmed_dtcs.mil_on = (confirmed_dtcs.count > 0);
    }
    
    vTaskDelay(pdMS_TO_TICKS(100));  // Pequeno delay entre requests
    
    // Lê DTCs pendentes (em monitoramento)
    if (pid_scheduler_read_dtcs(&pending_dtcs, false) == ESP_OK) {
        ext_telemetry.pending_dtc_count = pending_dtcs.count;
        
        // Novo DTC pendente?
        if (pending_dtcs.count > last_pending_count) {
            ESP_LOGI(TAG, "⚡ New pending DTC detected:");
            for (int i = 0; i < pending_dtcs.count; i++) {
                ESP_LOGI(TAG, "   🟡 %s", pending_dtcs.codes[i].code_str);
            }
            save_dtcs_to_file(&pending_dtcs, false);
        }
        last_pending_count = pending_dtcs.count;
    }
    
    // Log resumo
    if (confirmed_dtcs.count > 0 || pending_dtcs.count > 0) {
        ESP_LOGI(TAG, "DTC Status: %d confirmed, %d pending, MIL: %s",
                 confirmed_dtcs.count, pending_dtcs.count,
                 confirmed_dtcs.mil_on ? "ON" : "OFF");
    }
    
    // Atualiza web server com DTCs
    web_server_update_dtcs(&confirmed_dtcs, &pending_dtcs);
}

/**
 * @brief Salva registro no CSV (formato estendido)
 */
static void save_record() {
    if (!csv_file) return;
    
    // Timestamp em microssegundos (sincronizado com CAN sniffer)
    uint64_t timestamp_us = esp_timer_get_time();
    ext_telemetry.timestamp_us = timestamp_us;
    
    // Formato estendido com todos os tiers
    fprintf(csv_file, "%llu,%.0f,%d,%.1f,%.1f,%.2f,"     // Tier 1
                      "%d,%d,%.1f,%d,%d,"                 // Tier 2
                      "%.2f,%d,%.1f,%.1f,%d,%d,%.1f,"    // Tier 3
                      "%d,%d,%lu\n",                      // Tier 4 + mask
            timestamp_us,
            // Tier 1 - Critical
            ext_telemetry.rpm,
            ext_telemetry.speed,
            ext_telemetry.throttle_pos,
            ext_telemetry.engine_load,
            ext_telemetry.maf_rate,
            // Tier 2 - High
            ext_telemetry.coolant_temp,
            ext_telemetry.manifold_pressure,
            ext_telemetry.fuel_level,
            ext_telemetry.intake_air_temp,
            ext_telemetry.run_time,
            // Tier 3 - Medium
            ext_telemetry.control_voltage,
            ext_telemetry.oil_temp,
            ext_telemetry.fuel_trim_short_b1,
            ext_telemetry.fuel_trim_long_b1,
            ext_telemetry.distance_since_clear,
            ext_telemetry.ambient_temp,
            ext_telemetry.timing_advance,
            // Tier 4 - Low
            ext_telemetry.mil_status,
            ext_telemetry.dtc_count,
            ext_telemetry.valid_mask);
    
    records_in_session++;
    
    // Flush a cada 10 registros
    if (records_in_session % 10 == 0) {
        fflush(csv_file);
    }
    
    // Sync snapshot para compatibilidade com web_server
    snapshot.rpm = ext_telemetry.rpm;
    snapshot.speed = ext_telemetry.speed;
    snapshot.coolant_temp = ext_telemetry.coolant_temp;
    snapshot.engine_load = ext_telemetry.engine_load;
    snapshot.throttle_pos = ext_telemetry.throttle_pos;
    snapshot.maf_rate = ext_telemetry.maf_rate;
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
 * @brief Task principal de aquisição OBD com scheduler por tiers
 * 
 * Tiers de polling:
 * - CRITICAL (100ms): RPM, Speed, Throttle, Load, MAF
 * - HIGH (500ms): Coolant, Manifold, Fuel, Intake, Runtime
 * - MEDIUM (2s): Voltage, Oil, Fuel Trim, Distance, Ambient, Timing
 * - LOW (15s): MIL, DTCs, O2 sensors
 */
static void obd_task(void *arg) {
    ESP_LOGI(TAG, "OBD task started with tier-based scheduler");
    
    // Inicializa scheduler e telemetria
    pid_scheduler_init();
    obd_init_telemetry(&snapshot);
    memset(&ext_telemetry, 0, sizeof(ext_telemetry));
    
    uint64_t last_print = 0;
    uint64_t last_stats = 0;
    uint32_t cycle_count = 0;
    
    // Timestamps para cada tier
    uint64_t last_tier_poll[POLL_TIER_COUNT] = {0};
    
    vTaskDelay(pdMS_TO_TICKS(2000)); // Aguarda estabilização
    
    ESP_LOGI(TAG, "Starting tier-based polling:");
    ESP_LOGI(TAG, "  CRITICAL: 5 PIDs @ 100ms");
    ESP_LOGI(TAG, "  HIGH:     5 PIDs @ 500ms");
    ESP_LOGI(TAG, "  MEDIUM:   7 PIDs @ 2000ms");
    ESP_LOGI(TAG, "  LOW:      5 PIDs @ 15000ms");
    
    while (1) {
        uint64_t now_ms = esp_timer_get_time() / 1000;
        snapshot.device_timestamp = now_ms;
        
        // ===== TIER-BASED POLLING =====
        // Tier CRITICAL - Polls every 100ms (high priority)
        if (now_ms - last_tier_poll[POLL_TIER_CRITICAL] >= TIER_CRITICAL_INTERVAL_MS) {
            (void)pid_scheduler_poll_tier(POLL_TIER_CRITICAL, &ext_telemetry);
            last_tier_poll[POLL_TIER_CRITICAL] = now_ms;
            
            // RPM/Speed para detecção de ignição
            if (ext_telemetry.valid_mask & VALID_RPM) {
                snapshot.rpm = ext_telemetry.rpm;
                if (ext_telemetry.rpm > 0) {
                    last_ignition_signal = now_ms;
                }
            }
            if (ext_telemetry.valid_mask & VALID_SPEED) {
                snapshot.speed = ext_telemetry.speed;
            }
        }
        
        // Tier HIGH - Polls every 500ms
        if (now_ms - last_tier_poll[POLL_TIER_HIGH] >= TIER_HIGH_INTERVAL_MS) {
            pid_scheduler_poll_tier(POLL_TIER_HIGH, &ext_telemetry);
            last_tier_poll[POLL_TIER_HIGH] = now_ms;
            
            // Sync para snapshot
            if (ext_telemetry.valid_mask & VALID_COOLANT) {
                snapshot.coolant_temp = ext_telemetry.coolant_temp;
            }
        }
        
        // Tier MEDIUM - Polls every 2s
        if (now_ms - last_tier_poll[POLL_TIER_MEDIUM] >= TIER_MEDIUM_INTERVAL_MS) {
            pid_scheduler_poll_tier(POLL_TIER_MEDIUM, &ext_telemetry);
            last_tier_poll[POLL_TIER_MEDIUM] = now_ms;
            
            // Voltagem para detecção de ignição
            if (ext_telemetry.valid_mask & VALID_VOLTAGE) {
                snapshot.module_voltage = ext_telemetry.control_voltage;
            }
        }
        
        // Tier LOW - Polls every 15s
        if (now_ms - last_tier_poll[POLL_TIER_LOW] >= TIER_LOW_INTERVAL_MS) {
            pid_scheduler_poll_tier(POLL_TIER_LOW, &ext_telemetry);
            last_tier_poll[POLL_TIER_LOW] = now_ms;
        }
        
        // ===== DTC CHECK - Every 30 seconds =====
        if (now_ms - last_dtc_check >= DTC_CHECK_INTERVAL_MS) {
            last_dtc_check = now_ms;
            check_and_log_dtcs();
        }
        
        // ===== MÁQUINA DE ESTADOS =====
        bool ignition = is_ignition_on();
        
        switch (current_state) {
            case STATE_INIT:
                current_state = ignition ? STATE_IGNITION_ON : STATE_IGNITION_OFF;
                break;
                
            case STATE_IGNITION_OFF:
                if (ignition) {
                    ESP_LOGI(TAG, "🔑 Ignition ON detected");
                    current_state = STATE_IGNITION_ON;
                    
                    // Cria novo arquivo de sessão OBD
                    if (create_session_file() == ESP_OK) {
                        current_state = STATE_LOGGING;
                        
                        // === AUTO-START SNIFFER (NEW!) ===
                        #if AUTO_START_SNIFFER_ON_IGNITION
                        sniff_filter_t auto_filter = {
                            .id_min = 0x000,
                            .id_max = 0x7FF,
                            .exclude_obd_requests = SNIFFER_EXCLUDE_OBD_REQUESTS,
                            .exclude_obd_responses = SNIFFER_EXCLUDE_OBD_RESPONSES
                        };
                        if (can_sniffer_start(&auto_filter) == ESP_OK) {
                            ESP_LOGI(TAG, "🔴 CAN Sniffer AUTO-STARTED (hybrid mode)");
                            ESP_LOGI(TAG, "   ⚡ Capturing ALL CAN traffic for reverse engineering");
                            current_state = STATE_HYBRID_MODE;
                            sniffer_auto_started = true;
                        } else {
                            ESP_LOGW(TAG, "Failed to auto-start sniffer");
                        }
                        #endif
                    } else {
                        current_state = STATE_ERROR;
                    }
                }
                break;
                
            case STATE_IGNITION_ON:
            case STATE_LOGGING:
            case STATE_HYBRID_MODE:
                if (!ignition) {
                    ESP_LOGI(TAG, "🔑 Ignition OFF detected");
                    
                    // Close OBD session file
                    close_session_file();
                    
                    // === AUTO-STOP SNIFFER (NEW!) ===
                    if (sniffer_auto_started || can_sniffer_get_state() != SNIFF_STATE_IDLE) {
                        can_sniffer_stop();
                        ESP_LOGI(TAG, "⚫ CAN Sniffer AUTO-STOPPED");
                        sniffer_auto_started = false;
                    }
                    
                    pid_scheduler_log_stats();  // Log stats ao final da sessão
                    current_state = STATE_IGNITION_OFF;
                } else {
                    // Salva dados OBD
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
                // Só sniffing (sem OBD logging) - raro, mas suportado
                if (!ignition) {
                    can_sniffer_stop();
                    sniffer_auto_started = false;
                    current_state = STATE_IGNITION_OFF;
                }
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
            
            // Status principal com dados de todos os tiers
            ESP_LOGI(TAG, "[%lu] State:%s | RPM:%.0f Spd:%d Tmp:%d°C Load:%.1f%% | Rec:%lu | Heap:%luKB",
                     cycle_count,
                     state_names[current_state],
                     ext_telemetry.rpm,
                     ext_telemetry.speed,
                     ext_telemetry.coolant_temp,
                     ext_telemetry.engine_load,
                     records_in_session,
                     esp_get_free_heap_size() / 1024);
            
            // Dados adicionais dos tiers
            ESP_LOGI(TAG, "       Throttle:%.1f%% MAF:%.2fg/s Fuel:%.1f%% Volt:%.2fV",
                     ext_telemetry.throttle_pos,
                     ext_telemetry.maf_rate,
                     ext_telemetry.fuel_level,
                     ext_telemetry.control_voltage);
            
            // Atualiza web server (usando snapshot para compatibilidade)
            web_server_update_telemetry(&snapshot);
            web_server_update_ext_telemetry(&ext_telemetry);
            web_server_update_state(current_state, records_in_session);
        }
        
        // Log estatísticas do scheduler a cada 60 segundos
        if (now - last_stats > 60000) {
            last_stats = now;
            pid_scheduler_log_stats();
        }
        
        vTaskDelay(pdMS_TO_TICKS(50)); // 20Hz loop base
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
    const char *base_path = storage_manager_get_base_path();
    if (!base_path) {
        ESP_LOGE(TAG, "Storage not initialized");
        return;
    }
    
    ESP_LOGI(TAG, "=== Session Files (%s) ===", 
             storage_manager_is_sd_active() ? "SD Card" : "SPIFFS");
    
    DIR *dir = opendir(base_path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory: %s", base_path);
        return;
    }
    
    struct dirent *entry;
    struct stat st;
    char filepath[320];
    int file_count = 0;
    size_t total_size = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        snprintf(filepath, sizeof(filepath), "%s/%s", base_path, entry->d_name);
        
        if (stat(filepath, &st) == 0 && S_ISREG(st.st_mode)) {
            file_count++;
            total_size += st.st_size;
            ESP_LOGI(TAG, "  %s - %.2f KB", entry->d_name, st.st_size / 1024.0f);
        }
    }
    
    closedir(dir);
    
    // Get storage stats
    storage_stats_t stats;
    if (storage_manager_get_stats(&stats) == ESP_OK) {
        ESP_LOGI(TAG, "Total: %d files, %.2f MB used, %.2f MB free (%.1f%% used)", 
                 file_count, 
                 stats.used_bytes / (1024.0f * 1024.0f),
                 stats.free_bytes / (1024.0f * 1024.0f),
                 stats.percent_used);
    }
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
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_init();
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
    esp_log_level_set("STORAGE", ESP_LOG_INFO);
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   Smart OBD2 Logger v2.0               ║");
    ESP_LOGI(TAG, "║   Hardware: SN65HVD230 + SD Card       ║");
    ESP_LOGI(TAG, "║   Auto OBD + Sniffer on Ignition       ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
    
    // Init NVS
    ESP_ERROR_CHECK(nvs_flash_init());
    
    // Init Storage (SD Card with SPIFFS fallback)
    ESP_LOGI(TAG, "Initializing Storage...");
    ESP_ERROR_CHECK(init_storage());
    
    // Lista sessões anteriores
    list_session_files();

    
    // Init CAN PRIMEIRO! (antes do WiFi)
    ESP_LOGI(TAG, "Initializing CAN (SN65HVD230 transceiver)...");
    ESP_LOGI(TAG, "  TX=GPIO%d, RX=GPIO%d", CAN_TX_PIN, CAN_RX_PIN);
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
        #if AUTO_START_SNIFFER_ON_IGNITION
        ESP_LOGI(TAG, "✓ Sniffer ready (AUTO-START on ignition enabled)");
        #else
        ESP_LOGI(TAG, "✓ Sniffer ready (manual start via BOOT button or API)");
        #endif
    }

    // Init WiFi (não-bloqueante)
    ESP_LOGI(TAG, "Initializing WiFi (background)...");
    ESP_ERROR_CHECK(wifi_manager_init());
    wifi_connect(WIFI_SSID, WIFI_PASSWORD);

    // Start tasks IMEDIATAMENTE (não espera WiFi!)
    ESP_LOGI(TAG, "✓ System ready - Starting tasks");
    xTaskCreate(led_task, "LED", 2048, NULL, 5, NULL);
    xTaskCreate(obd_task, "OBD", 12288, NULL, 6, NULL);
    xTaskCreate(sniffer_task, "SNIFF", 8192, NULL, 7, NULL);
    xTaskCreate(button_task, "BUTTON", 2048, NULL, 4, NULL);

    // Task separada para iniciar web server quando WiFi conectar
    xTaskCreate(wifi_web_task, "WIFI_WEB", 6144, NULL, 3, NULL);

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   LED Patterns:                        ║");
    ESP_LOGI(TAG, "║   - Slow blink: Ignition OFF           ║");
    ESP_LOGI(TAG, "║   - Medium blink: OBD Logging only     ║");
    ESP_LOGI(TAG, "║   - 2 blinks + pause: HYBRID MODE      ║");
    ESP_LOGI(TAG, "║     (OBD + CAN Sniffing active)        ║");
    ESP_LOGI(TAG, "║   - 3 quick + pause: Storage Low       ║");
    ESP_LOGI(TAG, "║                                        ║");
    ESP_LOGI(TAG, "║   🔴 BOOT button: toggle sniffer       ║");
    ESP_LOGI(TAG, "║   📁 Storage: %s            ║", 
             storage_manager_is_sd_active() ? "SD Card   " : "SPIFFS    ");
    ESP_LOGI(TAG, "║   🚗 Auto-start: OBD + Sniffer         ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════╝");
}