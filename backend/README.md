# OBD Data Logger API

A comprehensive FastAPI backend for receiving, storing, and analyzing OBD-II vehicle telemetry data from ESP32 devices.

## Features

- 🚗 **Vehicle Management**: Create and manage vehicle profiles
- 📊 **Telemetry Data**: Receive and store OBD-II data in real-time
- 🔄 **Batch Processing**: Handle large batches of telemetry data efficiently
- 📁 **CSV Import**: Import historical data from CSV files
- ⚠️ **Smart Alerts**: Automatic threshold-based alerts for vehicle parameters
- 📈 **Statistics**: Comprehensive vehicle and session statistics
- 🗃️ **PostgreSQL**: Robust data storage with optimized schemas
- 📝 **API Documentation**: Auto-generated OpenAPI documentation
- 🐳 **Docker**: Containerized deployment for easy setup
- 🔒 **Security**: CORS, rate limiting, and input validation
- 📋 **Logging**: Structured logging for monitoring and debugging

## Quick Start

### Prerequisites

- Docker and Docker Compose
- Git

### Development Setup

1. **Clone the repository**
   ```bash
   git clone <repository-url>
   cd obd2_data_aquisition/backend
   ```

2. **Run the setup script**
   
   **Linux/macOS:**
   ```bash
   chmod +x setup-dev.sh
   ./setup-dev.sh
   ```
   
   **Windows:**
   ```cmd
   setup-dev.bat
   ```

3. **Access the application**
   - API: http://localhost:8000
   - API Documentation: http://localhost:8000/api/v1/docs
   - Database: localhost:5432
   - Redis: localhost:6379

### Manual Setup

1. **Create environment file**
   ```bash
   cp .env.example .env
   ```
   Edit `.env` with your configuration.

2. **Start services**
   ```bash
   docker-compose up -d --build
   ```

3. **View logs**
   ```bash
   docker-compose logs -f
   ```

## API Endpoints

### Vehicle Management

- `POST /api/v1/vehicles` - Create a new vehicle
- `GET /api/v1/vehicles` - List all vehicles
- `GET /api/v1/vehicles/{id}` - Get vehicle details
- `PUT /api/v1/vehicles/{id}` - Update vehicle
- `DELETE /api/v1/vehicles/{id}` - Delete vehicle
- `GET /api/v1/vehicles/{id}/statistics` - Get vehicle statistics

### Session Management

- `POST /api/v1/sessions` - Create a new data session
- `GET /api/v1/sessions` - List all sessions
- `GET /api/v1/sessions/{id}` - Get session details
- `PUT /api/v1/sessions/{id}` - Update session
- `DELETE /api/v1/sessions/{id}` - Delete session
- `GET /api/v1/sessions/{id}/statistics` - Get session statistics

### Telemetry Data

- `POST /api/v1/telemetry/records` - Create single telemetry record
- `POST /api/v1/telemetry/batch` - Create batch of telemetry records
- `GET /api/v1/telemetry/records` - Get telemetry records with filtering
- `POST /api/v1/telemetry/import/csv` - Import data from CSV
- `POST /api/v1/telemetry/upload/csv` - Upload and import CSV file

### System

- `GET /health` - Health check
- `GET /` - API information

## Data Schema

### Telemetry Data Fields

The API accepts telemetry data matching the ESP32 OBD logger format:

| Field | Type | Description | Unit |
|-------|------|-------------|------|
| `device_timestamp` | integer | ESP32 timestamp | milliseconds |
| `rpm` | float | Engine RPM | rpm |
| `speed` | integer | Vehicle speed | km/h |
| `coolant_temp` | integer | Coolant temperature | °C |
| `engine_load` | float | Engine load | % |
| `timing_advance` | float | Timing advance | degrees |
| `intake_air_temp` | integer | Intake air temperature | °C |
| `maf_rate` | float | Mass airflow rate | g/s |
| `throttle_pos` | float | Throttle position | % |
| `run_time` | integer | Engine runtime | seconds |
| `dist_since_clear` | integer | Distance since codes cleared | km |
| `fuel_level` | float | Fuel level | % |
| `module_voltage` | float | Control module voltage | V |
| `commanded_lambda` | float | Commanded equivalence ratio | ratio |
| `relative_throttle` | float | Relative throttle position | % |
| `ethanol_percentage` | float | Ethanol fuel percentage | % |
| `oil_temp` | integer | Oil temperature | °C |
| `fuel_rate` | float | Fuel consumption rate | L/h |

