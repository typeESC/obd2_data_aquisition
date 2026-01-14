/**
 * @file pid_scheduler.c
 * @brief Priority-based PID polling scheduler implementation
 * @date 2025
 * 
 * Implements multi-tier polling for fleet management and predictive maintenance.
 * Supports 25 PIDs across 4 priority tiers plus DTC reading.
 */

#include "pid_scheduler.h"
#include "obd_can.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "PID_SCHED";

// =============================================================================
// PID DEFINITIONS BY TIER
// =============================================================================

// TIER 1 - CRITICAL (50-100ms) - Real-time data
static const pid_config_t TIER1_PIDS[] = {
    {0x0C, "RPM",           "rpm",  POLL_TIER_CRITICAL, 1, 
     PID_FLAG_FLEET_CRITICAL | PID_FLAG_DRIVER_BEHAVIOR, 
     2, 0.25f, 0.0f, 0, 8000},
    
    {0x0D, "Speed",         "km/h", POLL_TIER_CRITICAL, 2, 
     PID_FLAG_FLEET_CRITICAL | PID_FLAG_DRIVER_BEHAVIOR, 
     1, 1.0f, 0.0f, 0, 255},
    
    {0x11, "Throttle",      "%",    POLL_TIER_CRITICAL, 3, 
     PID_FLAG_DRIVER_BEHAVIOR | PID_FLAG_FUEL_EFFICIENCY, 
     1, 100.0f/255.0f, 0.0f, 0, 100},
    
    {0x04, "EngineLoad",    "%",    POLL_TIER_CRITICAL, 4, 
     PID_FLAG_FUEL_EFFICIENCY | PID_FLAG_PREDICTIVE_MAINT, 
     1, 100.0f/255.0f, 0.0f, 0, 100},
    
    {0x10, "MAF",           "g/s",  POLL_TIER_CRITICAL, 5, 
     PID_FLAG_FUEL_EFFICIENCY, 
     2, 0.01f, 0.0f, 0, 655},
};
#define TIER1_COUNT (sizeof(TIER1_PIDS) / sizeof(TIER1_PIDS[0]))

// TIER 2 - HIGH (200-500ms) - Operational data
static const pid_config_t TIER2_PIDS[] = {
    {0x05, "CoolantTemp",   "°C",   POLL_TIER_HIGH, 6, 
     PID_FLAG_FLEET_CRITICAL | PID_FLAG_PREDICTIVE_MAINT, 
     1, 1.0f, -40.0f, -40, 215},
    
    {0x0B, "ManifoldPress", "kPa",  POLL_TIER_HIGH, 7, 
     PID_FLAG_PREDICTIVE_MAINT, 
     1, 1.0f, 0.0f, 0, 255},
    
    {0x2F, "FuelLevel",     "%",    POLL_TIER_HIGH, 8, 
     PID_FLAG_FLEET_CRITICAL, 
     1, 100.0f/255.0f, 0.0f, 0, 100},
    
    {0x0F, "IntakeTemp",    "°C",   POLL_TIER_HIGH, 9, 
     PID_FLAG_FUEL_EFFICIENCY, 
     1, 1.0f, -40.0f, -40, 215},
    
    {0x1F, "Runtime",       "s",    POLL_TIER_HIGH, 10, 
     PID_FLAG_FLEET_CRITICAL, 
     2, 1.0f, 0.0f, 0, 65535},
};
#define TIER2_COUNT (sizeof(TIER2_PIDS) / sizeof(TIER2_PIDS[0]))

