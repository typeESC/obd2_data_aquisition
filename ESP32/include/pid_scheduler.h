/**
 * @file pid_scheduler.h
 * @brief Priority-based PID polling scheduler for fleet management
 * @date 2025
 * 
 * Implements a multi-tier polling strategy:
 * - TIER 1 (Critical): 50-100ms - Real-time data (RPM, Speed, Throttle, Load, MAF)
 * - TIER 2 (High): 200-500ms - Operational data (Coolant, Manifold, Fuel, Intake, Runtime)
 * - TIER 3 (Medium): 1-5s - Slow data (Voltage, Oil, Fuel Trim, Distance, Ambient)
 * - TIER 4 (Low): 10-30s - Diagnostic data (MIL, DTCs, O2 sensors)
 */

#ifndef PID_SCHEDULER_H
#define PID_SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// TIER DEFINITIONS
// =============================================================================

typedef enum {
    POLL_TIER_CRITICAL = 0,    // 50-100ms (real-time)
    POLL_TIER_HIGH = 1,        // 200-500ms (operational)
    POLL_TIER_MEDIUM = 2,      // 1-5 seconds (slow)
    POLL_TIER_LOW = 3,         // 10-30 seconds (diagnostic)
    POLL_TIER_COUNT
} poll_tier_t;

// Tier timing configuration (in milliseconds)
#define TIER_CRITICAL_INTERVAL_MS   100
#define TIER_HIGH_INTERVAL_MS       500
#define TIER_MEDIUM_INTERVAL_MS     2000
#define TIER_LOW_INTERVAL_MS        15000

// =============================================================================
// PID CONFIGURATION
// =============================================================================

// OBD-II Modes
#define OBD_MODE_CURRENT        0x01    // Current data
#define OBD_MODE_FREEZE_FRAME   0x02    // Freeze frame data
#define OBD_MODE_READ_DTC       0x03    // Read DTCs
#define OBD_MODE_CLEAR_DTC      0x04    // Clear DTCs
#define OBD_MODE_PENDING_DTC    0x07    // Pending DTCs

// PID flags
#define PID_FLAG_FLEET_CRITICAL     (1 << 0)    // Critical for fleet tracking
#define PID_FLAG_PREDICTIVE_MAINT   (1 << 1)    // Used for predictive maintenance
#define PID_FLAG_DRIVER_BEHAVIOR    (1 << 2)    // Driver behavior analysis
#define PID_FLAG_FUEL_EFFICIENCY    (1 << 3)    // Fuel efficiency calculation
#define PID_FLAG_DIAGNOSTIC         (1 << 4)    // Diagnostic data

typedef struct {
    uint8_t pid;                // PID number
    const char* name;           // Human-readable name
    const char* unit;           // Unit string
    poll_tier_t tier;           // Polling tier
    uint8_t priority;           // Priority within tier (1 = highest)
    uint8_t flags;              // Feature flags
    uint8_t data_bytes;         // Expected response bytes (1-4)
    float scale;                // Scaling factor for conversion
    float offset;               // Offset for conversion
    int16_t min_val;            // Minimum valid value
    int16_t max_val;            // Maximum valid value
} pid_config_t;

// =============================================================================
// SCHEDULER STATE
// =============================================================================

typedef struct {
    uint32_t last_poll_ms[POLL_TIER_COUNT];     // Last poll time per tier
    uint8_t current_index[POLL_TIER_COUNT];     // Current PID index per tier
    uint32_t poll_count[POLL_TIER_COUNT];       // Total polls per tier
    uint32_t success_count[POLL_TIER_COUNT];    // Successful polls per tier
    uint32_t error_count[POLL_TIER_COUNT];      // Failed polls per tier
    bool enabled;                                // Scheduler enabled flag
} scheduler_state_t;

// =============================================================================
// EXTENDED TELEMETRY DATA
// =============================================================================

typedef struct {
    uint64_t timestamp_us;          // Timestamp in microseconds
    
    // Tier 1 - Critical (real-time)
    float rpm;                      // Engine RPM (PID 0x0C)
    int16_t speed;                  // Vehicle speed km/h (PID 0x0D)
    float throttle_pos;             // Throttle position % (PID 0x11)
    float engine_load;              // Engine load % (PID 0x04)
    float maf_rate;                 // MAF g/s (PID 0x10)
    
    // Tier 2 - High (operational)
    int16_t coolant_temp;           // Coolant temp °C (PID 0x05)
    uint8_t manifold_pressure;      // Intake manifold kPa (PID 0x0B)
    float fuel_level;               // Fuel level % (PID 0x2F)
    int16_t intake_air_temp;        // Intake air temp °C (PID 0x0F)
    uint16_t run_time;              // Engine runtime seconds (PID 0x1F)
    
    // Tier 3 - Medium (slow)
    float control_voltage;          // Control module V (PID 0x42)
    int16_t oil_temp;               // Oil temp °C (PID 0x5C)
    float fuel_trim_short_b1;       // Short term fuel trim B1 % (PID 0x06)
    float fuel_trim_long_b1;        // Long term fuel trim B1 % (PID 0x07)
    uint16_t distance_since_clear;  // Distance km (PID 0x31)
    int16_t ambient_temp;           // Ambient temp °C (PID 0x46)
    float timing_advance;           // Timing advance ° (PID 0x0E)
    
    // Tier 4 - Low (diagnostic)
    uint8_t mil_status;             // MIL status bitmap (PID 0x01)
    uint8_t dtc_count;              // Number of confirmed DTCs
    uint8_t pending_dtc_count;      // Number of pending DTCs
    float o2_voltage_b1s1;          // O2 sensor B1S1 V (PID 0x14)
    float o2_voltage_b1s2;          // O2 sensor B1S2 V (PID 0x15)
    uint16_t distance_with_mil;     // Distance with MIL km (PID 0x21)
    uint8_t warmups_since_clear;    // Warm-ups count (PID 0x30)
    
    // Validity flags
    uint32_t valid_mask;            // Bitmask of valid fields
} extended_telemetry_t;

