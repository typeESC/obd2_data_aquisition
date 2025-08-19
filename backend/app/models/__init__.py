"""
Database models for the OBD Data Logger API.
"""

from datetime import datetime
from typing import Optional, List
from uuid import uuid4

from sqlalchemy import (
    Column,
    String,
    Integer,
    Numeric,
    Boolean,
    DateTime,
    ForeignKey,
    Text,
    BIGINT,
    JSON,
)
from sqlalchemy.dialects.postgresql import UUID
from sqlalchemy.ext.hybrid import hybrid_property
from sqlalchemy.orm import relationship
from sqlalchemy.sql import func

from app.core.database import Base


class TimestampMixin:
    """Mixin for created_at and updated_at timestamps."""

    created_at = Column(DateTime(timezone=True), server_default=func.now())
    updated_at = Column(
        DateTime(timezone=True), server_default=func.now(), onupdate=func.now()
    )


class Vehicle(Base, TimestampMixin):
    """Vehicle model."""

    __tablename__ = "vehicles"

    id = Column(UUID(as_uuid=True), primary_key=True, default=uuid4, index=True)
    vin = Column(String(17), unique=True, index=True)
    make = Column(String(50))
    model = Column(String(50))
    year = Column(Integer)
    license_plate = Column(String(20))

    # Relationships
    sessions = relationship(
        "DataSession", back_populates="vehicle", cascade="all, delete-orphan"
    )
    telemetry_data = relationship(
        "TelemetryData", back_populates="vehicle", cascade="all, delete-orphan"
    )
    alerts = relationship(
        "VehicleAlert", back_populates="vehicle", cascade="all, delete-orphan"
    )

    def __repr__(self):
        return f"<Vehicle(id={self.id}, vin={self.vin}, make={self.make}, model={self.model})>"


class DataSession(Base, TimestampMixin):
    """Data session model to group telemetry data."""

    __tablename__ = "data_sessions"

    id = Column(UUID(as_uuid=True), primary_key=True, default=uuid4, index=True)
    vehicle_id = Column(
        UUID(as_uuid=True), ForeignKey("vehicles.id"), nullable=False, index=True
    )
    session_name = Column(String(100))
    start_time = Column(DateTime(timezone=True), server_default=func.now())
    end_time = Column(DateTime(timezone=True))
    total_records = Column(Integer, default=0)

    # Relationships
    vehicle = relationship("Vehicle", back_populates="sessions")
    telemetry_data = relationship(
        "TelemetryData", back_populates="session", cascade="all, delete-orphan"
    )
    batches = relationship(
        "DataBatch", back_populates="session", cascade="all, delete-orphan"
    )
    alerts = relationship(
        "VehicleAlert", back_populates="session", cascade="all, delete-orphan"
    )

    @hybrid_property
    def duration_minutes(self) -> Optional[float]:
        """Calculate session duration in minutes."""
        if self.end_time and self.start_time:
            return (self.end_time - self.start_time).total_seconds() / 60
        return None

    def __repr__(self):
        return f"<DataSession(id={self.id}, vehicle_id={self.vehicle_id}, session_name={self.session_name})>"


