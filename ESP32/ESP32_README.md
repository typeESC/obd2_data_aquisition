# Smart ESP32 OBD-II Logger v2.0

A comprehensive ESP32-based OBD-II data logger with CAN bus sniffing capabilities, featuring automatic ignition detection, hybrid mode operation, web interface, and SPIFFS storage.

## Features

- **Automatic Ignition Detection**: Auto-start/stop logging based on RPM and voltage
- **Dual-Mode Operation**: 
  - OBD-II mode for standard diagnostics
  - CAN Sniffer mode for reverse engineering
  - Hybrid mode (OBD + Sniffing simultaneously)
- **Web Interface**: Real-time monitoring and control via HTTP server
- **Local Storage**: SPIFFS-based CSV logging (SavvyCAN-compatible GVRET format)
- **LED Status Indicators**: Visual feedback without display
- **WiFi Connectivity**: Web dashboard and future cloud integration
- **Offline Operation**: Works without internet connection

## Hardware Requirements

- **ESP32 DevKit V1** (ESP32-D0WD-V3)
- **CAN Transceiver**: MCP2515 or SN65HVD230
- **OBD-II Connector**: 16-pin male connector
- **Power**: 12V from OBD port → 3.3V/5V for ESP32
- **Optional**: External antenna for better WiFi range

## Pin Configuration

```
ESP32 GPIO     Function              OBD-II Pin
-------------------------------------------------
GPIO 25        CAN TX                Via Transceiver
GPIO 27        CAN RX                Via Transceiver
GPIO 2         Status LED            Internal LED
GND            Ground                Pin 4, 5 (Ground)
VIN            12V Power             Pin 16 (12V Battery)

CAN Bus:
  Pin 6  → CANH (CAN High)
  Pin 14 → CANL (CAN Low)
```

## Configuration

### 1. WiFi Configuration
Edit `include/obd_config.h`:
```c
#define WIFI_SSID           "YOUR_WIFI_SSID"
#define WIFI_PASSWORD       "YOUR_WIFI_PASSWORD"
```

### 2. CAN Bus Configuration (Already configured)
```c
#define CAN_RX_PIN          GPIO_NUM_27
#define CAN_TX_PIN          GPIO_NUM_25
#define CAN_BITRATE         500000  // 500 kbps (standard OBD)
```

### 3. Ignition Detection Thresholds
```c
#define IGNITION_RPM_THRESHOLD      300     // RPM > 300 = ON
#define IGNITION_VOLTAGE_THRESHOLD  12.5f   // Volts > 12.5 = ON
#define IGNITION_OFF_TIMEOUT        5000    // 5s timeout
```

### 4. Storage Configuration
- **SPIFFS Partition**: ~2MB for session logs
- **Auto-cleanup**: When storage reaches 80%
- **File format**: 
  - OBD logs: `session_YYYY-MM-DD_HH-MM.csv`
  - CAN logs: `canlog_YYYYMMDD_HHMMSS.csv` (GVRET format)

## Build Instructions

### Prerequisites
- ESP-IDF v4.4 or later
- FastAPI backend running (see backend documentation)
**PlatformIO** or **ESP-IDF v5.5**
- **Python 3.7+** (for analysis script)
- **SavvyCAN** (optional, for CAN log analysis)

### Using PlatformIO (Recommended)
```bash
# Build
platformio run

# Upload
platformio run --target upload

# Monitor serial output
platformio run --target monitor

# Upload + Monitor
platformio run --target upload --target monitor
```

### Using ESP-IDF
```bash
# Set up environment
. $HOME/esp/esp-idf/export.sh
src/obd_can.c`, `include/obd_can.h`)
   - TWAI driver for CAN communication
   - Multi-mode operation (OBD/Sniff/Hybrid)
   - Thread-safe with mutex protection
   - 500 kbps bitrate

2. **OBD Parser** (`src/obd_parser.c`, `include/obd_parser.h`)
   - Parses 17+ OBD PIDs
   - Data validation and unit conversion
   - Telemetry structure management

3. **CAN Sniffer** (`src/can_sniffer.c`, `include/can_sniffer.h`)
   - Circular buffer (200 messages)
   - GVRET CSV format (SavvyCAN compatible)
   - Configurable filters (ID range, exclude OBD)
   - Statistics tracking

