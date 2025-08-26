/**
 * @file obd_parser.c
 * @brief OBD-II data parsing implementation
 * @author Mario Venere Neto
 * @date 2025
 */

#include "obd_parser.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <math.h>

static const char *TAG = "OBD_PARSER";

bool obd_parse_response(uint8_t pid, const uint8_t *response, size_t response_len, telemetry_data_t *telemetry)
{
    if (response == NULL || telemetry == NULL || response_len == 0) {
        return false;
    }

    // Update timestamp
    telemetry->device_timestamp = esp_timer_get_time() / 1000; // Convert to milliseconds

    switch (pid) {
        case PID_ENGINE_LOAD:
            if (response_len >= 1) {
                telemetry->engine_load = (response[0] * 100.0f) / 255.0f;
                return IS_VALID_PERCENTAGE(telemetry->engine_load);
            }
            break;

        case PID_COOLANT_TEMP:
            if (response_len >= 1) {
                telemetry->coolant_temp = response[0] - 40;
                return IS_VALID_TEMP(telemetry->coolant_temp);
            }
            break;

        case PID_RPM:
            if (response_len >= 2) {
                telemetry->rpm = ((response[0] * 256.0f) + response[1]) / 4.0f;
                return IS_VALID_RPM(telemetry->rpm);
            }
            break;

        case PID_SPEED:
            if (response_len >= 1) {
                telemetry->speed = response[0];
                return IS_VALID_SPEED(telemetry->speed);
            }
            break;

        case PID_TIMING_ADVANCE:
            if (response_len >= 1) {
                telemetry->timing_advance = (response[0] / 2.0f) - 64.0f;
                return (telemetry->timing_advance >= -64.0f && telemetry->timing_advance <= 63.5f);
            }
            break;

        case PID_INTAKE_AIR_TEMP:
            if (response_len >= 1) {
                telemetry->intake_air_temp = response[0] - 40;
                return IS_VALID_TEMP(telemetry->intake_air_temp);
            }
            break;

        case PID_MAF_RATE:
            if (response_len >= 2) {
                telemetry->maf_rate = ((response[0] * 256.0f) + response[1]) / 100.0f;
                return (telemetry->maf_rate >= 0 && telemetry->maf_rate <= 655.35f);
            }
            break;

        case PID_THROTTLE_POS:
            if (response_len >= 1) {
                telemetry->throttle_pos = (response[0] * 100.0f) / 255.0f;
                return IS_VALID_PERCENTAGE(telemetry->throttle_pos);
            }
            break;

        case PID_RUN_TIME:
            if (response_len >= 2) {
                telemetry->run_time = (response[0] * 256) + response[1];
                return (telemetry->run_time >= 0 && telemetry->run_time <= 65535);
            }
            break;

        case PID_DIST_SINCE_CLEAR:
            if (response_len >= 2) {
                telemetry->dist_since_clear = (response[0] * 256) + response[1];
                return (telemetry->dist_since_clear >= 0 && telemetry->dist_since_clear <= 65535);
            }
            break;

        case PID_FUEL_LEVEL:
            if (response_len >= 1) {
                telemetry->fuel_level = (response[0] * 100.0f) / 255.0f;
                return IS_VALID_PERCENTAGE(telemetry->fuel_level);
            }
            break;

        case PID_MODULE_VOLTAGE:
            if (response_len >= 2) {
                telemetry->module_voltage = ((response[0] * 256.0f) + response[1]) / 1000.0f;
                return IS_VALID_VOLTAGE(telemetry->module_voltage);
            }
            break;

        case PID_COMMANDED_LAMBDA:
            if (response_len >= 2) {
                telemetry->commanded_lambda = ((response[0] * 256.0f) + response[1]) / 32768.0f;
                return (telemetry->commanded_lambda >= 0 && telemetry->commanded_lambda <= 2.0f);
            }
            break;

        case PID_RELATIVE_THROTTLE:
            if (response_len >= 1) {
                telemetry->relative_throttle = (response[0] * 100.0f) / 255.0f;
                return IS_VALID_PERCENTAGE(telemetry->relative_throttle);
            }
            break;

        case PID_ETHANOL_PERCENTAGE:
            if (response_len >= 1) {
                telemetry->ethanol_percentage = (response[0] * 100.0f) / 255.0f;
                return IS_VALID_PERCENTAGE(telemetry->ethanol_percentage);
            }
            break;

        case PID_OIL_TEMP:
            if (response_len >= 1) {
                telemetry->oil_temp = response[0] - 40;
                return IS_VALID_TEMP(telemetry->oil_temp);
            }
            break;

        case PID_FUEL_RATE:
            if (response_len >= 2) {
                telemetry->fuel_rate = ((response[0] * 256.0f) + response[1]) / 20.0f;
                return (telemetry->fuel_rate >= 0 && telemetry->fuel_rate <= 3276.75f);
            }
            break;

        default:
            ESP_LOGW(TAG, "Unknown PID: 0x%02X", pid);
            return false;
    }

    ESP_LOGW(TAG, "Failed to parse PID 0x%02X (insufficient data: %d bytes)", pid, response_len);
    return false;
}