// TIER 3 - MEDIUM (1-5s) - Slow-changing data
static const pid_config_t TIER3_PIDS[] = {
    {0x42, "Voltage",       "V",    POLL_TIER_MEDIUM, 11, 
     PID_FLAG_FLEET_CRITICAL | PID_FLAG_PREDICTIVE_MAINT, 
     2, 0.001f, 0.0f, 0, 65},
    
    {0x5C, "OilTemp",       "°C",   POLL_TIER_MEDIUM, 12, 
     PID_FLAG_PREDICTIVE_MAINT, 
     1, 1.0f, -40.0f, -40, 210},
    
    {0x06, "FuelTrimShortB1", "%",  POLL_TIER_MEDIUM, 13, 
     PID_FLAG_PREDICTIVE_MAINT, 
     1, 100.0f/128.0f, -100.0f, -100, 100},
    
    {0x07, "FuelTrimLongB1", "%",   POLL_TIER_MEDIUM, 14, 
     PID_FLAG_PREDICTIVE_MAINT, 
     1, 100.0f/128.0f, -100.0f, -100, 100},
    
    {0x31, "DistanceClear", "km",   POLL_TIER_MEDIUM, 15, 
     PID_FLAG_FLEET_CRITICAL, 
     2, 1.0f, 0.0f, 0, 65535},
    
    {0x46, "AmbientTemp",   "°C",   POLL_TIER_MEDIUM, 16, 
     0, 
     1, 1.0f, -40.0f, -40, 215},
    
    {0x0E, "TimingAdvance", "°",    POLL_TIER_MEDIUM, 17, 
     PID_FLAG_PREDICTIVE_MAINT, 
     1, 0.5f, -64.0f, -64, 64},
};
#define TIER3_COUNT (sizeof(TIER3_PIDS) / sizeof(TIER3_PIDS[0]))

// TIER 4 - LOW (10-30s) - Diagnostic data
static const pid_config_t TIER4_PIDS[] = {
    {0x01, "MILStatus",     "",     POLL_TIER_LOW, 18, 
     PID_FLAG_FLEET_CRITICAL | PID_FLAG_DIAGNOSTIC, 
     4, 1.0f, 0.0f, 0, 255},
    
    {0x14, "O2_B1S1",       "V",    POLL_TIER_LOW, 19, 
     PID_FLAG_PREDICTIVE_MAINT | PID_FLAG_DIAGNOSTIC, 
     2, 0.005f, 0.0f, 0, 1275},
    
    {0x15, "O2_B1S2",       "V",    POLL_TIER_LOW, 20, 
     PID_FLAG_PREDICTIVE_MAINT | PID_FLAG_DIAGNOSTIC, 
     2, 0.005f, 0.0f, 0, 1275},
    
    {0x21, "DistanceMIL",   "km",   POLL_TIER_LOW, 21, 
     PID_FLAG_FLEET_CRITICAL | PID_FLAG_DIAGNOSTIC, 
     2, 1.0f, 0.0f, 0, 65535},
    
    {0x30, "WarmupsClear",  "",     POLL_TIER_LOW, 22, 
     PID_FLAG_DIAGNOSTIC, 
     1, 1.0f, 0.0f, 0, 255},
};
#define TIER4_COUNT (sizeof(TIER4_PIDS) / sizeof(TIER4_PIDS[0]))

// Tier intervals (ms)
static const uint32_t TIER_INTERVALS[POLL_TIER_COUNT] = {
    TIER_CRITICAL_INTERVAL_MS,
    TIER_HIGH_INTERVAL_MS,
    TIER_MEDIUM_INTERVAL_MS,
    TIER_LOW_INTERVAL_MS
};

// Tier names
static const char* TIER_NAMES[POLL_TIER_COUNT] = {
    "CRITICAL",
    "HIGH",
    "MEDIUM",
    "LOW"
};

// =============================================================================
// SCHEDULER STATE
// =============================================================================

static scheduler_state_t scheduler = {
    .last_poll_ms = {0},
    .current_index = {0},
    .poll_count = {0},
    .success_count = {0},
    .error_count = {0},
    .enabled = true
};

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

static uint32_t get_time_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

const char* pid_scheduler_tier_name(poll_tier_t tier) {
    if (tier < POLL_TIER_COUNT) {
        return TIER_NAMES[tier];
    }
    return "UNKNOWN";
}

