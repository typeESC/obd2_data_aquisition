"""
Pydantic schemas for API request/response validation.
"""

from datetime import datetime
from typing import Optional, List, Dict, Any
from uuid import UUID

from pydantic import BaseModel, Field, validator, ConfigDict


class BaseSchema(BaseModel):
    """Base schema with common configuration."""

    model_config = ConfigDict(from_attributes=True)


# Vehicle schemas
class VehicleBase(BaseSchema):
    """Base vehicle schema."""

    vin: Optional[str] = Field(
        None, max_length=17, description="Vehicle Identification Number"
    )
    make: Optional[str] = Field(None, max_length=50, description="Vehicle manufacturer")
    model: Optional[str] = Field(None, max_length=50, description="Vehicle model")
    year: Optional[int] = Field(None, ge=1900, le=2100, description="Vehicle year")
    license_plate: Optional[str] = Field(
        None, max_length=20, description="License plate number"
    )


class VehicleCreate(VehicleBase):
    """Schema for creating a vehicle."""


class VehicleUpdate(VehicleBase):
    """Schema for updating a vehicle."""


class VehicleResponse(VehicleBase):
    """Schema for vehicle response."""

    id: UUID
    created_at: datetime
    updated_at: datetime


# Session schemas
class DataSessionBase(BaseSchema):
    """Base data session schema."""

    session_name: Optional[str] = Field(
        None, max_length=100, description="Session name"
    )


class DataSessionCreate(DataSessionBase):
    """Schema for creating a data session."""

    vehicle_id: UUID


class DataSessionUpdate(DataSessionBase):
    """Schema for updating a data session."""

    end_time: Optional[datetime] = None


class DataSessionResponse(DataSessionBase):
    """Schema for data session response."""

    id: UUID
    vehicle_id: UUID
    start_time: datetime
    end_time: Optional[datetime]
    total_records: int
    duration_minutes: Optional[float]
    created_at: datetime
    updated_at: datetime


# Telemetry data schemas
class TelemetryDataBase(BaseSchema):
    """Base telemetry data schema."""

    device_timestamp: int = Field(
        ..., description="Original timestamp from ESP32 device"
    )
    rpm: Optional[float] = Field(None, ge=0, le=10000, description="Engine RPM")
    engine_load: Optional[float] = Field(
        None, ge=0, le=100, description="Engine load percentage"
    )
    coolant_temp: Optional[int] = Field(
        None, ge=-40, le=200, description="Coolant temperature in Celsius"
    )
    oil_temp: Optional[int] = Field(
        None, ge=-40, le=200, description="Oil temperature in Celsius"
    )
    run_time: Optional[int] = Field(None, ge=0, description="Engine runtime in seconds")
    speed: Optional[int] = Field(
        None, ge=0, le=500, description="Vehicle speed in km/h"
    )
    throttle_pos: Optional[float] = Field(
        None, ge=0, le=100, description="Throttle position percentage"
    )
    relative_throttle: Optional[float] = Field(
        None, ge=0, le=100, description="Relative throttle position"
    )
    timing_advance: Optional[float] = Field(
        None, ge=-64, le=64, description="Timing advance in degrees"
    )
    intake_air_temp: Optional[int] = Field(
        None, ge=-40, le=200, description="Intake air temperature"
    )
    maf_rate: Optional[float] = Field(
        None, ge=0, le=1000, description="Mass airflow rate in g/s"
    )
    fuel_level: Optional[float] = Field(
        None, ge=0, le=100, description="Fuel level percentage"
    )
    fuel_rate: Optional[float] = Field(
        None, ge=0, le=100, description="Fuel consumption rate"
    )
    ethanol_percentage: Optional[float] = Field(
        None, ge=0, le=100, description="Ethanol percentage"
    )
    commanded_lambda: Optional[float] = Field(
        None, ge=0, le=2, description="Commanded equivalence ratio"
    )
    module_voltage: Optional[float] = Field(
        None, ge=0, le=20, description="Control module voltage"
    )
    dist_since_clear: Optional[int] = Field(
        None, ge=0, description="Distance since codes cleared"
    )


