"""
Service layer for telemetry data operations.
"""

import csv
import io
import base64
from datetime import datetime
from typing import List, Optional, Dict, Any, Tuple
from uuid import UUID

from sqlalchemy import select, func, and_, desc
from sqlalchemy.ext.asyncio import AsyncSession

from app.models import TelemetryData, DataSession, DataBatch, VehicleAlert
from app.schemas import (
    TelemetryDataCreate,
    TelemetryDataBatchCreate,
    TelemetryDataResponse,
    DataBatchResponse,
    CSVImportRequest,
    CSVImportResponse,
    VehicleAlertCreate,
)
from app.core.config import settings
from app.core.logging import get_logger

logger = get_logger(__name__)


class TelemetryService:
    """Service for handling telemetry data operations."""

    def __init__(self, db: AsyncSession):
        self.db = db

    async def create_single_record(
        self,
        session_id: UUID,
        data: TelemetryDataCreate,
        vehicle_id: Optional[UUID] = None,
    ) -> TelemetryDataResponse:
        """
        Create a single telemetry data record.

        Args:
            session_id: Session ID
            data: Telemetry data
            vehicle_id: Vehicle ID (optional, will use session's vehicle if not provided)

        Returns:
            Created telemetry data record
        """
        # Get session and vehicle info if needed
        if not vehicle_id:
            session_query = select(DataSession).where(DataSession.id == session_id)
            result = await self.db.execute(session_query)
            session = result.scalar_one_or_none()
            if not session:
                raise ValueError(f"Session {session_id} not found")
            vehicle_id = session.vehicle_id

        # Calculate data quality score
        quality_score, missing_fields = self._calculate_data_quality(data)

        # Create record
        db_record = TelemetryData(
            session_id=session_id,
            vehicle_id=vehicle_id,
            data_quality_score=quality_score,
            missing_fields=missing_fields,
            **data.model_dump(exclude_unset=True),
        )

        self.db.add(db_record)
        await self.db.commit()
        await self.db.refresh(db_record)

        # Check for alerts
        await self._check_alerts(db_record)

        logger.info("Created telemetry record", record_id=str(db_record.id))
        return TelemetryDataResponse.model_validate(db_record)

    async def create_batch(
        self, batch_data: TelemetryDataBatchCreate
    ) -> DataBatchResponse:
        """
        Create a batch of telemetry data records.

        Args:
            batch_data: Batch of telemetry data

        Returns:
            Batch processing result
        """
        # Get session info
        session_query = select(DataSession).where(
            DataSession.id == batch_data.session_id
        )
        result = await self.db.execute(session_query)
        session = result.scalar_one_or_none()
        if not session:
            raise ValueError(f"Session {batch_data.session_id} not found")

        vehicle_id = batch_data.vehicle_id or session.vehicle_id

        # Create batch record
        batch_record = DataBatch(
            session_id=batch_data.session_id,
            batch_size=len(batch_data.data),
            status="processing",
        )
        self.db.add(batch_record)
        await self.db.flush()

        processed_records = 0
        failed_records = 0
        errors = []

        try:
            # Process each record
            for i, data in enumerate(batch_data.data):
                try:
                    quality_score, missing_fields = self._calculate_data_quality(data)

                    db_record = TelemetryData(
                        session_id=batch_data.session_id,
                        vehicle_id=vehicle_id,
                        data_quality_score=quality_score,
                        missing_fields=missing_fields,
                        **data.model_dump(exclude_unset=True),
                    )

                    self.db.add(db_record)
                    processed_records += 1

                    # Check for alerts (async, don't wait)
                    await self._check_alerts(db_record)

                except Exception as e:
                    failed_records += 1
                    errors.append(f"Record {i}: {str(e)}")
                    logger.error(
                        "Failed to process record", record_index=i, error=str(e)
                    )

            # Update batch status
            batch_record.processed_records = processed_records
            batch_record.failed_records = failed_records
            batch_record.status = "completed" if failed_records == 0 else "partial"
            batch_record.completed_at = datetime.utcnow()

            if errors:
                batch_record.error_message = "; ".join(
                    errors[:10]
                )  # Limit error message size

            await self.db.commit()

            logger.info(
                "Processed batch",
                batch_id=str(batch_record.id),
                processed=processed_records,
                failed=failed_records,
            )

            return DataBatchResponse.model_validate(batch_record)

        except Exception as e:
            batch_record.status = "failed"
            batch_record.error_message = str(e)
            batch_record.completed_at = datetime.utcnow()
            await self.db.commit()
            logger.error(
                "Batch processing failed", batch_id=str(batch_record.id), error=str(e)
            )
            raise

    async def get_records(
        self,
        session_id: Optional[UUID] = None,
        vehicle_id: Optional[UUID] = None,
        start_time: Optional[datetime] = None,
        end_time: Optional[datetime] = None,
        limit: int = 100,
        offset: int = 0,
    ) -> Tuple[List[TelemetryDataResponse], int]:
        """
        Get telemetry data records with filtering.

        Args:
            session_id: Filter by session ID
            vehicle_id: Filter by vehicle ID
            start_time: Filter by start time
            end_time: Filter by end time
            limit: Maximum number of records
            offset: Number of records to skip

        Returns:
            Tuple of (records, total_count)
        """
        query = select(TelemetryData)
        count_query = select(func.count(TelemetryData.id))

        # Apply filters
        conditions = []
        if session_id:
            conditions.append(TelemetryData.session_id == session_id)
        if vehicle_id:
            conditions.append(TelemetryData.vehicle_id == vehicle_id)
        if start_time:
            conditions.append(TelemetryData.recorded_at >= start_time)
        if end_time:
            conditions.append(TelemetryData.recorded_at <= end_time)

        if conditions:
            query = query.where(and_(*conditions))
            count_query = count_query.where(and_(*conditions))

        # Get total count
        count_result = await self.db.execute(count_query)
        total = count_result.scalar() or 0

        # Get records
        query = (
            query.order_by(desc(TelemetryData.recorded_at)).limit(limit).offset(offset)
        )
        result = await self.db.execute(query)
        records = result.scalars().all()

        return [
            TelemetryDataResponse.model_validate(record) for record in records
        ], total

    async def import_csv(self, import_request: CSVImportRequest) -> CSVImportResponse:
        """
        Import telemetry data from CSV.

        Args:
            import_request: CSV import request

        Returns:
            Import result
        """
        try:
            # Decode CSV content
            csv_content = base64.b64decode(import_request.file_content).decode("utf-8")
            csv_reader = csv.DictReader(
                io.StringIO(csv_content), delimiter=import_request.delimiter
            )

            # Skip header if requested
            if import_request.skip_header:
                next(csv_reader, None)

            # Create batch record
            batch_record = DataBatch(
                session_id=import_request.session_id,
                batch_size=0,  # Will be updated
                status="processing",
            )
            self.db.add(batch_record)
            await self.db.flush()

            # Process CSV rows
            telemetry_data = []
            errors = []
            warnings = []
            row_count = 0

            for row_num, row in enumerate(csv_reader, 1):
                row_count += 1
                try:
                    # Map CSV columns to telemetry fields
                    mapped_data = self._map_csv_row(row)
                    telemetry_data.append(TelemetryDataCreate(**mapped_data))

                except Exception as e:
                    errors.append(f"Row {row_num}: {str(e)}")

            # Update batch size
            batch_record.batch_size = row_count

            # Create batch data
            if telemetry_data:
                batch_create = TelemetryDataBatchCreate(
                    session_id=import_request.session_id, data=telemetry_data
                )

                # Process the batch
                batch_result = await self.create_batch(batch_create)

                return CSVImportResponse(
                    batch_id=batch_result.id,
                    total_rows=row_count,
                    processed_rows=batch_result.processed_records,
                    failed_rows=batch_result.failed_records,
                    errors=errors,
                    warnings=warnings,
                )
            else:
                batch_record.status = "failed"
                batch_record.error_message = "No valid data found in CSV"
                await self.db.commit()

                return CSVImportResponse(
                    batch_id=batch_record.id,
                    total_rows=row_count,
                    processed_rows=0,
                    failed_rows=row_count,
                    errors=errors + ["No valid data found"],
                    warnings=warnings,
                )

        except Exception as e:
            logger.error("CSV import failed", error=str(e))
            raise ValueError(f"CSV import failed: {str(e)}")

    def _calculate_data_quality(
        self, data: TelemetryDataCreate
    ) -> Tuple[float, List[str]]:
        """
        Calculate data quality score and identify missing fields.

        Args:
            data: Telemetry data

        Returns:
            Tuple of (quality_score, missing_fields)
        """
        required_fields = [
            "rpm",
            "speed",
            "coolant_temp",
            "engine_load",
            "throttle_pos",
            "fuel_level",
            "module_voltage",
        ]

        data_dict = data.model_dump(exclude_unset=True)
        missing_fields = []
        present_fields = 0

        for field in required_fields:
            if field in data_dict and data_dict[field] is not None:
                present_fields += 1
            else:
                missing_fields.append(field)

        quality_score = present_fields / len(required_fields)
        return quality_score, missing_fields

    async def _check_alerts(self, record: TelemetryData) -> None:
        """
        Check telemetry data for alert conditions.

        Args:
            record: Telemetry data record
        """
        alerts = []

        # High RPM alert
        if record.rpm and record.rpm > settings.HIGH_RPM_THRESHOLD:
            alerts.append(
                VehicleAlertCreate(
                    vehicle_id=record.vehicle_id,
                    session_id=record.session_id,
                    alert_type="high_rpm",
                    severity=(
                        "high"
                        if record.rpm > settings.HIGH_RPM_THRESHOLD * 1.2
                        else "medium"
                    ),
                    message=f"High RPM detected: {record.rpm}",
                    parameter_name="rpm",
                    parameter_value=float(record.rpm),
                    threshold_value=settings.HIGH_RPM_THRESHOLD,
                )
            )

        # High coolant temperature alert
        if (
            record.coolant_temp
            and record.coolant_temp > settings.HIGH_COOLANT_TEMP_THRESHOLD
        ):
            alerts.append(
                VehicleAlertCreate(
                    vehicle_id=record.vehicle_id,
                    session_id=record.session_id,
                    alert_type="high_coolant_temp",
                    severity="critical",
                    message=f"High coolant temperature: {record.coolant_temp}°C",
                    parameter_name="coolant_temp",
                    parameter_value=float(record.coolant_temp),
                    threshold_value=float(settings.HIGH_COOLANT_TEMP_THRESHOLD),
                )
            )

        # Low fuel alert
        if record.fuel_level and record.fuel_level < settings.LOW_FUEL_THRESHOLD:
            alerts.append(
                VehicleAlertCreate(
                    vehicle_id=record.vehicle_id,
                    session_id=record.session_id,
                    alert_type="low_fuel",
                    severity="medium",
                    message=f"Low fuel level: {record.fuel_level}%",
                    parameter_name="fuel_level",
                    parameter_value=float(record.fuel_level),
                    threshold_value=settings.LOW_FUEL_THRESHOLD,
                )
            )

        # Low voltage alert
        if (
            record.module_voltage
            and record.module_voltage < settings.LOW_VOLTAGE_THRESHOLD
        ):
            alerts.append(
                VehicleAlertCreate(
                    vehicle_id=record.vehicle_id,
                    session_id=record.session_id,
                    alert_type="low_voltage",
                    severity="high",
                    message=f"Low module voltage: {record.module_voltage}V",
                    parameter_name="module_voltage",
                    parameter_value=float(record.module_voltage),
                    threshold_value=settings.LOW_VOLTAGE_THRESHOLD,
                )
            )

        # Create alert records
        for alert_data in alerts:
            alert_record = VehicleAlert(**alert_data.model_dump())
            self.db.add(alert_record)

        if alerts:
            await self.db.flush()
            logger.info(
                "Created alerts", count=len(alerts), vehicle_id=str(record.vehicle_id)
            )

    def _map_csv_row(self, row: Dict[str, str]) -> Dict[str, Any]:
        """
        Map CSV row to telemetry data fields.

        Args:
            row: CSV row as dictionary

        Returns:
            Mapped telemetry data
        """
        # Map based on the CSV header from the ESP32 code
        mapping = {
            "Timestamp": "device_timestamp",
            "RPM": "rpm",
            "Velocidade": "speed",
            "TempAgua": "coolant_temp",
            "CargaMotor": "engine_load",
            "AvancoIgnicao": "timing_advance",
            "TempArAdmissao": "intake_air_temp",
            "MAF": "maf_rate",
            "PosAcelerador": "throttle_pos",
            "TempoMotorLigado": "run_time",
            "DistCodApagados": "dist_since_clear",
            "NivelCombustivel": "fuel_level",
            "TensaoModulo": "module_voltage",
            "LambdaComandado": "commanded_lambda",
            "PosRelAcelerador": "relative_throttle",
            "PercEtanol": "ethanol_percentage",
            "TempOleo": "oil_temp",
            "TaxaCombustivel": "fuel_rate",
        }

        mapped_data = {}
        for csv_key, field_name in mapping.items():
            if csv_key in row and row[csv_key].strip():
                try:
                    value = row[csv_key].strip()
                    if field_name == "device_timestamp":
                        mapped_data[field_name] = int(value)
                    elif field_name in [
                        "coolant_temp",
                        "speed",
                        "intake_air_temp",
                        "run_time",
                        "dist_since_clear",
                        "oil_temp",
                    ]:
                        mapped_data[field_name] = int(float(value))
                    else:
                        mapped_data[field_name] = float(value)
                except (ValueError, TypeError):
                    # Skip invalid values
                    pass

        return mapped_data