const pid_config_t* pid_scheduler_get_tier_pids(poll_tier_t tier, uint8_t* count) {
    switch (tier) {
        case POLL_TIER_CRITICAL:
            *count = TIER1_COUNT;
            return TIER1_PIDS;
        case POLL_TIER_HIGH:
            *count = TIER2_COUNT;
            return TIER2_PIDS;
        case POLL_TIER_MEDIUM:
            *count = TIER3_COUNT;
            return TIER3_PIDS;
        case POLL_TIER_LOW:
            *count = TIER4_COUNT;
            return TIER4_PIDS;
        default:
            *count = 0;
            return NULL;
    }
}

float pid_convert_value(const pid_config_t* config, const uint8_t* data, size_t len) {
    if (!config || !data || len == 0) {
        return 0.0f;
    }
    
    uint32_t raw = 0;
    
    // Combine bytes (big-endian as per OBD-II standard)
    for (size_t i = 0; i < len && i < config->data_bytes; i++) {
        raw = (raw << 8) | data[i];
    }
    
    // Apply scale and offset
    float value = (float)raw * config->scale + config->offset;
    
    // Special cases for specific PIDs
    switch (config->pid) {
        case 0x0C:  // RPM = ((A * 256) + B) / 4
            if (len >= 2) {
                value = ((data[0] * 256.0f) + data[1]) / 4.0f;
            }
            break;
            
        case 0x42:  // Voltage = ((A * 256) + B) / 1000
            if (len >= 2) {
                value = ((data[0] * 256.0f) + data[1]) / 1000.0f;
            }
            break;
            
        case 0x06:  // Fuel trim = (A - 128) * 100 / 128
        case 0x07:
            if (len >= 1) {
                value = ((float)data[0] - 128.0f) * 100.0f / 128.0f;
            }
            break;
            
        case 0x14:  // O2 voltage = A / 200, fuel trim = (B - 128) * 100/128
        case 0x15:
            if (len >= 1) {
                value = data[0] / 200.0f;
            }
            break;
    }
    
    return value;
}

// =============================================================================
// DTC PARSING
// =============================================================================

void pid_parse_dtc(uint8_t high_byte, uint8_t low_byte, dtc_code_t* dtc) {
    if (!dtc) return;
    
    // First 2 bits = system
    uint8_t system_bits = (high_byte >> 6) & 0x03;
    dtc->system = (dtc_system_t)system_bits;
    
    // Next 2 bits = type (0 = SAE, 1 = manufacturer)
    dtc->type = (high_byte >> 4) & 0x01;
    
    // Remaining 12 bits = code number
    uint16_t code_num = ((high_byte & 0x0F) << 8) | low_byte;
    dtc->code = code_num;
    
    // Build string representation
    char system_char;
    switch (dtc->system) {
        case DTC_SYSTEM_POWERTRAIN: system_char = 'P'; break;
        case DTC_SYSTEM_CHASSIS:    system_char = 'C'; break;
        case DTC_SYSTEM_BODY:       system_char = 'B'; break;
        case DTC_SYSTEM_NETWORK:    system_char = 'U'; break;
        default:                    system_char = '?'; break;
    }
    
    snprintf(dtc->code_str, sizeof(dtc->code_str), "%c%04X", system_char, code_num);
}

// =============================================================================
// SCHEDULER FUNCTIONS
// =============================================================================

esp_err_t pid_scheduler_init(void) {
    ESP_LOGI(TAG, "Initializing PID scheduler...");
    
    uint32_t now = get_time_ms();
    
    for (int i = 0; i < POLL_TIER_COUNT; i++) {
        scheduler.last_poll_ms[i] = now;
        scheduler.current_index[i] = 0;
        scheduler.poll_count[i] = 0;
        scheduler.success_count[i] = 0;
        scheduler.error_count[i] = 0;
    }
    
    scheduler.enabled = true;
    
    ESP_LOGI(TAG, "PID scheduler initialized:");
    ESP_LOGI(TAG, "  Tier CRITICAL: %d PIDs @ %lu ms", TIER1_COUNT, TIER_INTERVALS[0]);
    ESP_LOGI(TAG, "  Tier HIGH:     %d PIDs @ %lu ms", TIER2_COUNT, TIER_INTERVALS[1]);
    ESP_LOGI(TAG, "  Tier MEDIUM:   %d PIDs @ %lu ms", TIER3_COUNT, TIER_INTERVALS[2]);
    ESP_LOGI(TAG, "  Tier LOW:      %d PIDs @ %lu ms", TIER4_COUNT, TIER_INTERVALS[3]);
    ESP_LOGI(TAG, "  Total PIDs:    %d", TIER1_COUNT + TIER2_COUNT + TIER3_COUNT + TIER4_COUNT);
    
    return ESP_OK;
}