4. **Storage Manager** (`src/storage_manager.c`, `include/storage_manager.h`)
   - SPIFFS abstraction layer
   - Auto-cleanup when storage low
   - Prepared for SD card expansion

5. **WiFi Manager** (`src/wifi_manager.c`, `include/wifi_manager.h`)
   - Auto-connect with retry logic
   - mDNS support (obd2logger.local)
   - Connection monitoring

6. **Web Server** (`src/web_server.c`, `include/web_server.h`)
   - Real-time dashboard
   - REST API for sniffer control
   - Session file download
   - Status updates via SSE (future)

7. **Data Logger** (`src/data_logger.c`, `include/data_logger.h`)
   - CSV session management
   - Auto-start/stop based on ignition
   - Timestamp synchronization via NTP

8. **Main Application** (`src/main.c`)
   - State machine with 8 states
   Features in Detail

### 1. Automatic Ignition Detection
Sistema detecta automaticamente quando o carro liga/desliga através de:
- **RPM > 300**: Motor rodando
- **Voltagem > 12.5V**: Bateria carregando (alternador)
- **Timeout de 5s**: Sem sinais = carro desligou

### 2. LED Status Patterns

| Pattern | Meaning | Duration |
|---------|---------|----------|
| Slow blink (2s ON, 0.5s OFF) | Ignition OFF | Continuous |
| Quick pulse (50ms ON, 1950ms OFF) | Ignition ON | Continuous |
| Medium blink (500ms/500ms) | OBD Logging | Continuous |
| Fast blink (100ms/100ms) | Sniffing Active | Continuous |
| 3 quick + pause | Storage Low Warning | Repeating |
| 2 blinks + pause | Hybrid Mode | Repeating |

### 3. Web Dashboard

Access at: `http://obd2logger.local/` or `http://<ESP32_IP>/`

**Features:**
- Real-time telemetry display
- System state indicator
- Session statistics
- File download links
- Sniffer controls (Start/Stop/Status)

**API Endpoints:**
```
GET  /                      → Dashboard HTML
GET  /api/telemetry         → Current telemetry JSON
GET  /api/sessions          → List of session files
GET  /download/<filename>   → Download CSV file
POST /api/sniff/start       → Start CAN sniffer
POST /api/sniff/stop        → Stop CAN sniffer
GET  /api/sniff/status      → Sniffer status JSON
```

### 4. CAN Sniffer Features

- **GVRET Format**: Compatible with SavvyCAN
- **Configurable Filters**: ID range and OBD exclusion
- **Circular Buffer**: 200 messages (never loses data)
- **Statistics**: Messages captured, unique IDs, overflows
- **Storage Protection**: Stops at 80% capacity

**Example GVRET Output:**
```csv
Time Stamp,ID,Extended,Dir,Bus,LEN,D1,D2,D3,D4,D5,D6,D7,D8
152463421,0x123,false,Rx,0,8,01,02,03,04,05,06,07,08
```

### 5. Python Analysis Tool

`tools/can_analyzer.py` - Reverse engineering helper

```bash
python tools/can_analyzer.py canlog_20251219_165447.csv --rpm-range 800 3500
```

**Features:**
- Find RPM candidates (8-bit and 16-bit)
- Find speed candidates
- Frequency analysis
- Correlation detection

## Monitored OBD Parameters

| PID | Parameter | Unit | Range |
|-----|-----------|------|-------|
| 0x0C | Engine RPM | RPM | 0-8000 |
| 0x0D | Vehicle Speed | km/h | 0-255 |
| 0x05 | Coolant Temp | °C | -40-215 |
| 0x04 | Engine Load | % | 0-100 |
| 0x11 | Throttle Position | % | 0-100 |
| 0x10 | MAF Rate | g/s | 0-655 | → Car off, standby mode
STATE_IGNITION_ON    → Car on, ready to log
STATE_LOGGING        → Actively logging OBD data
STATE_ERROR          → Error state, attempting recovery
STATE_SNIFF_ACTIVE   → CAN sniffing active
STATE_SNIFF_STORAGE_LOW → Sniffing with low storage warning
STATE_HYBRID_MODE    → OBD + Sniffing simultaneously
```

### Data Flow

#### OBD Mode
```
Vehicle ECU → CAN Bus → TWAI → OBD Parser → CSV File (SPIFFS)
                                    ↓
                              Web Dashboard
