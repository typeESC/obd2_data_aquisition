/**
 * @file obd_can.c
 * @brief CAN communication implementation for OBD-II
 * @date 2025
 * 
 * REFATORADO: Melhor debug, código mais claro e recebimento permissivo
 */

#include "obd_can.h"
#include "obd_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "OBD_CAN";
static bool can_initialized = false;
static SemaphoreHandle_t can_mutex = NULL;

// ============================================================================
// CONTADORES DE ESTATÍSTICAS (expandidos para melhor debug)
// ============================================================================
static uint32_t tx_error_count = 0;
static uint32_t rx_error_count = 0;
static uint32_t arb_lost_count = 0;
static uint32_t bus_error_count = 0;
static uint32_t pid_mismatch_count = 0;  // Novo: conta PIDs que não correspondem
static uint32_t invalid_id_count = 0;    // Novo: conta IDs fora do range OBD
static uint32_t timeout_count = 0;       // Novo: conta timeouts
static uint32_t success_count = 0;       // Novo: conta sucessos

// ============================================================================
// FUNÇÕES AUXILIARES DE DEBUG
// ============================================================================

/**
 * @brief Converte array de bytes em string hexadecimal para debug
 * @param data Array de bytes
 * @param len Quantidade de bytes
 * @param output Buffer de saída (mínimo 3*len bytes)
 */
static void bytes_to_hex_string(const uint8_t *data, size_t len, char *output) {
    for (size_t i = 0; i < len; i++) {
        snprintf(output + (i * 3), 4, "%02X ", data[i]);
    }
    if (len > 0) {
        output[(len * 3) - 1] = '\0';  // Remove último espaço
    }
}

/**
 * @brief Imprime mensagem CAN de forma legível para debug
 * @param msg Ponteiro para mensagem TWAI
 * @param direction "TX" para envio, "RX" para recepção
 */
static void debug_print_can_message(const twai_message_t *msg, const char *direction) {
    char hex_data[64] = {0};
    bytes_to_hex_string(msg->data, msg->data_length_code, hex_data);
    
    ESP_LOGI(TAG, "═══ CAN %s ═══════════════════════════════", direction);
    ESP_LOGI(TAG, "  ID:     0x%03lX", msg->identifier);
    ESP_LOGI(TAG, "  Length: %d bytes", msg->data_length_code);
    ESP_LOGI(TAG, "  Data:   [ %s]", hex_data);
    ESP_LOGI(TAG, "═══════════════════════════════════════════");
}

/**
 * @brief Valida se o ID está no range de respostas OBD-II (0x7E8-0x7EF)
 * @param identifier ID da mensagem CAN
 * @return true se válido, false caso contrário
 */
static bool is_valid_obd_response_id(uint32_t identifier) {
    // Respostas OBD vêm das ECUs com IDs de 0x7E8 até 0x7EF
    return (identifier >= OBD_RESPONSE_ID && identifier <= (OBD_RESPONSE_ID + 7));
}

/**
 * @brief Interpreta e explica detalhadamente uma mensagem OBD recebida
 * @param msg Ponteiro para mensagem recebida
 * @param expected_pid PID que foi solicitado (para comparação)
 */
