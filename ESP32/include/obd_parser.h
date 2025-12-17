/**
 * @file obd_parser.h
 * @brief OBD-II data parsing utilities
 * @date 2025
 */

#ifndef OBD_PARSER_H
#define OBD_PARSER_H

#include "obd_config.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Parse OBD response data into telemetry structure
 * @param pid OBD PID that was requested
 * @param response Raw response data from OBD
 * @param response_len Length of response data
 * @param telemetry Pointer to telemetry structure to populate
 * @return true if parsing successful, false otherwise
 */
bool obd_parse_response(uint8_t pid, const uint8_t *response, size_t response_len, telemetry_data_t *telemetry);

/**
 * @brief Validate telemetry data for reasonableness
 * @param telemetry Pointer to telemetry data to validate
 * @return true if data is valid, false otherwise
 */
bool obd_validate_telemetry(const telemetry_data_t *telemetry);

/**
 * @brief Initialize telemetry data structure with defaults
 * @param telemetry Pointer to telemetry structure to initialize
 */
void obd_init_telemetry(telemetry_data_t *telemetry);

/**
 * @brief Log telemetry data for debugging
 * @param telemetry Pointer to telemetry data to log
 */
void obd_log_telemetry(const telemetry_data_t *telemetry);

/**
 * @brief Convert fuel consumption from L/h to mpg (rough estimation)
 * @param lph Fuel consumption in L/h
 * @param speed Current speed in km/h
 * @return Rough MPG estimation
 */
static inline float lph(float kmh, int speed)
{
    if (kmh <= 0 || speed <= 0) return 0;
    float kmpl = speed / kmh;
    return kmpl;  // Convert km/L to MPG
}

#endif // OBD_PARSER_H