### Example API Usage

#### Create a Vehicle
```bash
curl -X POST "http://localhost:8000/api/v1/vehicles" \
  -H "Content-Type: application/json" \
  -d '{
    "vin": "1HGBH41JXMN109186",
    "make": "Honda",
    "model": "Civic",
    "year": 2021,
    "license_plate": "ABC-1234"
  }'
```

#### Create a Session
```bash
curl -X POST "http://localhost:8000/api/v1/sessions" \
  -H "Content-Type: application/json" \
  -d '{
    "vehicle_id": "550e8400-e29b-41d4-a716-446655440000",
    "session_name": "Morning Commute"
  }'
```

#### Send Telemetry Data Batch
```bash
curl -X POST "http://localhost:8000/api/v1/telemetry/batch" \
  -H "Content-Type: application/json" \
  -d '{
    "session_id": "550e8400-e29b-41d4-a716-446655440001",
    "data": [
      {
        "device_timestamp": 1691234567890,
        "rpm": 2500.0,
        "speed": 80,
        "coolant_temp": 85,
        "engine_load": 45.2,
        "fuel_level": 75.5,
        "module_voltage": 13.8
      }
    ]
  }'
```

## Configuration

### Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `POSTGRES_DB` | obd_logger | Database name |
| `POSTGRES_USER` | obd_user | Database user |
| `POSTGRES_PASSWORD` | obd_password | Database password |
| `SECRET_KEY` | - | JWT secret key |
| `HIGH_RPM_THRESHOLD` | 6000 | High RPM alert threshold |
| `HIGH_COOLANT_TEMP_THRESHOLD` | 105 | High temperature alert threshold |
| `LOW_FUEL_THRESHOLD` | 10 | Low fuel alert threshold |
| `MAX_BATCH_SIZE` | 1000 | Maximum batch size |

See `.env.example` for all available configuration options.

## Database Schema

The application uses PostgreSQL with the following main tables:

- **vehicles**: Vehicle information and metadata
- **data_sessions**: Logical grouping of telemetry data
- **telemetry_data**: Main table for OBD-II data
- **vehicle_alerts**: Threshold-based alerts
- **data_batches**: Batch processing tracking

## Alert System

The API automatically monitors telemetry data and creates alerts when:

- RPM exceeds threshold (default: 6000 RPM)
- Coolant temperature is too high (default: 105°C)
- Fuel level is low (default: 10%)
- Oil temperature is too high (default: 140°C)
- Module voltage is low (default: 11.5V)

## Development

### Local Development

1. **Install dependencies**
   ```bash
   pip install -r requirements.txt
   ```

2. **Set up database**
   ```bash
   docker-compose up -d db redis
   ```

3. **Run migrations**
   ```bash
   # Apply the SQL migration manually or use Alembic
   ```

4. **Start the API**
   ```bash
   uvicorn app.main:app --reload
   ```

### Code Quality

- **Black**: Code formatting
- **isort**: Import sorting
- **flake8**: Linting
- **mypy**: Type checking
- **pytest**: Testing

### Testing

```bash
# Run all tests
pytest

# Run with coverage
pytest --cov=app --cov-report=html
```

## Production Deployment

### With Docker Compose

1. **Update environment variables** for production
2. **Use production profile**
   ```bash
   docker-compose --profile production up -d
   ```

### With Kubernetes

Deploy using the provided Kubernetes manifests (create separately).

### Performance Tuning

- Use connection pooling for database
- Configure Redis for caching
- Set up horizontal pod autoscaling
- Use CDN for static assets

## Monitoring

### Health Checks

- API health: `GET /health`
- Database connectivity check
- Redis connectivity check

### Metrics

The API exposes Prometheus metrics on port 8001 (if enabled).

### Logging

Structured JSON logging with configurable levels:
- Request/response logging
- Error tracking
- Performance metrics
- Security events

## Security

- CORS protection
- Rate limiting
- Input validation
- SQL injection prevention
- XSS protection headers

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests
5. Submit a pull request

## License

[Add your license here]

## Support

For issues and questions:
1. Check the documentation
2. Search existing issues
3. Create a new issue with details

## Changelog

### v1.0.0
- Initial release
- Complete OBD-II data ingestion
- Vehicle and session management
- Alert system
- CSV import functionality
- Docker containerization