static void explain_obd_message(const twai_message_t *msg, uint8_t expected_pid) {
    ESP_LOGI(TAG, "┌─ ANÁLISE DA MENSAGEM ─────────────────────");
    
    // Validação 1: Verifica ID
    if (is_valid_obd_response_id(msg->identifier)) {
        uint32_t ecu_number = msg->identifier - OBD_RESPONSE_ID;
        ESP_LOGI(TAG, "│ ✓ ID válido: 0x%03lX (ECU #%lu)", msg->identifier, ecu_number);
    } else {
        ESP_LOGW(TAG, "│ ✗ ID inválido: 0x%03lX (esperado 0x7E8-0x7EF)", msg->identifier);
    }
    
    // Validação 2: Verifica tamanho mínimo
    if (msg->data_length_code < 3) {
        ESP_LOGW(TAG, "│ ✗ Mensagem muito curta: %d bytes (mínimo 3)", msg->data_length_code);
        ESP_LOGI(TAG, "└───────────────────────────────────────────");
        return;
    }
    
    // Interpreta cada byte da mensagem OBD
    uint8_t num_bytes = msg->data[0];
    uint8_t mode = msg->data[1];
    uint8_t pid = msg->data[2];
    
    ESP_LOGI(TAG, "│ Byte 0 (Length): 0x%02X (%d bytes de dados)", num_bytes, num_bytes);
    
    // Validação 3: Verifica modo (deve ser 0x41 para resposta do modo 01)
    if (mode == (OBD_MODE_CURRENT + 0x40)) {
        ESP_LOGI(TAG, "│ Byte 1 (Mode):   0x%02X ✓ (Resposta Modo 01)", mode);
    } else {
        ESP_LOGW(TAG, "│ Byte 1 (Mode):   0x%02X ✗ (Esperado 0x%02X)", mode, OBD_MODE_CURRENT + 0x40);
    }
    
    // Validação 4: Verifica PID
    if (pid == expected_pid) {
        ESP_LOGI(TAG, "│ Byte 2 (PID):    0x%02X ✓ (Corresponde ao solicitado)", pid);
    } else {
        ESP_LOGW(TAG, "│ Byte 2 (PID):    0x%02X ✗ (Esperado 0x%02X)", pid, expected_pid);
    }
    
    // Mostra dados do PID (bytes 3 em diante)
    if (msg->data_length_code > 3) {
        ESP_LOGI(TAG, "│ Dados do PID:");
        for (int i = 3; i < msg->data_length_code; i++) {
            ESP_LOGI(TAG, "│   Byte %d: 0x%02X (%d decimal)", i, msg->data[i], msg->data[i]);
        }
    }
    
    ESP_LOGI(TAG, "└───────────────────────────────────────────");
}

/**
 * @brief Limpa o buffer de recepção CAN (remove mensagens antigas/atrasadas)
 * Essencial para evitar PID mismatch causado por mensagens antigas na fila
 */
static void flush_rx_buffer(void) {
    twai_message_t temp_msg;
    int flushed = 0;
    
    // Drena todas as mensagens pendentes na fila RX
    while (twai_receive(&temp_msg, pdMS_TO_TICKS(0)) == ESP_OK) {
        flushed++;
        DEBUG_LOG("  Descartando mensagem antiga (ID: 0x%03lX)", temp_msg.identifier);
    }
    
    if (flushed > 0) {
        ESP_LOGD(TAG, "Buffer limpo: %d mensagens antigas removidas", flushed);
    }
}

// ============================================================================
// IMPLEMENTAÇÃO DAS FUNÇÕES PÚBLICAS
// ============================================================================

