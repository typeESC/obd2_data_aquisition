/**
 * @file obd_can.h
 * @brief CAN communication interface for OBD-II
 * @author Mario Venere Neto
 * @date 2025
 */

#ifndef OBD_CAN_H
#define OBD_CAN_H

#include "obd_config.h"
#include "driver/twai.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// === CAN CONFIGURATION ===
#define CAN_TIMING_CONFIG_500KBITS()    TWAI_TIMING_CONFIG_500KBITS()
#define CAN_FILTER_CONFIG_ACCEPT_ALL()  TWAI_FILTER_CONFIG_ACCEPT_ALL()
#define CAN_GENERAL_CONFIG_DEFAULT(tx_pin, rx_pin, mode) TWAI_GENERAL_CONFIG_DEFAULT(tx_pin, rx_pin, mode)

// === OBD-II CAN IDENTIFIERS ===
#define OBD_REQUEST_ID      0x7DF          // Standard OBD-II request ID
#define OBD_RESPONSE_ID     0x7E8          // Standard OBD-II response ID (ECU 1)

// === OBD-II MESSAGE STRUCTURE ===
#define OBD_MODE_CURRENT    0x01           // Current data mode
#define OBD_MODE_FREEZE     0x02           // Freeze frame data mode
#define OBD_MODE_CODES      0x03           // Diagnostic trouble codes
#define OBD_MODE_CLEAR      0x04           // Clear codes mode

/**
 * @brief Initialize CAN interface for OBD communication
 * @return ESP_OK on success, error code on failure
 */
esp_err_t obd_can_init(void);

/**
 * @brief Deinitialize CAN interface
 * @return ESP_OK on success, error code on failure
 */
esp_err_t obd_can_deinit(void);

/**
 * @brief Send OBD request and wait for response
 * @param pid PID to request
 * @param response Buffer to store response data
 * @param response_len Length of response buffer
 * @return ESP_OK on success, error code on failure
 */
esp_err_t obd_can_request(uint8_t pid, uint8_t *response, size_t *response_len);

/**
 * @brief Check if CAN interface is ready
 * @return true if ready, false otherwise
 */
bool obd_can_is_ready(void);

/**
 * @brief Get CAN error statistics
 * @param tx_errors Pointer to store TX error count
 * @param rx_errors Pointer to store RX error count
 * @param arb_lost Pointer to store arbitration lost count
 * @param bus_errors Pointer to store bus error count
 */
void obd_can_get_stats(uint32_t *tx_errors, uint32_t *rx_errors, uint32_t *arb_lost, uint32_t *bus_errors);

/**
 * @brief Reset CAN error counters
 */
void obd_can_reset_stats(void);

#endif // OBD_CAN_H
