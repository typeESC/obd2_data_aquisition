/**
 * @file data_logger.c
 * @brief Implementação do sistema de logging
 */

#include "data_logger.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include <stdio.h>
#include <sys/stat.h>

static const char *TAG = "DATA_LOGGER";

static FILE *csv_file = NULL;
static bool logging_active = false;
static uint32_t records_saved = 0;

esp_err_t logger_init(void) {
    ESP_LOGI(TAG, "Initializing SPIFFS...");
    
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPIFFS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS: %d KB total, %d KB used", total/1024, used/1024);
    }
    
    return ESP_OK;
}

esp_err_t logger_start(void) {
    if (logging_active) {
        ESP_LOGW(TAG, "Logging already active");
        return ESP_OK;
    }
    
    csv_file = fopen(LOG_FILE_PATH, "a");
    if (!csv_file) {
        ESP_LOGE(TAG, "Failed to open log file");
        return ESP_FAIL;
    }
    
    // Se arquivo vazio, escreve header
    fseek(csv_file, 0, SEEK_END);
    if (ftell(csv_file) == 0) {
        fprintf(csv_file, "timestamp,rpm,speed,coolant,load,throttle,maf,timing_advance,intake_temp,fuel_level\n");
        fflush(csv_file);
    }
    
    logging_active = true;
    ESP_LOGI(TAG, "Logging started");
    
    return ESP_OK;
}

esp_err_t logger_stop(void) {
    if (!logging_active) {
        return ESP_OK;
    }
    
    if (csv_file) {
        fflush(csv_file);
        fclose(csv_file);
        csv_file = NULL;
    }
    
    logging_active = false;
    ESP_LOGI(TAG, "Logging stopped - %lu records saved", records_saved);
    
    return ESP_OK;
}

esp_err_t logger_save_record(const telemetry_data_t *data) {
    if (!logging_active || !csv_file || !data) {
        return ESP_FAIL;
    }
    
    // Salva CSV com todos os campos
    fprintf(csv_file, "%llu,%.0f,%d,%d,%.1f,%.1f,%.2f,%.1f,%d,%.1f\n",
            data->device_timestamp,
            data->rpm,
            data->speed,
            data->coolant_temp,
            data->engine_load,
            data->throttle_pos,
            data->maf_rate,
            data->timing_advance,
            data->intake_air_temp,
            data->fuel_level);
    
    records_saved++;
    
    // Flush a cada 10 registros
    if (records_saved % 10 == 0) {
        fflush(csv_file);
    }
    
    return ESP_OK;
}

esp_err_t logger_delete_file(void) {
    logger_stop();
    
    if (remove(LOG_FILE_PATH) == 0) {
        records_saved = 0;
        ESP_LOGI(TAG, "Log file deleted");
        return ESP_OK;
    }
    
    ESP_LOGW(TAG, "Failed to delete log file");
    return ESP_FAIL;
}

esp_err_t logger_get_stats(logger_stats_t *stats) {
    if (!stats) return ESP_ERR_INVALID_ARG;
    
    stats->records_saved = records_saved;
    stats->is_logging = logging_active;
    
    // Obtém tamanho do arquivo
    struct stat st;
    if (stat(LOG_FILE_PATH, &st) == 0) {
        stats->file_size_bytes = st.st_size;
    } else {
        stats->file_size_bytes = 0;
    }
    
    return ESP_OK;
}

bool logger_is_active(void) {
    return logging_active;
}

const char* logger_get_file_path(void) {
    return LOG_FILE_PATH;
}