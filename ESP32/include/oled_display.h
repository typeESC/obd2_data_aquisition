/**
 * @file oled_display.h
 * @brief OLED SSD1306 Display driver for ESP32-S3
 * @date 2026
 * 
 * Display: 0.91" OLED 128x32 pixels (I2C interface)
 * Controller: SSD1306
 * 
 * Features:
 * - Real-time OBD data display
 * - Status indicators
 * - Multiple display pages
 * - Low power mode support
 */

#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// =============================================================================
// DISPLAY CONFIGURATION
// =============================================================================
#define OLED_WIDTH          128
#define OLED_HEIGHT         32
#define OLED_PAGES          (OLED_HEIGHT / 8)   // 4 pages for 32px height

// SSD1306 Commands
#define SSD1306_CMD_DISPLAY_OFF         0xAE
#define SSD1306_CMD_DISPLAY_ON          0xAF
#define SSD1306_CMD_SET_CONTRAST        0x81
#define SSD1306_CMD_NORMAL_DISPLAY      0xA6
#define SSD1306_CMD_INVERT_DISPLAY      0xA7
#define SSD1306_CMD_SET_MULTIPLEX       0xA8
#define SSD1306_CMD_SET_DISPLAY_OFFSET  0xD3
#define SSD1306_CMD_SET_START_LINE      0x40
#define SSD1306_CMD_SET_SEG_REMAP       0xA1
#define SSD1306_CMD_SET_COM_SCAN_DEC    0xC8
#define SSD1306_CMD_SET_COM_PINS        0xDA
#define SSD1306_CMD_SET_CLOCK_DIV       0xD5
#define SSD1306_CMD_SET_PRECHARGE       0xD9
#define SSD1306_CMD_SET_VCOM_DETECT     0xDB
#define SSD1306_CMD_CHARGE_PUMP         0x8D
#define SSD1306_CMD_SET_MEMORY_MODE     0x20
#define SSD1306_CMD_SET_COLUMN_ADDR     0x21
#define SSD1306_CMD_SET_PAGE_ADDR       0x22

// =============================================================================
// DISPLAY PAGES / SCREENS
// =============================================================================
typedef enum {
    OLED_PAGE_MAIN = 0,     // RPM + Speed (large)
    OLED_PAGE_ENGINE,       // Engine data (temp, load, throttle)
    OLED_PAGE_FUEL,         // Fuel data
    OLED_PAGE_IMU,          // Accelerometer data
    OLED_PAGE_STATUS,       // System status
    OLED_PAGE_USER_COUNT,   // Number of user-accessible pages (5)
    // Special pages (not in normal cycle)
    OLED_PAGE_ERROR = 10,   // Error display (manual only)
    OLED_PAGE_STARTUP = 11  // Startup screen (manual only)
} oled_page_t;

#define OLED_PAGE_COUNT OLED_PAGE_USER_COUNT  // For backwards compatibility

// =============================================================================
// STATUS ICONS (for status bar)
// =============================================================================
typedef struct {
    bool wifi_connected;
    bool can_active;
    bool logging_active;
    bool sd_card_present;
    bool gps_fix;
    bool lte_connected;
    uint8_t battery_percent;
    int8_t signal_strength;     // WiFi/LTE RSSI
    uint32_t session_records;   // Records in current session
} oled_status_t;

// =============================================================================
// OBD DATA FOR DISPLAY
// =============================================================================
typedef struct {
    float rpm;
    int speed;
    int coolant_temp;
    float throttle;
    float engine_load;
    float voltage;
    float fuel_level;
    float maf;                  // Mass Air Flow
    int intake_temp;            // Intake Air Temperature
    bool mil_on;                // Check Engine Light
    int dtc_count;
} oled_obd_data_t;

// =============================================================================
// IMU DATA FOR DISPLAY
// =============================================================================
typedef struct {
    float accel_x;              // Acceleration X (g)
    float accel_y;              // Acceleration Y (g)
    float accel_z;              // Acceleration Z (g)
    float gyro_x;               // Gyroscope X (dps)
    float gyro_y;               // Gyroscope Y (dps)
    float gyro_z;               // Gyroscope Z (dps)
    float tilt_angle;           // Current tilt angle
    bool is_moving;             // Motion detected
    bool impact_detected;       // Impact detected
} oled_imu_data_t;

// =============================================================================
// PUBLIC FUNCTIONS
// =============================================================================

/**
 * @brief Initialize OLED display
 * @return ESP_OK on success
 */
esp_err_t oled_init(void);

/**
 * @brief Deinitialize OLED display
 * @return ESP_OK on success
 */
esp_err_t oled_deinit(void);

/**
 * @brief Check if display is available
 * @return true if display is connected and working
 */
bool oled_is_available(void);

/**
 * @brief Clear entire display
 */
void oled_clear(void);

/**
 * @brief Update display (flush buffer to screen)
 */
void oled_update(void);

/**
 * @brief Set display brightness/contrast
 * @param contrast Contrast value (0-255)
 */
void oled_set_contrast(uint8_t contrast);

/**
 * @brief Turn display on/off
 * @param on true to turn on, false to turn off
 */
void oled_set_power(bool on);

/**
 * @brief Invert display colors
 * @param invert true to invert, false for normal
 */
void oled_set_invert(bool invert);

/**
 * @brief Set current display page
 * @param page Page to display
 */
void oled_set_page(oled_page_t page);

/**
 * @brief Go to next page
 */
void oled_next_page(void);

/**
 * @brief Get current page
 * @return Current page
 */
oled_page_t oled_get_page(void);

/**
 * @brief Update OBD data for display
 * @param data Pointer to OBD data structure
 */
void oled_update_obd_data(const oled_obd_data_t *data);

/**
 * @brief Update IMU data for display
 * @param data Pointer to IMU data structure
 */
void oled_update_imu_data(const oled_imu_data_t *data);

/**
 * @brief Update status indicators
 * @param status Pointer to status structure
 */
void oled_update_status(const oled_status_t *status);

/**
 * @brief Draw text at position
 * @param x X coordinate
 * @param y Y coordinate (0-3 for 32px height)
 * @param text Text to draw
 * @param size Font size (1=6x8, 2=12x16)
 */
void oled_draw_text(int x, int y, const char *text, uint8_t size);

/**
 * @brief Draw large number (for RPM/Speed display)
 * @param x X coordinate
 * @param value Number to display
 * @param digits Number of digits
 */
void oled_draw_large_number(int x, int value, int digits);

/**
 * @brief Show error message
 * @param error_msg Error message to display
 */
void oled_show_error(const char *error_msg);

/**
 * @brief Show startup screen with logo/version
 */
void oled_show_startup(void);

/**
 * @brief Show progress bar
 * @param x X coordinate
 * @param y Y coordinate  
 * @param width Width in pixels
 * @param percent Progress percentage (0-100)
 */
void oled_draw_progress_bar(int x, int y, int width, int percent);

/**
 * @brief Render the current page with latest data
 * Called periodically by display task
 */
void oled_render(void);

/**
 * @brief Set a pixel
 * @param x X coordinate
 * @param y Y coordinate
 * @param color 1=white, 0=black
 */
void oled_set_pixel(int x, int y, uint8_t color);

/**
 * @brief Draw horizontal line
 */
void oled_draw_hline(int x, int y, int width);

/**
 * @brief Draw vertical line
 */
void oled_draw_vline(int x, int y, int height);

/**
 * @brief Draw rectangle
 */
void oled_draw_rect(int x, int y, int width, int height, bool filled);

#endif // OLED_DISPLAY_H
