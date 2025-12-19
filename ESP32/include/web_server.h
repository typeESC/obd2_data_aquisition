/**
 * @file web_server.h
 * @brief Servidor web HTTP para controle e download de logs
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "obd_config.h"
#include "esp_err.h"

/**
 * @brief Inicializa e inicia o servidor web
 * @param device_ip String com IP do dispositivo para exibição
 * @return ESP_OK em sucesso
 */
esp_err_t web_server_start(const char *device_ip);

/**
 * @brief Para o servidor web
 * @return ESP_OK em sucesso
 */
esp_err_t web_server_stop(void);

/**
 * @brief Atualiza dados de telemetria para exibição
 * @param data Dados atuais de telemetria
 */
void web_server_update_telemetry(const telemetry_data_t *data);

/**
 * @brief Atualiza estado do sistema para exibição
 * @param state Estado atual (0=INIT, 1=IGN_OFF, 2=IGN_ON, 3=LOGGING, 4=ERROR)
 * @param records_count Número de registros na sessão atual
 */
void web_server_update_state(int state, uint32_t records_count);

#endif // WEB_SERVER_H