// Validity mask bits
#define VALID_RPM               (1 << 0)
#define VALID_SPEED             (1 << 1)
#define VALID_THROTTLE          (1 << 2)
#define VALID_LOAD              (1 << 3)
#define VALID_MAF               (1 << 4)
#define VALID_COOLANT           (1 << 5)
#define VALID_MANIFOLD          (1 << 6)
#define VALID_FUEL_LEVEL        (1 << 7)
#define VALID_INTAKE_TEMP       (1 << 8)
#define VALID_RUNTIME           (1 << 9)
#define VALID_VOLTAGE           (1 << 10)
#define VALID_OIL_TEMP          (1 << 11)
#define VALID_FUEL_TRIM_SHORT   (1 << 12)
#define VALID_FUEL_TRIM_LONG    (1 << 13)
#define VALID_DISTANCE          (1 << 14)
#define VALID_AMBIENT           (1 << 15)
#define VALID_TIMING            (1 << 16)
#define VALID_MIL               (1 << 17)
#define VALID_DTC               (1 << 18)
#define VALID_PENDING_DTC       (1 << 19)
#define VALID_O2_B1S1           (1 << 20)
#define VALID_O2_B1S2           (1 << 21)
#define VALID_DIST_MIL          (1 << 22)
#define VALID_WARMUPS           (1 << 23)

// =============================================================================
// DTC STRUCTURES
// =============================================================================

#define MAX_DTCS                16      // Maximum DTCs to store

typedef enum {
    DTC_SYSTEM_POWERTRAIN = 0,      // P codes
    DTC_SYSTEM_CHASSIS = 1,         // C codes
    DTC_SYSTEM_BODY = 2,            // B codes
    DTC_SYSTEM_NETWORK = 3          // U codes
} dtc_system_t;

typedef struct {
    dtc_system_t system;            // System type (P/C/B/U)
    uint8_t type;                   // 0 = SAE, 1 = Manufacturer
    uint16_t code;                  // Numeric code (0-9999)
    char code_str[6];               // String representation (e.g., "P0301")
} dtc_code_t;

typedef struct {
    dtc_code_t codes[MAX_DTCS];     // Array of DTCs
    uint8_t count;                  // Number of DTCs
    bool mil_on;                    // MIL (Check Engine) light status
    uint64_t last_read_us;          // Last read timestamp
} dtc_data_t;

// =============================================================================
// FUNCTION PROTOTYPES
// =============================================================================

/**
 * @brief Initialize the PID scheduler
 * @return ESP_OK on success
 */
esp_err_t pid_scheduler_init(void);

/**
 * @brief Get the next PID to poll based on timing and priority
 * @param[out] pid Output PID configuration
 * @return true if a PID should be polled, false if nothing due
 */
bool pid_scheduler_get_next(const pid_config_t** pid);

/**
 * @brief Report poll result for statistics
 * @param tier The tier that was polled
 * @param success Whether the poll was successful
 */
void pid_scheduler_report_result(poll_tier_t tier, bool success);

/**
 * @brief Get scheduler statistics
 * @return Pointer to scheduler state
 */
const scheduler_state_t* pid_scheduler_get_stats(void);

/**
 * @brief Enable or disable the scheduler
 * @param enable true to enable, false to disable
 */
void pid_scheduler_set_enabled(bool enable);

/**
 * @brief Poll all PIDs for a specific tier
 * @param tier The tier to poll
 * @param telemetry Output telemetry data
 * @return Number of successful polls
 */
int pid_scheduler_poll_tier(poll_tier_t tier, extended_telemetry_t* telemetry);

/**
 * @brief Read diagnostic trouble codes
 * @param[out] dtc_data Output DTC data structure
 * @param confirmed true for confirmed DTCs (Mode 03), false for pending (Mode 07)
 * @return ESP_OK on success
 */
esp_err_t pid_scheduler_read_dtcs(dtc_data_t* dtc_data, bool confirmed);

/**
 * @brief Clear all DTCs and reset monitors
 * @return ESP_OK on success
 */
esp_err_t pid_scheduler_clear_dtcs(void);

/**
 * @brief Parse DTC bytes into code structure
 * @param high_byte First DTC byte
 * @param low_byte Second DTC byte
 * @param[out] dtc Output DTC structure
 */
void pid_parse_dtc(uint8_t high_byte, uint8_t low_byte, dtc_code_t* dtc);

/**
 * @brief Get human-readable name for a tier
 * @param tier The tier
 * @return String name
 */
const char* pid_scheduler_tier_name(poll_tier_t tier);

/**
 * @brief Get all PIDs for a specific tier
 * @param tier The tier
 * @param[out] count Output count of PIDs
 * @return Array of PID configurations
 */
const pid_config_t* pid_scheduler_get_tier_pids(poll_tier_t tier, uint8_t* count);

/**
 * @brief Convert raw PID bytes to physical value
 * @param config PID configuration
 * @param data Raw data bytes
 * @param len Data length
 * @return Physical value
 */
float pid_convert_value(const pid_config_t* config, const uint8_t* data, size_t len);

/**
 * @brief Log scheduler statistics to console
 */
void pid_scheduler_log_stats(void);

#ifdef __cplusplus
}
#endif

#endif // PID_SCHEDULER_H
