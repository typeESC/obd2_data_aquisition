/**
 * @file obd_parser.h
 * @brief OBD-II data parsing utilities
 * @author Mario Venere Neto
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
 * @brief Convert temperature from Celsius to Fahrenheit
 * @param celsius Temperature in Celsius
 * @return Temperature in Fahrenheit
 */
static inline float celsius_to_fahrenheit(int celsius)
{
    return (celsius * 9.0f / 5.0f) + 32.0f;
}

/**
 * @brief Convert speed from km/h to mph
 * @param kmh Speed in km/h
 * @return Speed in mph
 */
static inline float kmh_to_mph(int kmh)
{
    return kmh * 0.621371f;
}

/**
 * @brief Convert fuel consumption from L/h to mpg (rough estimation)
 * @param lph Fuel consumption in L/h
 * @param speed Current speed in km/h
 * @return Rough MPG estimation
 */
static inline float lph_to_mpg(float lph, int speed)
{
    if (lph <= 0 || speed <= 0) return 0;
    float kmpl = speed / lph;
    return kmpl * 2.352f;  // Convert km/L to MPG
}

#endif // OBD_PARSER_H