esp_err_t obd_can_init(void)
{
    if (can_initialized) {
        ESP_LOGW(TAG, "CAN já inicializado");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Inicializando interface CAN...");

    // Cria mutex para garantir thread-safety
    can_mutex = xSemaphoreCreateMutex();
    if (can_mutex == NULL) {
        ESP_LOGE(TAG, "✗ Falha ao criar mutex CAN");
        return ESP_FAIL;
    }

    // Configuração de timing: 500 kbps (padrão OBD-II)
    twai_timing_config_t timing_config = CAN_TIMING_CONFIG_500KBITS();
    
    // Configure CAN filter para aceitar APENAS respostas OBD (0x7E8 a 0x7EF)
    // Isso é um filtro de hardware para rejeitar todo o "lixo" do barramento.
    twai_filter_config_t filter_config = {
        .acceptance_code = (uint32_t)(0x7E8 << 21),
        .acceptance_mask = (uint32_t)(~(0x7 << 21)), // Mascara os últimos 3 bits
        .single_filter = true
    };

    // Configuração geral usando suas definições
    twai_general_config_t general_config = CAN_GENERAL_CONFIG_DEFAULT(
        CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
    
    // Aumenta tamanho das filas para não perder mensagens
    general_config.tx_queue_len = 20;
    general_config.rx_queue_len = 50;

    // Instala o driver TWAI
    esp_err_t ret = twai_driver_install(&general_config, &timing_config, &filter_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "✗ Falha ao instalar driver TWAI: %s", esp_err_to_name(ret));
        vSemaphoreDelete(can_mutex);
        can_mutex = NULL;
        return ret;
    }

    // Inicia o driver TWAI
    ret = twai_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "✗ Falha ao iniciar driver TWAI: %s", esp_err_to_name(ret));
        twai_driver_uninstall();
        vSemaphoreDelete(can_mutex);
        can_mutex = NULL;
        return ret;
    }

    can_initialized = true;
    obd_can_reset_stats();
    
    ESP_LOGI(TAG, "✓ Interface CAN inicializada com sucesso!");
    ESP_LOGI(TAG, "  - Pino TX: GPIO %d", CAN_TX_PIN);
    ESP_LOGI(TAG, "  - Pino RX: GPIO %d", CAN_RX_PIN);
    ESP_LOGI(TAG, "  - Velocidade: 500 kbps");
    
    return ESP_OK;
}

