# Copilot Instructions for OBDScan (obd2_data_aquisition)

## Project Overview
- **OBDScan** is a real-time vehicle telemetry system using ESP32 (C, ESP-IDF, FreeRTOS) and a FastAPI backend (Python).
- Data flows: Vehicle CAN bus → ESP32 (batching, validation) → HTTP/JSON → FastAPI backend → PostgreSQL.
- ESP32 firmware and backend are decoupled but share data contracts (see `main/` and `backend/app/schemas/`).

## Key Components
- `main/` (ESP32 firmware):
  - `obd_can.c/h`: CAN bus interface, OBD-II PID polling, error handling.
  - `obd_parser.c/h`: PID-specific parsing, validation, and conversion to telemetry structs.
  - `api_client.c/h`: HTTP client, batching, fallback logic, JSON serialization (cJSON), thread safety (FreeRTOS mutex).
  - `wifi_manager.c/h`: WiFi connection, reconnection, and monitoring.
  - `obd_config.h`: Device-specific config (WiFi, API URL, IDs, pins). Must be customized per device.
- `backend/app/`: FastAPI app, with `api/`, `core/`, `models/`, `schemas/`, and `services/` submodules.
- `backend/docker-compose.yml`: Orchestrates API, PostgreSQL, Redis, PgAdmin, and Nginx (optional).

## Build & Test Workflows
- **ESP32**:
  - Use ESP-IDF (`idf.py build`, `idf.py flash`, `idf.py monitor`).
  - Edit `main/obd_config.h` for WiFi, API, and IDs before flashing.
  - Debug logs: `idf.py monitor | grep "E ("` for errors.
- **Backend**:
  - Use `backend/setup-dev.sh` (Linux/macOS) or `setup-dev.bat` (Windows) for local dev.
  - Start all services: `docker-compose up -d --build` in `backend/`.
  - Logs: `docker-compose logs -f api` (API), `docker-compose logs postgres` (DB).
  - API docs: [http://localhost:8000/docs](http://localhost:8000/docs)

## Project-Specific Patterns
- **Batching**: ESP32 batches telemetry (default 10 records, configurable) for efficient HTTP transfer.
- **Fallback Mode**: If API is unreachable, ESP32 enters fallback (offline) mode, resumes when healthy.
- **JSON Serialization**: Use cJSON on ESP32; Pydantic on backend. Field names and types must match.
- **Thread Safety**: All API client operations on ESP32 are mutex-protected.
- **Logging**: Use `ESP_LOG*` macros for ESP32; Python logging for backend. Debug logs can be filtered by tag.
- **Error Handling**: ESP32 retries failed HTTP requests (3x, exponential backoff), then triggers fallback.
- **Config**: Never commit secrets in `obd_config.h` or `.env`.

## Integration Points
- **API Endpoints**: `/api/v1/telemetry/batch`, `/api/v1/vehicles`, `/api/v1/health` (see backend/app/api/telemetry.py).
- **Data Contracts**: See `main/api_client.c` and `backend/app/schemas/telemetry.py` for JSON structure.
- **Docker Compose**: All backend services are started via `docker-compose` in `backend/`.

## Examples
- ESP32 batch send: see `api_send_telemetry_batch()` in `main/api_client.c`.
- Backend batch receive: see `post_telemetry_batch()` in `backend/app/api/telemetry.py`.
- Custom PID parsing: see `obd_parse_response()` in `main/obd_parser.c`.

## Conventions
- C: Use `DEBUG_LOG` macro for debug (define as `ESP_LOGD(TAG, ...)` if not present).
- Python: Format with Black, lint with Pylint.
- Commit messages: Conventional (feat:, fix:, docs:, style:, refactor:, test:).

---
For more, see `README.md` and code comments in each module.
