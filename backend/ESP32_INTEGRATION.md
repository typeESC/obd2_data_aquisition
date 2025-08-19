# ESP32 Integration Guide

This guide explains how to modify your ESP32 OBD logger to send data to the FastAPI backend instead of storing it locally.

## Overview

Instead of writing data to SPIFFS as a CSV file, the ESP32 will send telemetry data to the FastAPI backend via HTTP POST requests.

## Required Changes to ESP32 Code

### 1. Include HTTP Client Library

Add to your includes in `main.c`:

```c
#include "esp_http_client.h"
```

### 2. API Configuration

Add these configuration constants:

```c
// API Configuration
#define API_BASE_URL "http://your-api-server:8000/api/v1"
#define SESSION_ID "550e8400-e29b-41d4-a716-446655440001"  // Use actual session ID
#define VEHICLE_ID "550e8400-e29b-41d4-a716-446655440000"  // Use actual vehicle ID
#define BATCH_SIZE 10  // Number of records to batch before sending
```

### 3. Data Structure for JSON

Replace the CSV writing with JSON data preparation:

```c
// Structure to hold batch data
typedef struct {
    TelemetryData data[BATCH_SIZE];
    int count;
} TelemetryBatch;

TelemetryBatch batch = {0};
```

### 4. HTTP Client Functions

Add these functions to handle HTTP communication:

```c
// HTTP event handler
esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(TAG, "HTTP Response: %.*s", evt->data_len, (char*)evt->data);
            break;
        default:
            break;
    }
    return ESP_OK;
}

// Send single telemetry record
esp_err_t send_telemetry_record(TelemetryData *data) {
    char url[256];
    char json_data[1024];
    
    // Prepare URL
    snprintf(url, sizeof(url), "%s/telemetry/records?session_id=%s", 
             API_BASE_URL, SESSION_ID);
    
    // Prepare JSON data
    snprintf(json_data, sizeof(json_data),
        "{"
        "\"device_timestamp\":%lu,"
        "\"rpm\":%.1f,"
        "\"speed\":%d,"
        "\"coolant_temp\":%d,"
        "\"engine_load\":%.1f,"
        "\"timing_advance\":%.1f,"
        "\"intake_air_temp\":%d,"
        "\"maf_rate\":%.2f,"
        "\"throttle_pos\":%.1f,"
        "\"run_time\":%d,"
        "\"dist_since_clear\":%d,"
        "\"fuel_level\":%.1f,"
        "\"module_voltage\":%.2f,"
        "\"commanded_lambda\":%.4f,"
        "\"relative_throttle\":%.1f,"
        "\"ethanol_percentage\":%.1f,"
        "\"oil_temp\":%d,"
        "\"fuel_rate\":%.2f"
        "}",
        (unsigned long)esp_log_timestamp(),
        data->rpm, data->speed, data->coolant_temp, data->engine_load,
        data->timing_advance, data->intake_air_temp, data->maf_rate,
        data->throttle_pos, data->run_time, data->dist_since_clear,
        data->fuel_level, data->module_voltage, data->commanded_lambda,
        data->relative_throttle, data->ethanol_percentage,
        data->oil_temp, data->fuel_rate
    );
    
    // Configure HTTP client
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    // Set headers
    esp_http_client_set_header(client, "Content-Type", "application/json");
    
    // Set POST data
    esp_http_client_set_post_field(client, json_data, strlen(json_data));
    
    // Perform request
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code == 201) {
            ESP_LOGI(TAG, "Telemetry sent successfully");
        } else {
            ESP_LOGE(TAG, "HTTP POST failed with status %d", status_code);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }
    
    esp_http_client_cleanup(client);
    return err;
}

// Send batch of telemetry records
esp_err_t send_telemetry_batch(TelemetryBatch *batch) {
    if (batch->count == 0) return ESP_OK;
    
    char url[256];
    char *json_data = malloc(4096);  // Allocate larger buffer for batch
    
    if (!json_data) {
        ESP_LOGE(TAG, "Failed to allocate memory for JSON data");
        return ESP_ERR_NO_MEM;
    }
    
    // Prepare URL
    snprintf(url, sizeof(url), "%s/telemetry/batch", API_BASE_URL);
    
    // Start JSON object
    strcpy(json_data, "{\"session_id\":\"" SESSION_ID "\",\"data\":[");
    
    // Add each record
    for (int i = 0; i < batch->count; i++) {
        char record[512];
        snprintf(record, sizeof(record),
            "%s{"
            "\"device_timestamp\":%lu,"
            "\"rpm\":%.1f,"
            "\"speed\":%d,"
            "\"coolant_temp\":%d,"
            "\"engine_load\":%.1f,"
            "\"timing_advance\":%.1f,"
            "\"intake_air_temp\":%d,"
            "\"maf_rate\":%.2f,"
            "\"throttle_pos\":%.1f,"
            "\"run_time\":%d,"
            "\"dist_since_clear\":%d,"
            "\"fuel_level\":%.1f,"
            "\"module_voltage\":%.2f,"
            "\"commanded_lambda\":%.4f,"
            "\"relative_throttle\":%.1f,"
            "\"ethanol_percentage\":%.1f,"
            "\"oil_temp\":%d,"
            "\"fuel_rate\":%.2f"
            "}",
            (i > 0) ? "," : "",
            (unsigned long)esp_log_timestamp(),
            batch->data[i].rpm, batch->data[i].speed, batch->data[i].coolant_temp,
            batch->data[i].engine_load, batch->data[i].timing_advance,
            batch->data[i].intake_air_temp, batch->data[i].maf_rate,
            batch->data[i].throttle_pos, batch->data[i].run_time,
            batch->data[i].dist_since_clear, batch->data[i].fuel_level,
            batch->data[i].module_voltage, batch->data[i].commanded_lambda,
            batch->data[i].relative_throttle, batch->data[i].ethanol_percentage,
            batch->data[i].oil_temp, batch->data[i].fuel_rate
        );
        strcat(json_data, record);
    }
    
    // Close JSON object
    strcat(json_data, "]}");
    
    // Configure HTTP client
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .timeout_ms = 15000,  // Longer timeout for batch
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    // Set headers
    esp_http_client_set_header(client, "Content-Type", "application/json");
    
    // Set POST data
    esp_http_client_set_post_field(client, json_data, strlen(json_data));
    
    // Perform request
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code == 201) {
            ESP_LOGI(TAG, "Batch of %d records sent successfully", batch->count);
            batch->count = 0;  // Clear batch
        } else {
            ESP_LOGE(TAG, "HTTP POST failed with status %d", status_code);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }
    
    esp_http_client_cleanup(client);
    free(json_data);
    return err;
}
```