esp_err_t obd_can_deinit(void)
{
    if (!can_initialized) {
        return ESP_OK;
    }

    if (xSemaphoreTake(can_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        twai_stop();
        twai_driver_uninstall();
        
        can_initialized = false;
        xSemaphoreGive(can_mutex);
        vSemaphoreDelete(can_mutex);
        can_mutex = NULL;
        
        ESP_LOGI(TAG, "Interface CAN desinicializada");
        return ESP_OK;
    }
    
    ESP_LOGE(TAG, "Falha ao adquirir mutex para deinit");
    return ESP_FAIL;
}

/**
 * @brief Requisita um PID OBD-II e aguarda resposta com validação completa
 * 
 * FLUXO DE OPERAÇÃO:
 * 1. Valida parâmetros e adquire mutex
 * 2. Limpa buffer de mensagens antigas (previne mismatch)
 * 3. Envia requisição para 0x7DF (broadcast para todas ECUs)
 * 4. Tenta receber até 5 mensagens (permite múltiplas ECUs respondendo)
 * 5. Valida cada mensagem (ID, tamanho, modo, PID)
 * 6. Retorna dados ou erro detalhado
 * 
 * @param pid PID a requisitar (ex: 0x0C para RPM)
 * @param response Buffer para armazenar dados da resposta (sem cabeçalho)
 * @param response_len Entrada: tamanho do buffer / Saída: bytes lidos
 * @return ESP_OK se sucesso, código de erro caso contrário
 */
esp_err_t obd_can_request(uint8_t pid, uint8_t *response, size_t *response_len)
{
    // ========================================================================
    // VALIDAÇÕES INICIAIS
    // ========================================================================
    if (!can_initialized || response == NULL || response_len == NULL) {
        ESP_LOGE(TAG, "✗ Parâmetros inválidos ou CAN não inicializado");
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(can_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "✗ Timeout ao adquirir mutex CAN");
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;
    
    // ========================================================================
    // PASSO 1: LIMPA BUFFER (Remove mensagens antigas que causam mismatch)
    // ========================================================================
    ESP_LOGD(TAG, "[1/4] Limpando buffer de recepção...");
    flush_rx_buffer();

    // ========================================================================
    // PASSO 2: PREPARA E ENVIA REQUISIÇÃO
    // ========================================================================
    ESP_LOGD(TAG, "[2/4] Preparando requisição OBD...");
    
    twai_message_t tx_msg = {0};
    tx_msg.identifier = OBD_REQUEST_ID;    // 0x7DF - broadcast para todas ECUs
    tx_msg.data_length_code = 8;
    tx_msg.data[0] = 0x02;                 // 2 bytes adicionais (mode + PID)
    tx_msg.data[1] = OBD_MODE_CURRENT;     // Modo 01: dados em tempo real
    tx_msg.data[2] = pid;                  // PID requisitado
    tx_msg.data[3] = 0x55;                 // Padding (padrão)
    tx_msg.data[4] = 0x55;
    tx_msg.data[5] = 0x55;
    tx_msg.data[6] = 0x55;
    tx_msg.data[7] = 0x55;

    // Mostra mensagem que será enviada (útil para debug)
    //debug_print_can_message(&tx_msg, "TX");

    // Inicia timer de performance (se debug habilitado)
    OBD_PERF_START();

    // Envia a requisição
    ret = twai_transmit(&tx_msg, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        tx_error_count++;
        //ESP_LOGE(TAG, "✗ Falha ao enviar requisição: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    ESP_LOGI(TAG, "Requisição OBD-II enviada com sucesso - PID 0x%02X", pid);
    // ========================================================================
    // PASSO 3: AGUARDA E PROCESSA RESPOSTA (COM MÚLTIPLAS TENTATIVAS)
    // ========================================================================
    ESP_LOGD(TAG, "[3/4] Aguardando resposta...");

    // Controle de tempo: começa a contar agora
    uint32_t start_time = esp_timer_get_time() / 1000;  // Timestamp em milissegundos
    bool found_response = false;
    twai_message_t rx_msg = {0};
    int attempt = 0;

    // Loop enquanto não exceder o tempo máximo total OBD_MAX_WAIT_TIME
    while ((esp_timer_get_time() / 1000) - start_time < OBD_MAX_WAIT_TIME) {
        attempt++;
        
        // Tenta receber uma mensagem (timeout curto: 20ms por tentativa)
        ret = twai_receive(&rx_msg, pdMS_TO_TICKS(OBD_RESPONSE_TIMEOUT));
        
        // Caso 1: Timeout nesta tentativa - continua tentando
        if (ret == ESP_ERR_TIMEOUT) {
            continue;  // Volta ao while e tenta de novo
        }
        
        // Caso 2: Erro ao receber
        if (ret != ESP_OK) {
            rx_error_count++;
            //ESP_LOGW(TAG, "✗ Erro ao receber: %s (tentativa %d)", 
            //        esp_err_to_name(ret), attempt);
            continue;  // Tenta próxima mensagem
        }

        // Caso 3: Mensagem recebida! Mostra no log
        //ESP_LOGI(TAG, "");
        //ESP_LOGI(TAG, "→ Mensagem recebida (tentativa %d, tempo decorrido: %lu ms):", 
        //        attempt, (esp_timer_get_time() / 1000) - start_time);
        //debug_print_can_message(&rx_msg, "RX");
        //explain_obd_message(&rx_msg, pid);

        // ====================================================================
        // PASSO 4: VALIDA RESPOSTA (4 validações sequenciais)
        // ====================================================================
        ESP_LOGD(TAG, "[4/4] Validando resposta...");
        
        if ((is_valid_obd_response_id(rx_msg.identifier)) && (rx_msg.data[1] == (OBD_MODE_CURRENT + 0x40)) && (rx_msg.data[2] == pid)) {
        OBD_PERF_END("OBD request");
        ESP_LOGI(TAG, "║  ✓ RESPOSTA VÁLIDA, Tempo total: %lu ms, Tentativas: %d", (esp_timer_get_time() / 1000) - start_time, attempt);
        
        found_response = true;
        success_count++;        
        // Extrai dados da resposta (remove cabeçalho: length, mode, PID)
        size_t data_len = rx_msg.data_length_code - 3;
        if (data_len > *response_len) {
            ESP_LOGW(TAG, "Buffer de resposta pequeno: copiando apenas %zu de %zu bytes", 
                    *response_len, data_len);
            data_len = *response_len;
        }
        
        memcpy(response, &rx_msg.data[3], data_len);
        *response_len = data_len;
        
        // Mostra dados extraídos
        char hex_data[64] = {0};
        bytes_to_hex_string(response, data_len, hex_data);
        ESP_LOGI(TAG, "Dados extraídos (%zu bytes): [ %s]", data_len, hex_data);
        ret = ESP_OK;
        }
        break;  // Sai do loop while

    }

    // Se não encontrou resposta válida após timeout total
    if (!found_response) {
        timeout_count++;
        rx_error_count++;
        //ESP_LOGE(TAG, "  ✗ NENHUMA RESPOSTA VÁLIDA ENCONTRADA");
        ret = ESP_ERR_NOT_FOUND;
    }

cleanup:
    xSemaphoreGive(can_mutex);
    
    // Mostra estatísticas resumidas após cada requisição
    //ESP_LOGI(TAG, "");
    //ESP_LOGI(TAG, "─── Estatísticas Acumuladas ───────────────");
    //ESP_LOGI(TAG, "Sucessos:      %lu", success_count);
    //ESP_LOGI(TAG, "Erros TX:      %lu", tx_error_count);
    //ESP_LOGI(TAG, "Erros RX:      %lu", rx_error_count);
    //ESP_LOGI(TAG, "Timeouts:      %lu", timeout_count);
    //ESP_LOGI(TAG, "PID mismatch:  %lu", pid_mismatch_count);
    //ESP_LOGI(TAG, "IDs inválidos: %lu", invalid_id_count);
    
    // Calcula taxa de sucesso se houver tentativas
    if (success_count + rx_error_count > 0) {
        //float success_rate = (float)success_count / (success_count + rx_error_count) * 100.0f;
        //ESP_LOGI(TAG, "Taxa de sucesso: %.1f%%", success_rate);
    }
    
    //ESP_LOGI(TAG, "───────────────────────────────────────────");
    //ESP_LOGI(TAG, "");
    
    return ret;
}

bool obd_can_is_ready(void)
{
    if (!can_initialized) {
        return false;
    }

    twai_status_info_t status;
    esp_err_t ret = twai_get_status_info(&status);
    
    if (ret != ESP_OK) {
        return false;
    }

    // Verifica se driver está rodando e sem muitos erros
    bool is_running = (status.state == TWAI_STATE_RUNNING);
    bool low_errors = (status.bus_error_count < 100 && 
                       status.tx_error_counter < 100 && 
                       status.rx_error_counter < 100);
    
    return is_running && low_errors;
}

void obd_can_get_stats(uint32_t *tx_errors, uint32_t *rx_errors, 
                       uint32_t *arb_lost, uint32_t *bus_errors)
{
    if (tx_errors) *tx_errors = tx_error_count;
    if (rx_errors) *rx_errors = rx_error_count;
    if (arb_lost) *arb_lost = arb_lost_count;
    if (bus_errors) *bus_errors = bus_error_count;

    // Adiciona estatísticas do driver TWAI
    if (can_initialized) {
        twai_status_info_t status;
        if (twai_get_status_info(&status) == ESP_OK) {
            if (bus_errors) *bus_errors += status.bus_error_count;
        }
    }
}

void obd_can_reset_stats(void)
{
    tx_error_count = 0;
    rx_error_count = 0;
    arb_lost_count = 0;
    bus_error_count = 0;
    pid_mismatch_count = 0;
    invalid_id_count = 0;
    timeout_count = 0;
    success_count = 0;

    if (can_initialized) {
        twai_clear_transmit_queue();
        twai_clear_receive_queue();
    }
    
    ESP_LOGI(TAG, "✓ Estatísticas resetadas");
}