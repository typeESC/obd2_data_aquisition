/**
 * @file oled_display.c
 * @brief OLED SSD1306 Display driver implementation
 * @date 2026
 */

#include "oled_display.h"
#include "board_config.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

#define TAG "OLED"

// I2C Configuration (shared with MPU6050)
#define I2C_MASTER_NUM          I2C_NUM_0
#define I2C_MASTER_TIMEOUT_MS   100

// Display buffer
static uint8_t oled_buffer[OLED_WIDTH * OLED_PAGES];

// Module state
static struct {
    bool initialized;
    oled_page_t current_page;
    oled_obd_data_t obd_data;
    oled_imu_data_t imu_data;
    oled_status_t status;
    SemaphoreHandle_t mutex;
} oled_state = {0};

// Simple 6x8 font (ASCII 32-127)
static const uint8_t font_6x8[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Space
    0x00, 0x00, 0x5F, 0x00, 0x00, 0x00, // !
    0x00, 0x07, 0x00, 0x07, 0x00, 0x00, // "
    0x14, 0x7F, 0x14, 0x7F, 0x14, 0x00, // #
    0x24, 0x2A, 0x7F, 0x2A, 0x12, 0x00, // $
    0x23, 0x13, 0x08, 0x64, 0x62, 0x00, // %
    0x36, 0x49, 0x55, 0x22, 0x50, 0x00, // &
    0x00, 0x05, 0x03, 0x00, 0x00, 0x00, // '
    0x00, 0x1C, 0x22, 0x41, 0x00, 0x00, // (
    0x00, 0x41, 0x22, 0x1C, 0x00, 0x00, // )
    0x14, 0x08, 0x3E, 0x08, 0x14, 0x00, // *
    0x08, 0x08, 0x3E, 0x08, 0x08, 0x00, // +
    0x00, 0x50, 0x30, 0x00, 0x00, 0x00, // ,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x00, // -
    0x00, 0x60, 0x60, 0x00, 0x00, 0x00, // .
    0x20, 0x10, 0x08, 0x04, 0x02, 0x00, // /
    0x3E, 0x51, 0x49, 0x45, 0x3E, 0x00, // 0
    0x00, 0x42, 0x7F, 0x40, 0x00, 0x00, // 1
    0x42, 0x61, 0x51, 0x49, 0x46, 0x00, // 2
    0x21, 0x41, 0x45, 0x4B, 0x31, 0x00, // 3
    0x18, 0x14, 0x12, 0x7F, 0x10, 0x00, // 4
    0x27, 0x45, 0x45, 0x45, 0x39, 0x00, // 5
    0x3C, 0x4A, 0x49, 0x49, 0x30, 0x00, // 6
    0x01, 0x71, 0x09, 0x05, 0x03, 0x00, // 7
    0x36, 0x49, 0x49, 0x49, 0x36, 0x00, // 8
    0x06, 0x49, 0x49, 0x29, 0x1E, 0x00, // 9
    0x00, 0x36, 0x36, 0x00, 0x00, 0x00, // :
    0x00, 0x56, 0x36, 0x00, 0x00, 0x00, // ;
    0x08, 0x14, 0x22, 0x41, 0x00, 0x00, // <
    0x14, 0x14, 0x14, 0x14, 0x14, 0x00, // =
    0x00, 0x41, 0x22, 0x14, 0x08, 0x00, // >
    0x02, 0x01, 0x51, 0x09, 0x06, 0x00, // ?
    0x32, 0x49, 0x79, 0x41, 0x3E, 0x00, // @
    0x7E, 0x11, 0x11, 0x11, 0x7E, 0x00, // A
    0x7F, 0x49, 0x49, 0x49, 0x36, 0x00, // B
    0x3E, 0x41, 0x41, 0x41, 0x22, 0x00, // C
    0x7F, 0x41, 0x41, 0x22, 0x1C, 0x00, // D
    0x7F, 0x49, 0x49, 0x49, 0x41, 0x00, // E
    0x7F, 0x09, 0x09, 0x09, 0x01, 0x00, // F
    0x3E, 0x41, 0x49, 0x49, 0x7A, 0x00, // G
    0x7F, 0x08, 0x08, 0x08, 0x7F, 0x00, // H
    0x00, 0x41, 0x7F, 0x41, 0x00, 0x00, // I
    0x20, 0x40, 0x41, 0x3F, 0x01, 0x00, // J
    0x7F, 0x08, 0x14, 0x22, 0x41, 0x00, // K
    0x7F, 0x40, 0x40, 0x40, 0x40, 0x00, // L
    0x7F, 0x02, 0x0C, 0x02, 0x7F, 0x00, // M
    0x7F, 0x04, 0x08, 0x10, 0x7F, 0x00, // N
    0x3E, 0x41, 0x41, 0x41, 0x3E, 0x00, // O
    0x7F, 0x09, 0x09, 0x09, 0x06, 0x00, // P
    0x3E, 0x41, 0x51, 0x21, 0x5E, 0x00, // Q
    0x7F, 0x09, 0x19, 0x29, 0x46, 0x00, // R
    0x46, 0x49, 0x49, 0x49, 0x31, 0x00, // S
    0x01, 0x01, 0x7F, 0x01, 0x01, 0x00, // T
    0x3F, 0x40, 0x40, 0x40, 0x3F, 0x00, // U
    0x1F, 0x20, 0x40, 0x20, 0x1F, 0x00, // V
    0x3F, 0x40, 0x38, 0x40, 0x3F, 0x00, // W
    0x63, 0x14, 0x08, 0x14, 0x63, 0x00, // X
    0x07, 0x08, 0x70, 0x08, 0x07, 0x00, // Y
    0x61, 0x51, 0x49, 0x45, 0x43, 0x00, // Z
    0x00, 0x7F, 0x41, 0x41, 0x00, 0x00, // [
    0x02, 0x04, 0x08, 0x10, 0x20, 0x00, // backslash
    0x00, 0x41, 0x41, 0x7F, 0x00, 0x00, // ]
    0x04, 0x02, 0x01, 0x02, 0x04, 0x00, // ^
    0x40, 0x40, 0x40, 0x40, 0x40, 0x00, // _
    0x00, 0x01, 0x02, 0x04, 0x00, 0x00, // `
    0x20, 0x54, 0x54, 0x54, 0x78, 0x00, // a
    0x7F, 0x48, 0x44, 0x44, 0x38, 0x00, // b
    0x38, 0x44, 0x44, 0x44, 0x20, 0x00, // c
    0x38, 0x44, 0x44, 0x48, 0x7F, 0x00, // d
    0x38, 0x54, 0x54, 0x54, 0x18, 0x00, // e
    0x08, 0x7E, 0x09, 0x01, 0x02, 0x00, // f
    0x0C, 0x52, 0x52, 0x52, 0x3E, 0x00, // g
    0x7F, 0x08, 0x04, 0x04, 0x78, 0x00, // h
    0x00, 0x44, 0x7D, 0x40, 0x00, 0x00, // i
    0x20, 0x40, 0x44, 0x3D, 0x00, 0x00, // j
    0x7F, 0x10, 0x28, 0x44, 0x00, 0x00, // k
    0x00, 0x41, 0x7F, 0x40, 0x00, 0x00, // l
    0x7C, 0x04, 0x18, 0x04, 0x78, 0x00, // m
    0x7C, 0x08, 0x04, 0x04, 0x78, 0x00, // n
    0x38, 0x44, 0x44, 0x44, 0x38, 0x00, // o
    0x7C, 0x14, 0x14, 0x14, 0x08, 0x00, // p
    0x08, 0x14, 0x14, 0x18, 0x7C, 0x00, // q
    0x7C, 0x08, 0x04, 0x04, 0x08, 0x00, // r
    0x48, 0x54, 0x54, 0x54, 0x20, 0x00, // s
    0x04, 0x3F, 0x44, 0x40, 0x20, 0x00, // t
    0x3C, 0x40, 0x40, 0x20, 0x7C, 0x00, // u
    0x1C, 0x20, 0x40, 0x20, 0x1C, 0x00, // v
    0x3C, 0x40, 0x30, 0x40, 0x3C, 0x00, // w
    0x44, 0x28, 0x10, 0x28, 0x44, 0x00, // x
    0x0C, 0x50, 0x50, 0x50, 0x3C, 0x00, // y
    0x44, 0x64, 0x54, 0x4C, 0x44, 0x00, // z
    0x00, 0x08, 0x36, 0x41, 0x00, 0x00, // {
    0x00, 0x00, 0x7F, 0x00, 0x00, 0x00, // |
    0x00, 0x41, 0x36, 0x08, 0x00, 0x00, // }
    0x10, 0x08, 0x08, 0x10, 0x08, 0x00, // ~
};