bool pid_scheduler_get_next(const pid_config_t** pid) {
    if (!scheduler.enabled || !pid) {
        return false;
    }
    
    uint32_t now = get_time_ms();
    
    // Check tiers in priority order (CRITICAL first)
    for (int tier = 0; tier < POLL_TIER_COUNT; tier++) {
        uint32_t elapsed = now - scheduler.last_poll_ms[tier];
        
        if (elapsed >= TIER_INTERVALS[tier]) {
            uint8_t count;
            const pid_config_t* pids = pid_scheduler_get_tier_pids((poll_tier_t)tier, &count);
            
            if (pids && count > 0) {
                uint8_t idx = scheduler.current_index[tier];
                *pid = &pids[idx];
                
                // Advance index for next time
                scheduler.current_index[tier] = (idx + 1) % count;
                
                // Update last poll time only when we've cycled through all PIDs in tier
                if (scheduler.current_index[tier] == 0) {
                    scheduler.last_poll_ms[tier] = now;
                }
                
                scheduler.poll_count[tier]++;
                return true;
            }
        }
    }
    
    return false;
}

void pid_scheduler_report_result(poll_tier_t tier, bool success) {
    if (tier < POLL_TIER_COUNT) {
        if (success) {
            scheduler.success_count[tier]++;
        } else {
            scheduler.error_count[tier]++;
        }
    }
}

const scheduler_state_t* pid_scheduler_get_stats(void) {
    return &scheduler;
}

void pid_scheduler_set_enabled(bool enable) {
    scheduler.enabled = enable;
    ESP_LOGI(TAG, "Scheduler %s", enable ? "enabled" : "disabled");
}