class TelemetryDataCreate(TelemetryDataBase):
    """Schema for creating telemetry data."""


class TelemetryDataBatchCreate(BaseSchema):
    """Schema for batch creating telemetry data."""

    session_id: UUID
    vehicle_id: Optional[UUID] = None  # If not provided, will use session's vehicle
    data: List[TelemetryDataCreate] = Field(..., min_items=1, max_items=1000)


class TelemetryDataResponse(TelemetryDataBase):
    """Schema for telemetry data response."""

    id: UUID
    session_id: UUID
    vehicle_id: UUID
    recorded_at: datetime
    data_quality_score: float
    missing_fields: Optional[List[str]]
    created_at: datetime


# Alert schemas
class VehicleAlertBase(BaseSchema):
    """Base vehicle alert schema."""

    alert_type: str = Field(..., max_length=50)
    severity: str = Field(..., pattern="^(low|medium|high|critical)$")
    message: str
    parameter_name: Optional[str] = Field(None, max_length=50)
    parameter_value: Optional[float] = None
    threshold_value: Optional[float] = None


class VehicleAlertCreate(VehicleAlertBase):
    """Schema for creating a vehicle alert."""

    vehicle_id: UUID
    session_id: Optional[UUID] = None


class VehicleAlertUpdate(BaseSchema):
    """Schema for updating a vehicle alert."""

    acknowledged: bool = True
    acknowledged_by: Optional[str] = Field(None, max_length=100)


class VehicleAlertResponse(VehicleAlertBase):
    """Schema for vehicle alert response."""

    id: UUID
    vehicle_id: UUID
    session_id: Optional[UUID]
    triggered_at: datetime
    acknowledged: bool
    acknowledged_at: Optional[datetime]
    acknowledged_by: Optional[str]
    created_at: datetime


# Batch processing schemas
class DataBatchResponse(BaseSchema):
    """Schema for data batch response."""

    id: UUID
    session_id: UUID
    batch_size: int
    processed_records: int
    failed_records: int
    status: str
    error_message: Optional[str]
    success_rate: float
    started_at: datetime
    completed_at: Optional[datetime]


# API response schemas
class APIResponse(BaseSchema):
    """Generic API response schema."""

    success: bool = True
    message: str = "Operation completed successfully"
    data: Optional[Any] = None


class PaginatedResponse(BaseSchema):
    """Paginated response schema."""

    items: List[Any]
    total: int
    page: int = Field(..., ge=1)
    size: int = Field(..., ge=1, le=1000)
    pages: int


class HealthCheckResponse(BaseSchema):
    """Health check response schema."""

    status: str = "healthy"
    timestamp: datetime
    version: str
    database: str = "connected"
    dependencies: Dict[str, str] = {}


# Statistics schemas
class SessionStatistics(BaseSchema):
    """Session statistics schema."""

    total_records: int
    duration_minutes: Optional[float]
    avg_rpm: float
    max_rpm: float
    avg_speed: float
    max_speed: float
    avg_coolant_temp: float
    max_coolant_temp: float
    avg_fuel_consumption: float


class VehicleStatistics(BaseSchema):
    """Vehicle statistics schema."""

    total_sessions: int
    total_records: int
    total_distance: float
    total_runtime_hours: float
    avg_fuel_consumption: float
    last_activity: Optional[datetime]


# Validation schemas for CSV import
class CSVImportRequest(BaseSchema):
    """Schema for CSV import request."""

    session_id: UUID
    file_content: str = Field(..., description="Base64 encoded CSV content")
    delimiter: str = Field(default=",", max_length=1)
    skip_header: bool = Field(default=True)


class CSVImportResponse(BaseSchema):
    """Schema for CSV import response."""

    batch_id: UUID
    total_rows: int
    processed_rows: int
    failed_rows: int
    errors: List[str] = []
    warnings: List[str] = []