```

#### Sniffer Mode
```
CAN Bus → TWAI (Promiscuous) → Circular Buffer → GVRET CSV → SPIFFS
                                                       ↓
                                                  SavvyCAN
```

#### Hybrid Mode
```
CAN Bus → ┬→ OBD Task (Filters 0x7E8) → OBD CSV
          └→ Sniffer Task (All IDs)    → CAN CSV

- **OBD Task**: High-priority data acquisition
- **API Task**: Network communication and health checks
- **Main Loop**: System monitoring and statistics

### Data Flow

```
OBD-II Vehicle → CAN Bus → ESP32 → Data Parser → Batch Buffer → API Client → FastAPI Backend
                                                       ↓
                                               Fallback Mode (if network fails)
```

## Monitored Parameters

### Engine Data
- RPM (Engine Speed)
- Vehicle Speed
- Engine Load
- Throttle Position
- Mass Airflow Rate

### Temperature Sensors
- Coolant Temperature
- Intake Air Temperature
- Oil Temperature

### Fuel System
- Fuel Level
- Fuel Consumption Rate
- Ethanol Percentage

### Electrical System
- Control Module Voltage
- Commanded Equivalence Ratio

### Diagnostic Data
- Engine Runtime
- Distance Since Codes Cleared
- Timing Advance

## Performance Characteristics

- **Data Acquisition Rate**: ~100 samples/second
- **Network Latency**: <200ms per batch
- **Memory Usage**: ~180KB RAM, ~1MB Flash
- **Battery Impact**: ~150mA @ 12V (optimized)

## Error Handling

### Network Failures
- Automatic fallback mode activation
- Retry logic with exponential backoff
- Local buffering capability

### CAN Bus Errors
- Automatic interface reset
- Error counter monitoring
- Bus error recovery

### Memory Management
- Heap monitoring
- Stack overflow detection
- Automatic garbage collection

## Debugging

### Serial Monitor
```bash
idf.py monitor
```

### Log Levels
- Set log level in `obd_config.h`:
```c
#define LOG_LEVEL           ESP_LOG_INFO
#define ENABLE_DEBUG_LOGS   true
```

### Performance Monitoring
- Real-time statistics every 60 seconds
- Memory usage tracking
- Task stack monitoring
- Network status reporting

## API Integration

### Batch Endpoint
```http
POST /api/v1/telemetry/batch
Content-Type: application/json

{
  "session_id": "550e8400-e29b-41d4-a716-446655440001",
  "vehicle_id": "550e8400-e29b-41d4-a716-446655440000",
  "batch_size": 10,
  "first_timestamp": 1704067200000,
  "telemetry_data": [...]
}
```

### Health Check
```http
GET /api/v1/health
```

## Troubleshooting

### Common Issues

1. **WiFi Connection Fails**
   - Verify SSID/password in `obd_config.h`
   - Check signal strength
   - Review router security settings

2. **CAN Bus Communication Issues**
   - Verify wiring connections
   - Check CAN transceiver power
   - Ensure proper termination resistance

3. **API Connection Errors**
   - Verify API server IP address
   - Check network firewall settings
   - Confirm FastAPI backend is running

4. **Memory Issues**
   - Reduce batch size
   - Increase heap size in menuconfig
   - Check for memory leaks

### Recovery Procedures

1. **Factory Reset**
   ```bash
   idf.py erase-flash
   idf.py flash
   ```

2. **Network Reset**
   - Power cycle the ESP32
   - Reset WiFi router
   - Check DHCP lease

3. **CAN Bus Reset**
   - Disconnect from OBD-II port
   - Wait 10 seconds
   - Reconnect and restart ESP32

## License

This project is licensed under the MIT License. See LICENSE file for details.

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

## Support

For technical support or questions:
- Create an issue on GitHub
- Review the troubleshooting section
- Check the ESP-IDF documentation
- Consult the FastAPI backend documentation