### 5. Modified OBD Logging Task

Replace the file writing section in your `obd_logging_task` function:

```c
void obd_logging_task(void *pvParameters) {
    ESP_LOGI(TAG, "Iniciando tarefa de logging OBD.");
    
    // ... (existing TWAI initialization code) ...
    
    TelemetryData carData = {0};
    uint8_t pids_to_query[] = {0x04, 0x05, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x1F, 0x31, 0x2F, 0x42, 0x44, 0x45, 0x52, 0x5C, 0x5E};
    size_t num_pids = sizeof(pids_to_query) / sizeof(pids_to_query[0]);
    twai_message_t response;

    while (1) {
        // ... (existing OBD query code) ...
        
        if (carData.rpm > 0) {
            // Add to batch instead of writing to file
            batch.data[batch.count] = carData;
            batch.count++;
            
            ESP_LOGI(TAG, "RPM: %.0f, Vel: %d km/h -> Added to batch (%d/%d)", 
                     carData.rpm, carData.speed, batch.count, BATCH_SIZE);
            
            // Send batch when full
            if (batch.count >= BATCH_SIZE) {
                esp_err_t err = send_telemetry_batch(&batch);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to send batch, will retry later");
                    // Could implement retry logic here
                }
            }
        } else {
            ESP_LOGI(TAG, "RPM: %.0f. Motor desligado, dados nao adicionados.", carData.rpm);
        }
        
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}
```

### 6. Fallback and Recovery

Add fallback mechanism for when API is unavailable:

```c
// Fallback to local storage when API is unavailable
esp_err_t fallback_to_local_storage(TelemetryData *data) {
    FILE* f = fopen("/spiffs/fallback.csv", "a");
    if(f != NULL) {
        fprintf(f, "%lu,%.0f,%d,%d,%.1f,%.1f,%d,%.2f,%.1f,%d,%d,%.1f,%.2f,%.4f,%.1f,%.1f,%d,%.2f\n", 
            (unsigned long)esp_log_timestamp(), data->rpm, data->speed, data->coolant_temp, data->engine_load,
            data->timing_advance, data->intake_air_temp, data->maf_rate, data->throttle_pos,
            data->run_time, data->dist_since_clear, data->fuel_level, data->module_voltage,
            data->commanded_lambda, data->relative_throttle, data->ethanol_percentage,
            data->oil_temp, data->fuel_rate);
        fclose(f);
        return ESP_OK;
    }
    return ESP_FAIL;
}
```

## API Endpoints Used

### Send Single Record
- **URL**: `POST /api/v1/telemetry/records?session_id={SESSION_ID}`
- **Content-Type**: `application/json`
- **Body**: Single telemetry record JSON

### Send Batch
- **URL**: `POST /api/v1/telemetry/batch`
- **Content-Type**: `application/json`
- **Body**: Batch of telemetry records JSON

## Configuration

1. **API Server URL**: Update `API_BASE_URL` with your server's IP/domain
2. **Session ID**: Create a session through the API and use its ID
3. **Vehicle ID**: Create a vehicle through the API and use its ID
4. **Batch Size**: Adjust based on memory constraints and network conditions

## Testing

1. Start the FastAPI backend
2. Use the ESP32 simulator to test the integration:
   ```bash
   python esp32_simulator.py
   ```
3. Check the API documentation at `http://your-server:8000/api/v1/docs`

## Error Handling

- Implement retry logic for failed HTTP requests
- Use fallback local storage when API is unavailable
- Monitor HTTP response codes and handle errors appropriately
- Add watchdog timer for HTTP operations

## Performance Considerations

- Use batch sending to reduce HTTP overhead
- Implement connection pooling if possible
- Consider data compression for large batches
- Monitor memory usage with JSON string building
- Add timeout handling for network operations

## Security

- Use HTTPS in production
- Implement API authentication if needed
- Validate server certificates
- Consider data encryption for sensitive information
