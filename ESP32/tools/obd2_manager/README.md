# OBD2 Manager

Desktop application for managing ESP32 OBD2 Smart Logger devices.

## Features

- **Real-time Dashboard**: Live gauges and charts for RPM, Speed, Temperature, etc.
- **Log Analyzer**: Analyze OBD sessions and CAN sniffer logs with synchronized overlay
- **PID Configuration**: Configure which PIDs to record and their polling tiers
- **File Manager**: Download, delete, and manage log files on the ESP32
- **DTC Reader**: View and clear diagnostic trouble codes
- **Correlation Engine**: Automatic CAN signal reverse engineering with OBD reference

## Installation

```bash
cd tools/obd2_manager
pip install -e .
```

For DBC file support:
```bash
pip install -e ".[dbc]"
```

## Usage

```bash
obd2-manager
```

Or run directly:
```bash
python -m obd2_manager
```

## Configuration

On first run, enter the ESP32 IP address (or use mDNS: `obd2logger.local`).

## Requirements

- Python 3.9+
- PyQt6
- ESP32 with OBD2 Smart Logger firmware connected to same network