bool obd_validate_telemetry(const telemetry_data_t *telemetry)
{
    if (telemetry == NULL) {
        return false;
    }

    bool valid = true;
    int error_count = 0;

    // Check for obviously invalid values
    if (!IS_VALID_RPM(telemetry->rpm)) {
        DEBUG_LOG("Invalid RPM: %.1f", telemetry->rpm);
        error_count++;
    }

    if (!IS_VALID_SPEED(telemetry->speed)) {
        DEBUG_LOG("Invalid speed: %d", telemetry->speed);
        error_count++;
    }

    if (!IS_VALID_TEMP(telemetry->coolant_temp)) {
        DEBUG_LOG("Invalid coolant temp: %d", telemetry->coolant_temp);
        error_count++;
    }

    if (!IS_VALID_PERCENTAGE(telemetry->engine_load)) {
        DEBUG_LOG("Invalid engine load: %.1f", telemetry->engine_load);
        error_count++;
    }

    if (!IS_VALID_VOLTAGE(telemetry->module_voltage)) {
        DEBUG_LOG("Invalid module voltage: %.2f", telemetry->module_voltage);
        error_count++;
    }

    // Cross-validation checks
    if (telemetry->rpm > 500 && telemetry->speed == 0) {
        // Engine running but not moving - could be valid (idle, park)
        DEBUG_LOG("Engine running but not moving (RPM: %.0f, Speed: %d)", telemetry->rpm, telemetry->speed);
    }

    if (telemetry->speed > 0 && telemetry->rpm < 500) {
        // Moving but low RPM - suspicious
        ESP_LOGW(TAG, "Moving with low RPM (Speed: %d, RPM: %.0f)", telemetry->speed, telemetry->rpm);
        error_count++;
    }

    if (telemetry->coolant_temp > 100 && telemetry->coolant_temp < 120) {
        // High but not critical coolant temp
        DEBUG_LOG("High coolant temperature: %d°C", telemetry->coolant_temp);
    }

    // Allow some errors but flag if too many
    if (error_count > 3) {
        ESP_LOGW(TAG, "Telemetry validation failed with %d errors", error_count);
        valid = false;
    }

    return valid;
}

void obd_init_telemetry(telemetry_data_t *telemetry)
{
    if (telemetry == NULL) {
        return;
    }

    memset(telemetry, 0, sizeof(telemetry_data_t));
    
    // Set invalid values as defaults
    telemetry->device_timestamp = 0;
    telemetry->rpm = -1;
    telemetry->speed = -1;
    telemetry->coolant_temp = -999;
    telemetry->engine_load = -1;
    telemetry->timing_advance = -999;
    telemetry->intake_air_temp = -999;
    telemetry->maf_rate = -1;
    telemetry->throttle_pos = -1;
    telemetry->run_time = -1;
    telemetry->dist_since_clear = -1;
    telemetry->fuel_level = -1;
    telemetry->module_voltage = -1;
    telemetry->commanded_lambda = -1;
    telemetry->relative_throttle = -1;
    telemetry->ethanol_percentage = -1;
    telemetry->oil_temp = -999;
    telemetry->fuel_rate = -1;
    telemetry->valid = false;
}

void obd_log_telemetry(const telemetry_data_t *telemetry)
{
    if (telemetry == NULL) {
        return;
    }

    ESP_LOGI(TAG, "=== Telemetry Data ===");
    ESP_LOGI(TAG, "Timestamp: %llu ms", telemetry->device_timestamp);
    ESP_LOGI(TAG, "RPM: %.1f", telemetry->rpm);
    ESP_LOGI(TAG, "Speed: %d km/h (%.1f mph)", telemetry->speed, kmh_to_mph(telemetry->speed));
    ESP_LOGI(TAG, "Coolant Temp: %d°C (%.1f°F)", telemetry->coolant_temp, celsius_to_fahrenheit(telemetry->coolant_temp));
    ESP_LOGI(TAG, "Engine Load: %.1f%%", telemetry->engine_load);
    ESP_LOGI(TAG, "Timing Advance: %.1f°", telemetry->timing_advance);
    ESP_LOGI(TAG, "Intake Air Temp: %d°C (%.1f°F)", telemetry->intake_air_temp, celsius_to_fahrenheit(telemetry->intake_air_temp));
    ESP_LOGI(TAG, "MAF Rate: %.2f g/s", telemetry->maf_rate);
    ESP_LOGI(TAG, "Throttle Position: %.1f%%", telemetry->throttle_pos);
    ESP_LOGI(TAG, "Runtime: %d s", telemetry->run_time);
    ESP_LOGI(TAG, "Distance Since Clear: %d km", telemetry->dist_since_clear);
    ESP_LOGI(TAG, "Fuel Level: %.1f%%", telemetry->fuel_level);
    ESP_LOGI(TAG, "Module Voltage: %.2f V", telemetry->module_voltage);
    ESP_LOGI(TAG, "Commanded Lambda: %.3f", telemetry->commanded_lambda);
    ESP_LOGI(TAG, "Relative Throttle: %.1f%%", telemetry->relative_throttle);
    ESP_LOGI(TAG, "Ethanol Percentage: %.1f%%", telemetry->ethanol_percentage);
    ESP_LOGI(TAG, "Oil Temp: %d°C (%.1f°F)", telemetry->oil_temp, celsius_to_fahrenheit(telemetry->oil_temp));
    ESP_LOGI(TAG, "Fuel Rate: %.2f L/h", telemetry->fuel_rate);
    
    if (telemetry->speed > 0 && telemetry->fuel_rate > 0) {
        ESP_LOGI(TAG, "Estimated MPG: %.1f", lph_to_mpg(telemetry->fuel_rate, telemetry->speed));
    }
    
    ESP_LOGI(TAG, "Valid: %s", telemetry->valid ? "YES" : "NO");
    ESP_LOGI(TAG, "=====================");
}
