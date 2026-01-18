# Smart ESP32 OBD-II Logger v3.0

A comprehensive ESP32-S3-based OBD-II data logger with CAN bus sniffing capabilities, featuring automatic ignition detection, hybrid mode operation, web interface, OLED display, IMU sensor, and SD card storage.

## Features

- **Automatic Ignition Detection**: Auto-start/stop logging based on RPM and voltage
- **Dual-Mode Operation**: 
  - OBD-II mode for standard diagnostics
  - CAN Sniffer mode for reverse engineering
  - Hybrid mode (OBD + Sniffing simultaneously)
- **Web Interface**: Real-time monitoring and control via HTTP server
- **OLED Display**: Real-time OBD data, IMU status, system info (128x32 SSD1306)
- **IMU Sensor**: Impact detection, tilt monitoring, movement tracking (MPU-6050)
- **Local Storage**: SD Card (primary) with SPIFFS fallback
- **4G LTE Ready**: SIM7670G modem for remote data upload
- **GPS Ready**: Integrated GPS via SIM7670G
- **LED Status Indicators**: Visual feedback
- **WiFi Connectivity**: Web dashboard and future cloud integration
- **Offline Operation**: Works without internet connection

## Hardware Requirements

### Main Board
- **LilyGO T-SIM7670G S3 V1.1**
  - MCU: ESP32-S3-WROOM-1 (16MB Flash, 8MB PSRAM OPI)
  - Modem: SIM7670G 4G LTE + GPS
  - Battery: 18650 holder with charger
  - Solar: 5-6V input support

### External Modules
- **CAN Transceiver**: SN65HVD230 (3.3V native - no level shifter!)
- **SD Card Module**: Standard SPI module
- **OLED Display**: SSD1306 0.91" 128x32 (I2C)
- **IMU Sensor**: MPU-6050 (I2C)
- **OBD-II Connector**: 16-pin male connector
- **Power**: 12V from OBD port → 3.3V/5V regulator

## Pin Configuration

```
╔════════════════════════════════════════════════════════════════╗
║  LilyGO T-SIM7670G S3 V1.1 - Pin Mapping                       ║
╠════════════════════════════════════════════════════════════════╣
║                                                                ║
║  CAN BUS (SN65HVD230):                                         ║
║    GPIO 6  → CAN TX (to SN65HVD230 TXD)                        ║
║    GPIO 7  → CAN RX (from SN65HVD230 RXD)                      ║
║                                                                ║
║  I2C BUS (OLED + MPU6050):                                     ║
║    GPIO 15 → SDA (shared bus)                                  ║
║    GPIO 16 → SCL (shared bus)                                  ║
║    OLED Address: 0x3C                                          ║
║    MPU6050 Address: 0x68 (AD0 = GND)                           ║
║                                                                ║
║  SD CARD (SPI - Board Reserved Pins):                          ║
║    GPIO 13 → CS (Chip Select)                                  ║
║    GPIO 14 → MOSI (Data In)                                    ║
║    GPIO 21 → CLK (Clock)                                       ║
║    GPIO 47 → MISO (Data Out)                                   ║
║                                                                ║
║  STATUS LED:                                                   ║
║    GPIO 12 → Board LED                                         ║
║                                                                ║
║  MODEM (Reserved - DO NOT USE):                                ║
║    GPIO 3, 4, 5, 9, 10, 11, 12, 17, 18                         ║
║                                                                ║
║  OBD-II CONNECTOR:                                             ║
║    Pin 4, 5  → Ground (GND)                                    ║
║    Pin 6     → CAN High (CANH)                                 ║
║    Pin 14    → CAN Low (CANL)                                  ║
║    Pin 16    → 12V Battery Power                               ║
║                                                                ║
╚════════════════════════════════════════════════════════════════╝
```

## Wiring Diagram