// =============================================================================
// INTERNAL FUNCTIONS
// =============================================================================

static esp_err_t oled_write_cmd(uint8_t cmd) {
    uint8_t write_buf[2] = {0x00, cmd};  // Co=0, D/C#=0 (command)
    return i2c_master_write_to_device(I2C_MASTER_NUM, OLED_I2C_ADDR,
                                      write_buf, sizeof(write_buf),
                                      pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
}

static esp_err_t oled_write_data(const uint8_t *data, size_t len) {
    uint8_t *write_buf = malloc(len + 1);
    if (!write_buf) return ESP_ERR_NO_MEM;
    
    write_buf[0] = 0x40;  // Co=0, D/C#=1 (data)
    memcpy(write_buf + 1, data, len);
    
    esp_err_t ret = i2c_master_write_to_device(I2C_MASTER_NUM, OLED_I2C_ADDR,
                                               write_buf, len + 1,
                                               pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    free(write_buf);
    return ret;
}

// =============================================================================
// PUBLIC FUNCTIONS
// =============================================================================

esp_err_t oled_init(void) {
    esp_err_t ret;
    
    if (oled_state.initialized) {
        return ESP_OK;
    }
    
    // Create mutex
    oled_state.mutex = xSemaphoreCreateMutex();
    if (!oled_state.mutex) {
        return ESP_ERR_NO_MEM;
    }
    
    // Note: I2C should already be initialized by mpu6050_init()
    
    // Send initialization sequence for SSD1306 128x32
    const uint8_t init_cmds[] = {
        SSD1306_CMD_DISPLAY_OFF,
        SSD1306_CMD_SET_CLOCK_DIV, 0x80,
        SSD1306_CMD_SET_MULTIPLEX, 0x1F,        // 32 lines
        SSD1306_CMD_SET_DISPLAY_OFFSET, 0x00,
        SSD1306_CMD_SET_START_LINE | 0x00,
        SSD1306_CMD_CHARGE_PUMP, 0x14,           // Enable charge pump
        SSD1306_CMD_SET_MEMORY_MODE, 0x00,       // Horizontal addressing
        SSD1306_CMD_SET_SEG_REMAP | 0x01,        // Segment remap
        SSD1306_CMD_SET_COM_SCAN_DEC,            // COM output scan direction
        SSD1306_CMD_SET_COM_PINS, 0x02,          // COM pins for 128x32
        SSD1306_CMD_SET_CONTRAST, 0x8F,
        SSD1306_CMD_SET_PRECHARGE, 0xF1,
        SSD1306_CMD_SET_VCOM_DETECT, 0x40,
        0xA4,                                     // Resume to RAM content
        SSD1306_CMD_NORMAL_DISPLAY,
        SSD1306_CMD_DISPLAY_ON
    };
    
    for (int i = 0; i < sizeof(init_cmds); i++) {
        ret = oled_write_cmd(init_cmds[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send init cmd 0x%02X", init_cmds[i]);
            return ret;
        }
    }
    
    // Clear buffer and display
    memset(oled_buffer, 0, sizeof(oled_buffer));
    oled_update();
    
    oled_state.initialized = true;
    oled_state.current_page = OLED_PAGE_MAIN;
    
    ESP_LOGI(TAG, "OLED display initialized (128x32)");
    return ESP_OK;
}

esp_err_t oled_deinit(void) {
    if (!oled_state.initialized) {
        return ESP_OK;
    }
    
    oled_set_power(false);
    
    if (oled_state.mutex) {
        vSemaphoreDelete(oled_state.mutex);
        oled_state.mutex = NULL;
    }
    
    oled_state.initialized = false;
    return ESP_OK;
}

bool oled_is_available(void) {
    return oled_state.initialized;
}

void oled_clear(void) {
    memset(oled_buffer, 0, sizeof(oled_buffer));
}

void oled_update(void) {
    if (!oled_state.initialized) return;
    
    // Set column address
    oled_write_cmd(SSD1306_CMD_SET_COLUMN_ADDR);
    oled_write_cmd(0);                  // Start column
    oled_write_cmd(OLED_WIDTH - 1);     // End column
    
    // Set page address
    oled_write_cmd(SSD1306_CMD_SET_PAGE_ADDR);
    oled_write_cmd(0);                  // Start page
    oled_write_cmd(OLED_PAGES - 1);     // End page
    
    // Write buffer
    oled_write_data(oled_buffer, sizeof(oled_buffer));
}

void oled_set_contrast(uint8_t contrast) {
    oled_write_cmd(SSD1306_CMD_SET_CONTRAST);
    oled_write_cmd(contrast);
}

void oled_set_power(bool on) {
    oled_write_cmd(on ? SSD1306_CMD_DISPLAY_ON : SSD1306_CMD_DISPLAY_OFF);
}

void oled_set_invert(bool invert) {
    oled_write_cmd(invert ? SSD1306_CMD_INVERT_DISPLAY : SSD1306_CMD_NORMAL_DISPLAY);
}

void oled_set_page(oled_page_t page) {
    if (page < OLED_PAGE_COUNT) {
        oled_state.current_page = page;
    }
}

void oled_next_page(void) {
    oled_state.current_page = (oled_state.current_page + 1) % OLED_PAGE_USER_COUNT;
}

oled_page_t oled_get_page(void) {
    return oled_state.current_page;
}

void oled_set_pixel(int x, int y, uint8_t color) {
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) return;
    
    int page = y / 8;
    int bit = y % 8;
    int idx = page * OLED_WIDTH + x;
    
    if (color) {
        oled_buffer[idx] |= (1 << bit);
    } else {
        oled_buffer[idx] &= ~(1 << bit);
    }
}

void oled_draw_hline(int x, int y, int width) {
    for (int i = 0; i < width; i++) {
        oled_set_pixel(x + i, y, 1);
    }
}

void oled_draw_vline(int x, int y, int height) {
    for (int i = 0; i < height; i++) {
        oled_set_pixel(x, y + i, 1);
    }
}

void oled_draw_rect(int x, int y, int width, int height, bool filled) {
    if (filled) {
        for (int j = 0; j < height; j++) {
            oled_draw_hline(x, y + j, width);
        }
    } else {
        oled_draw_hline(x, y, width);
        oled_draw_hline(x, y + height - 1, width);
        oled_draw_vline(x, y, height);
        oled_draw_vline(x + width - 1, y, height);
    }
}

void oled_draw_text(int x, int y, const char *text, uint8_t size) {
    if (!text) return;
    
    int cursor_x = x;
    int char_width = 6 * size;
    int char_height = 8 * size;
    
    while (*text && cursor_x < OLED_WIDTH) {
        char c = *text++;
        if (c < 32 || c > 126) c = '?';
        
        const uint8_t *glyph = &font_6x8[(c - 32) * 6];
        
        for (int col = 0; col < 6; col++) {
            uint8_t line = glyph[col];
            for (int row = 0; row < 8; row++) {
                if (line & (1 << row)) {
                    if (size == 1) {
                        oled_set_pixel(cursor_x + col, y + row, 1);
                    } else {
                        // Scale up for size > 1
                        for (int sx = 0; sx < size; sx++) {
                            for (int sy = 0; sy < size; sy++) {
                                oled_set_pixel(cursor_x + col * size + sx,
                                              y + row * size + sy, 1);
                            }
                        }
                    }
                }
            }
        }
        cursor_x += char_width;
    }
}

void oled_draw_progress_bar(int x, int y, int width, int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    
    // Draw border
    oled_draw_rect(x, y, width, 8, false);
    
    // Draw fill
    int fill_width = (width - 2) * percent / 100;
    if (fill_width > 0) {
        oled_draw_rect(x + 1, y + 1, fill_width, 6, true);
    }
}

void oled_update_obd_data(const oled_obd_data_t *data) {
    if (data) {
        memcpy(&oled_state.obd_data, data, sizeof(oled_obd_data_t));
    }
}

void oled_update_imu_data(const oled_imu_data_t *data) {
    if (data) {
        memcpy(&oled_state.imu_data, data, sizeof(oled_imu_data_t));
    }
}

void oled_update_status(const oled_status_t *status) {
    if (status) {
        memcpy(&oled_state.status, status, sizeof(oled_status_t));
    }
}

void oled_show_startup(void) {
    oled_clear();
    oled_draw_text(20, 4, "OBD2 Logger", 1);
    oled_draw_text(35, 16, "v3.0", 1);
    oled_draw_text(10, 24, "T-SIM7670G S3", 1);
    oled_update();
}

void oled_show_error(const char *error_msg) {
    oled_clear();
    oled_draw_text(0, 0, "ERROR:", 1);
    if (error_msg) {
        oled_draw_text(0, 12, error_msg, 1);
    }
    oled_update();
}

void oled_render(void) {
    if (!oled_state.initialized) return;
    
    oled_clear();
    
    char buf[32];
    
    switch (oled_state.current_page) {
        case OLED_PAGE_MAIN:
            // RPM (large)
            snprintf(buf, sizeof(buf), "%.0f", oled_state.obd_data.rpm);
            oled_draw_text(0, 0, buf, 2);
            oled_draw_text(60, 8, "RPM", 1);
            
            // Speed (large)
            snprintf(buf, sizeof(buf), "%d", oled_state.obd_data.speed);
            oled_draw_text(80, 0, buf, 2);
            oled_draw_text(110, 8, "km/h", 1);
            
            // Status bar
            oled_draw_hline(0, 20, OLED_WIDTH);
            snprintf(buf, sizeof(buf), "%dC  %.1fV  %s",
                     oled_state.obd_data.coolant_temp,
                     oled_state.obd_data.voltage,
                     oled_state.status.logging_active ? "REC" : "---");
            oled_draw_text(0, 24, buf, 1);
            break;
            
        case OLED_PAGE_ENGINE:
            oled_draw_text(0, 0, "ENGINE DATA", 1);
            oled_draw_hline(0, 9, OLED_WIDTH);
            
            snprintf(buf, sizeof(buf), "Temp:%dC Load:%.0f%%",
                     oled_state.obd_data.coolant_temp,
                     oled_state.obd_data.engine_load);
            oled_draw_text(0, 12, buf, 1);
            
            snprintf(buf, sizeof(buf), "Throttle:%.1f%%",
                     oled_state.obd_data.throttle);
            oled_draw_text(0, 22, buf, 1);
            break;
            
        case OLED_PAGE_FUEL:
            oled_draw_text(0, 0, "FUEL DATA", 1);
            oled_draw_hline(0, 9, OLED_WIDTH);
            
            snprintf(buf, sizeof(buf), "Level:%.1f%%",
                     oled_state.obd_data.fuel_level);
            oled_draw_text(0, 12, buf, 1);
            
            snprintf(buf, sizeof(buf), "MAF:%.2f g/s",
                     oled_state.obd_data.maf);
            oled_draw_text(0, 22, buf, 1);
            break;
            
        case OLED_PAGE_IMU:
            oled_draw_text(0, 0, "IMU DATA", 1);
            oled_draw_hline(0, 9, OLED_WIDTH);
            
            snprintf(buf, sizeof(buf), "X:%.2f Y:%.2f",
                     oled_state.imu_data.accel_x,
                     oled_state.imu_data.accel_y);
            oled_draw_text(0, 12, buf, 1);
            
            snprintf(buf, sizeof(buf), "Z:%.2f Tilt:%.1f",
                     oled_state.imu_data.accel_z,
                     oled_state.imu_data.tilt_angle);
            oled_draw_text(0, 22, buf, 1);
            break;
            
        case OLED_PAGE_STATUS:
            oled_draw_text(0, 0, "STATUS", 1);
            oled_draw_hline(0, 9, OLED_WIDTH);
            
            snprintf(buf, sizeof(buf), "WiFi:%s CAN:%s",
                     oled_state.status.wifi_connected ? "OK" : "--",
                     oled_state.status.can_active ? "OK" : "--");
            oled_draw_text(0, 12, buf, 1);
            
            snprintf(buf, sizeof(buf), "SD:%s Log:%s",
                     oled_state.status.sd_card_present ? "OK" : "--",
                     oled_state.status.logging_active ? "REC" : "OFF");
            oled_draw_text(0, 22, buf, 1);
            break;
            
        default:
            oled_draw_text(0, 12, "Unknown Page", 1);
            break;
    }
    
    oled_update();
}