class TelemetryData(Base, TimestampMixin):
    """Main telemetry data model."""

    __tablename__ = "telemetry_data"

    id = Column(UUID(as_uuid=True), primary_key=True, default=uuid4, index=True)
    session_id = Column(
        UUID(as_uuid=True), ForeignKey("data_sessions.id"), nullable=False, index=True
    )
    vehicle_id = Column(
        UUID(as_uuid=True), ForeignKey("vehicles.id"), nullable=False, index=True
    )

    # Timestamps
    device_timestamp = Column(
        BIGINT, nullable=False, index=True
    )  # Original ESP32 timestamp
    recorded_at = Column(DateTime(timezone=True), server_default=func.now(), index=True)

    # Engine parameters
    rpm = Column(Numeric(6, 2))
    engine_load = Column(Numeric(5, 2))  # Percentage 0-100
    coolant_temp = Column(Integer)  # Celsius
    oil_temp = Column(Integer)  # Celsius
    run_time = Column(Integer)  # Seconds

    # Vehicle movement
    speed = Column(Integer)  # km/h
    throttle_pos = Column(Numeric(5, 2))  # Percentage 0-100
    relative_throttle = Column(Numeric(5, 2))  # Percentage 0-100

    # Air and fuel system
    timing_advance = Column(Numeric(5, 2))  # Degrees
    intake_air_temp = Column(Integer)  # Celsius
    maf_rate = Column(Numeric(7, 2))  # g/s
    fuel_level = Column(Numeric(5, 2))  # Percentage 0-100
    fuel_rate = Column(Numeric(7, 2))  # L/h
    ethanol_percentage = Column(Numeric(5, 2))  # Percentage 0-100
    commanded_lambda = Column(Numeric(8, 4))  # Equivalence ratio

    # Electrical system
    module_voltage = Column(Numeric(4, 2))  # Volts

    # Diagnostic
    dist_since_clear = Column(Integer)  # km

    # Data quality
    data_quality_score = Column(Numeric(3, 2), default=1.0)  # 0-1
    missing_fields = Column(JSON)  # Array of missing field names

    # Relationships
    session = relationship("DataSession", back_populates="telemetry_data")
    vehicle = relationship("Vehicle", back_populates="telemetry_data")

    def __repr__(self):
        return f"<TelemetryData(id={self.id}, rpm={self.rpm}, speed={self.speed})>"


class DataBatch(Base):
    """Data batch processing model."""

    __tablename__ = "data_batches"

    id = Column(UUID(as_uuid=True), primary_key=True, default=uuid4, index=True)
    session_id = Column(
        UUID(as_uuid=True), ForeignKey("data_sessions.id"), nullable=False
    )
    batch_size = Column(Integer, nullable=False)
    processed_records = Column(Integer, default=0)
    failed_records = Column(Integer, default=0)
    status = Column(String(20), default="processing")  # processing, completed, failed
    error_message = Column(Text)
    started_at = Column(DateTime(timezone=True), server_default=func.now())
    completed_at = Column(DateTime(timezone=True))
    created_at = Column(DateTime(timezone=True), server_default=func.now())

    # Relationships
    session = relationship("DataSession", back_populates="batches")

    @hybrid_property
    def success_rate(self) -> float:
        """Calculate batch success rate."""
        if self.batch_size == 0:
            return 0.0
        return self.processed_records / self.batch_size

    def __repr__(self):
        return f"<DataBatch(id={self.id}, status={self.status}, success_rate={self.success_rate})>"


class VehicleAlert(Base, TimestampMixin):
    """Vehicle alert model for threshold monitoring."""

    __tablename__ = "vehicle_alerts"

    id = Column(UUID(as_uuid=True), primary_key=True, default=uuid4, index=True)
    vehicle_id = Column(
        UUID(as_uuid=True), ForeignKey("vehicles.id"), nullable=False, index=True
    )
    session_id = Column(
        UUID(as_uuid=True), ForeignKey("data_sessions.id"), nullable=True
    )
    alert_type = Column(
        String(50), nullable=False
    )  # high_temp, low_fuel, high_rpm, etc.
    severity = Column(String(20), nullable=False)  # low, medium, high, critical
    message = Column(Text, nullable=False)
    parameter_name = Column(String(50))
    parameter_value = Column(Numeric)
    threshold_value = Column(Numeric)
    triggered_at = Column(
        DateTime(timezone=True), server_default=func.now(), index=True
    )
    acknowledged = Column(Boolean, default=False, index=True)
    acknowledged_at = Column(DateTime(timezone=True))
    acknowledged_by = Column(String(100))

    # Relationships
    vehicle = relationship("Vehicle", back_populates="alerts")
    session = relationship("DataSession", back_populates="alerts")

    def __repr__(self):
        return f"<VehicleAlert(id={self.id}, alert_type={self.alert_type}, severity={self.severity})>"