```
                    LilyGO T-SIM7670G S3 V1.1
                    ┌─────────────────────────┐
                    │                         │
    ┌───────────────┼── GPIO 6  (CAN TX)      │
    │               │                         │
    │  ┌────────────┼── GPIO 7  (CAN RX)      │
    │  │            │                         │
    │  │    ┌───────┼── GPIO 15 (I2C SDA)     │
    │  │    │       │                         │
    │  │    │  ┌────┼── GPIO 16 (I2C SCL)     │
    │  │    │  │    │                         │
    │  │    │  │    │   GPIO 13 (SD CS)  ─────┼─────┐
    │  │    │  │    │   GPIO 14 (SD MOSI) ────┼─────┼──┐
    │  │    │  │    │   GPIO 21 (SD CLK)  ────┼─────┼──┼──┐
    │  │    │  │    │   GPIO 47 (SD MISO) ────┼─────┼──┼──┼──┐
    │  │    │  │    │                         │     │  │  │  │
    │  │    │  │    │   3.3V ─────────────────┼─────┼──┼──┼──┼──┐
    │  │    │  │    │   GND  ─────────────────┼─────┼──┼──┼──┼──┼──┐
    │  │    │  │    └─────────────────────────┘     │  │  │  │  │  │
    │  │    │  │                                    │  │  │  │  │  │
    │  │    │  │    ┌─────────────────────────┐     │  │  │  │  │  │
    │  │    │  │    │       SD Card Module    │     │  │  │  │  │  │
    │  │    │  │    │  CS ───────────────────────────┘  │  │  │  │  │
    │  │    │  │    │  MOSI ────────────────────────────┘  │  │  │  │
    │  │    │  │    │  CLK  ───────────────────────────────┘  │  │  │
    │  │    │  │    │  MISO ──────────────────────────────────┘  │  │
    │  │    │  │    │  VCC  ─────────────────────────────────────┘  │
    │  │    │  │    │  GND  ────────────────────────────────────────┘
    │  │    │  │    └─────────────────────────┘
    │  │    │  │
    │  │    │  │    ┌─────────────────────────┐
    │  │    │  │    │     OLED SSD1306        │
    │  │    └──┼────┤  SDA                    │
    │  │       └────┤  SCL                    │
    │  │            │  VCC ───── 3.3V         │
    │  │            │  GND ───── GND          │
    │  │            └─────────────────────────┘
    │  │
    │  │            ┌─────────────────────────┐
    │  │            │      MPU-6050           │
    │  │    ┌───────┤  SDA (same I2C bus)     │
    │  │    │  ┌────┤  SCL (same I2C bus)     │
    │  │    │  │    │  VCC ───── 3.3V         │
    │  │    │  │    │  GND ───── GND          │
    │  │    │  │    │  AD0 ───── GND (0x68)   │
    │  │    │  │    │  INT ───── NC (optional)│
    │  │    │  │    └─────────────────────────┘
    │  │    │  │
    │  │    │  │    ┌─────────────────────────┐
    │  │    │  │    │     SN65HVD230          │
    └──┼────┼──┼────┤  TXD                    │
       └────┼──┼────┤  RXD                    │
            │  │    │  VCC ───── 3.3V         │
            │  │    │  GND ───── GND          │
            │  │    │  CANH ──────────────────┼─── OBD Pin 6
            │  │    │  CANL ──────────────────┼─── OBD Pin 14
            │  │    └─────────────────────────┘
            │  │
            └──┴─── (I2C bus shared between OLED and MPU6050)
```

## Configuration

### 1. WiFi Configuration
Edit `include/obd_config.h`:
```c
#define WIFI_SSID           "YOUR_WIFI_SSID"
#define WIFI_PASSWORD       "YOUR_WIFI_PASSWORD"
```

### 2. CAN Bus Configuration (in board_config.h)
```c
#define CAN_TX_PIN          GPIO_NUM_6
#define CAN_RX_PIN          GPIO_NUM_7
#define CAN_BITRATE         500000  // 500 kbps (standard OBD)
```

### 3. I2C Configuration (in board_config.h)
```c
#define I2C_SDA_PIN         GPIO_NUM_15
#define I2C_SCL_PIN         GPIO_NUM_16
#define I2C_FREQ_HZ         400000  // 400 kHz (Fast Mode)
```

### 4. Ignition Detection Thresholds
```c
#define IGNITION_RPM_THRESHOLD      300     // RPM > 300 = ON
#define IGNITION_VOLTAGE_THRESHOLD  12.5f   // Volts > 12.5 = ON
#define IGNITION_OFF_TIMEOUT        5000    // 5s timeout
```

### 5. Storage Configuration
- **SD Card**: Primary storage (large capacity)
- **SPIFFS Partition**: Fallback (~2MB)
- **Auto-cleanup**: When storage reaches 80%
- **File format**: 
  - OBD logs: `session_YYYY-MM-DD_HH-MM.csv`
  - CAN logs: `canlog_YYYYMMDD_HHMMSS.csv` (GVRET format)

## Build Instructions

### Prerequisites
- **PlatformIO** or **ESP-IDF v5.5**
- **Python 3.7+** (for analysis scripts)
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
