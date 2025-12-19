/**
 * @file data_logger.h
 * @brief Gerenciamento de logging de dados OBD em SPIFFS
 */

#ifndef DATA_LOGGER_H
#define DATA_LOGGER_H

#include "obd_config.h"
#include "esp_err.h"
#include <stdbool.h>

#define LOG_FILE_PATH "/spiffs/obd_log.csv"

/**
 * @brief Estatísticas do logger
 */
typedef struct {
    uint32_t records_saved;
    uint32_t file_size_bytes;
    bool is_logging;
} logger_stats_t;

/**
 * @brief Inicializa o sistema de logging (monta SPIFFS)
 * @return ESP_OK em sucesso
 */
esp_err_t logger_init(void);

/**
 * @brief Inicia o logging (abre arquivo CSV)
 * @return ESP_OK em sucesso
 */
esp_err_t logger_start(void);

/**
 * @brief Para o logging (fecha arquivo)
 * @return ESP_OK em sucesso
 */
esp_err_t logger_stop(void);

/**
 * @brief Salva registro de telemetria no CSV
 * @param data Dados de telemetria
 * @return ESP_OK em sucesso
 */
esp_err_t logger_save_record(const telemetry_data_t *data);

/**
 * @brief Apaga o arquivo de log
 * @return ESP_OK em sucesso
 */
esp_err_t logger_delete_file(void);

/**
 * @brief Obtém estatísticas do logger
 * @param stats Ponteiro para estrutura de stats
 * @return ESP_OK em sucesso
 */
esp_err_t logger_get_stats(logger_stats_t *stats);

/**
 * @brief Verifica se está logando
 * @return true se logging ativo
 */
bool logger_is_active(void);

/**
 * @brief Obtém caminho do arquivo de log
 * @return String com caminho
 */
const char* logger_get_file_path(void);

#endif // DATA_LOGGER_H