int pid_scheduler_poll_tier(poll_tier_t tier, extended_telemetry_t* telemetry) {
    if (!telemetry) return 0;
    
    uint8_t count;
    const pid_config_t* pids = pid_scheduler_get_tier_pids(tier, &count);
    
    if (!pids || count == 0) return 0;
    
    int success = 0;
    uint8_t response[8];
    size_t response_len;
    
    for (uint8_t i = 0; i < count; i++) {
        response_len = sizeof(response);
        
        esp_err_t ret = obd_can_request(pids[i].pid, response, &response_len);
        
        if (ret == ESP_OK && response_len > 0) {
            float value = pid_convert_value(&pids[i], response, response_len);
            
            // Store value based on PID
            switch (pids[i].pid) {
                // Tier 1
                case 0x0C: telemetry->rpm = value; telemetry->valid_mask |= VALID_RPM; break;
                case 0x0D: telemetry->speed = (int16_t)value; telemetry->valid_mask |= VALID_SPEED; break;
                case 0x11: telemetry->throttle_pos = value; telemetry->valid_mask |= VALID_THROTTLE; break;
                case 0x04: telemetry->engine_load = value; telemetry->valid_mask |= VALID_LOAD; break;
                case 0x10: telemetry->maf_rate = value; telemetry->valid_mask |= VALID_MAF; break;
                
                // Tier 2
                case 0x05: telemetry->coolant_temp = (int16_t)value; telemetry->valid_mask |= VALID_COOLANT; break;
                case 0x0B: telemetry->manifold_pressure = (uint8_t)value; telemetry->valid_mask |= VALID_MANIFOLD; break;
                case 0x2F: telemetry->fuel_level = value; telemetry->valid_mask |= VALID_FUEL_LEVEL; break;
                case 0x0F: telemetry->intake_air_temp = (int16_t)value; telemetry->valid_mask |= VALID_INTAKE_TEMP; break;
                case 0x1F: telemetry->run_time = (uint16_t)value; telemetry->valid_mask |= VALID_RUNTIME; break;
                
                // Tier 3
                case 0x42: telemetry->control_voltage = value; telemetry->valid_mask |= VALID_VOLTAGE; break;
                case 0x5C: telemetry->oil_temp = (int16_t)value; telemetry->valid_mask |= VALID_OIL_TEMP; break;
                case 0x06: telemetry->fuel_trim_short_b1 = value; telemetry->valid_mask |= VALID_FUEL_TRIM_SHORT; break;
                case 0x07: telemetry->fuel_trim_long_b1 = value; telemetry->valid_mask |= VALID_FUEL_TRIM_LONG; break;
                case 0x31: telemetry->distance_since_clear = (uint16_t)value; telemetry->valid_mask |= VALID_DISTANCE; break;
                case 0x46: telemetry->ambient_temp = (int16_t)value; telemetry->valid_mask |= VALID_AMBIENT; break;
                case 0x0E: telemetry->timing_advance = value; telemetry->valid_mask |= VALID_TIMING; break;
                
                // Tier 4
                case 0x01: 
                    telemetry->mil_status = response[0]; 
                    telemetry->dtc_count = response[0] & 0x7F;  // Lower 7 bits = DTC count
                    telemetry->valid_mask |= VALID_MIL; 
                    break;
                case 0x14: telemetry->o2_voltage_b1s1 = value; telemetry->valid_mask |= VALID_O2_B1S1; break;
                case 0x15: telemetry->o2_voltage_b1s2 = value; telemetry->valid_mask |= VALID_O2_B1S2; break;
                case 0x21: telemetry->distance_with_mil = (uint16_t)value; telemetry->valid_mask |= VALID_DIST_MIL; break;
                case 0x30: telemetry->warmups_since_clear = (uint8_t)value; telemetry->valid_mask |= VALID_WARMUPS; break;
            }
            
            success++;
            pid_scheduler_report_result(tier, true);
        } else {
            pid_scheduler_report_result(tier, false);
        }
        
        // Small delay between PIDs to not overwhelm CAN bus
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    return success;
}

esp_err_t pid_scheduler_read_dtcs(dtc_data_t* dtc_data, bool confirmed) {
    if (!dtc_data) return ESP_ERR_INVALID_ARG;
    
    memset(dtc_data, 0, sizeof(dtc_data_t));
    
    uint8_t mode = confirmed ? OBD_MODE_READ_DTC : OBD_MODE_PENDING_DTC;
    
    // Send DTC request
    twai_message_t tx_msg = {0};
    tx_msg.identifier = 0x7DF;
    tx_msg.data_length_code = 8;
    tx_msg.data[0] = 0x01;  // 1 byte follows
    tx_msg.data[1] = mode;
    tx_msg.data[2] = 0x55;
    tx_msg.data[3] = 0x55;
    tx_msg.data[4] = 0x55;
    tx_msg.data[5] = 0x55;
    tx_msg.data[6] = 0x55;
    tx_msg.data[7] = 0x55;
    
    esp_err_t ret = twai_transmit(&tx_msg, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send DTC request");
        return ret;
    }
    
    // Wait for response
    twai_message_t rx_msg;
    ret = twai_receive(&rx_msg, pdMS_TO_TICKS(500));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No DTC response received");
        return ret;
    }
    
    // Parse response
    // Format: [num_bytes] [mode+0x40] [dtc1_high] [dtc1_low] [dtc2_high] [dtc2_low] ...
    if (rx_msg.data[1] != (mode + 0x40)) {
        ESP_LOGW(TAG, "Invalid DTC response mode: 0x%02X", rx_msg.data[1]);
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    uint8_t num_bytes = rx_msg.data[0];
    int num_dtcs = (num_bytes - 1) / 2;  // -1 for mode byte, /2 for 2 bytes per DTC
    
    if (num_dtcs > MAX_DTCS) {
        num_dtcs = MAX_DTCS;
    }
    
    for (int i = 0; i < num_dtcs && (2 + i*2 + 1) < rx_msg.data_length_code; i++) {
        uint8_t high = rx_msg.data[2 + i*2];
        uint8_t low = rx_msg.data[2 + i*2 + 1];
        
        if (high == 0 && low == 0) {
            break;  // No more DTCs
        }
        
        pid_parse_dtc(high, low, &dtc_data->codes[dtc_data->count]);
        dtc_data->count++;
        
        ESP_LOGI(TAG, "DTC found: %s", dtc_data->codes[dtc_data->count - 1].code_str);
    }
    
    dtc_data->last_read_us = esp_timer_get_time();
    
    ESP_LOGI(TAG, "%s DTCs: %d found", confirmed ? "Confirmed" : "Pending", dtc_data->count);
    
    return ESP_OK;
}

esp_err_t pid_scheduler_clear_dtcs(void) {
    ESP_LOGW(TAG, "Clearing all DTCs and resetting monitors...");
    
    twai_message_t tx_msg = {0};
    tx_msg.identifier = 0x7DF;
    tx_msg.data_length_code = 8;
    tx_msg.data[0] = 0x01;
    tx_msg.data[1] = OBD_MODE_CLEAR_DTC;
    tx_msg.data[2] = 0x55;
    tx_msg.data[3] = 0x55;
    tx_msg.data[4] = 0x55;
    tx_msg.data[5] = 0x55;
    tx_msg.data[6] = 0x55;
    tx_msg.data[7] = 0x55;
    
    esp_err_t ret = twai_transmit(&tx_msg, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send clear DTC command");
        return ret;
    }
    
    // Wait for acknowledgment
    twai_message_t rx_msg;
    ret = twai_receive(&rx_msg, pdMS_TO_TICKS(500));
    if (ret == ESP_OK && rx_msg.data[1] == 0x44) {
        ESP_LOGI(TAG, "DTCs cleared successfully");
        return ESP_OK;
    }
    
    ESP_LOGW(TAG, "Clear DTCs - no acknowledgment received");
    return ESP_ERR_TIMEOUT;
}

// =============================================================================
// STATISTICS LOGGING
// =============================================================================

void pid_scheduler_log_stats(void) {
    ESP_LOGI(TAG, "═══════════════════════════════════════════");
    ESP_LOGI(TAG, "           PID Scheduler Statistics        ");
    ESP_LOGI(TAG, "═══════════════════════════════════════════");
    
    uint32_t total_polls = 0;
    uint32_t total_success = 0;
    uint32_t total_errors = 0;
    
    for (int tier = 0; tier < POLL_TIER_COUNT; tier++) {
        uint8_t count;
        pid_scheduler_get_tier_pids((poll_tier_t)tier, &count);
        
        float success_rate = 0;
        if (scheduler.poll_count[tier] > 0) {
            success_rate = 100.0f * scheduler.success_count[tier] / scheduler.poll_count[tier];
        }
        
        ESP_LOGI(TAG, "Tier %s (%d PIDs @ %lu ms):", 
                 TIER_NAMES[tier], count, TIER_INTERVALS[tier]);
        ESP_LOGI(TAG, "  Polls: %lu | Success: %lu | Errors: %lu (%.1f%%)",
                 scheduler.poll_count[tier], 
                 scheduler.success_count[tier],
                 scheduler.error_count[tier],
                 success_rate);
        
        total_polls += scheduler.poll_count[tier];
        total_success += scheduler.success_count[tier];
        total_errors += scheduler.error_count[tier];
    }
    
    float overall_rate = total_polls > 0 ? 100.0f * total_success / total_polls : 0;
    
    ESP_LOGI(TAG, "───────────────────────────────────────────");
    ESP_LOGI(TAG, "TOTAL: %lu polls | %lu success | %lu errors (%.1f%%)",
             total_polls, total_success, total_errors, overall_rate);
    ESP_LOGI(TAG, "═══════════════════════════════════════════");
}
