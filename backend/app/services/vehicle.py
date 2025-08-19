"""
Service layer for vehicle and session operations.
"""

from typing import List, Optional, Tuple
from uuid import UUID

from sqlalchemy import select, func, desc
from sqlalchemy.ext.asyncio import AsyncSession

from app.models import Vehicle, DataSession, TelemetryData
from app.schemas import (
    VehicleCreate,
    VehicleUpdate,
    VehicleResponse,
    DataSessionCreate,
    DataSessionUpdate,
    DataSessionResponse,
    SessionStatistics,
    VehicleStatistics,
)
from app.core.logging import get_logger

logger = get_logger(__name__)


class VehicleService:
    """Service for handling vehicle operations."""

    def __init__(self, db: AsyncSession):
        self.db = db

    async def create_vehicle(self, vehicle_data: VehicleCreate) -> VehicleResponse:
        """
        Create a new vehicle.

        Args:
            vehicle_data: Vehicle creation data

        Returns:
            Created vehicle
        """
        db_vehicle = Vehicle(**vehicle_data.model_dump(exclude_unset=True))
        self.db.add(db_vehicle)
        await self.db.commit()
        await self.db.refresh(db_vehicle)

        logger.info("Created vehicle", vehicle_id=str(db_vehicle.id))
        return VehicleResponse.model_validate(db_vehicle)

    async def get_vehicle(self, vehicle_id: UUID) -> Optional[VehicleResponse]:
        """
        Get a vehicle by ID.

        Args:
            vehicle_id: Vehicle ID

        Returns:
            Vehicle if found
        """
        query = select(Vehicle).where(Vehicle.id == vehicle_id)
        result = await self.db.execute(query)
        vehicle = result.scalar_one_or_none()

        if vehicle:
            return VehicleResponse.model_validate(vehicle)
        return None

    async def get_vehicles(
        self, skip: int = 0, limit: int = 100
    ) -> Tuple[List[VehicleResponse], int]:
        """
        Get all vehicles with pagination.

        Args:
            skip: Number of records to skip
            limit: Maximum number of records

        Returns:
            Tuple of (vehicles, total_count)
        """
        # Get total count
        count_query = select(func.count(Vehicle.id))
        count_result = await self.db.execute(count_query)
        total = count_result.scalar() or 0

        # Get vehicles
        query = (
            select(Vehicle).order_by(desc(Vehicle.created_at)).offset(skip).limit(limit)
        )
        result = await self.db.execute(query)
        vehicles = result.scalars().all()

        return [VehicleResponse.model_validate(v) for v in vehicles], total

    async def update_vehicle(
        self, vehicle_id: UUID, vehicle_data: VehicleUpdate
    ) -> Optional[VehicleResponse]:
        """
        Update a vehicle.

        Args:
            vehicle_id: Vehicle ID
            vehicle_data: Vehicle update data

        Returns:
            Updated vehicle if found
        """
        query = select(Vehicle).where(Vehicle.id == vehicle_id)
        result = await self.db.execute(query)
        vehicle = result.scalar_one_or_none()

        if not vehicle:
            return None

        update_data = vehicle_data.model_dump(exclude_unset=True)
        for key, value in update_data.items():
            setattr(vehicle, key, value)

        await self.db.commit()
        await self.db.refresh(vehicle)

        logger.info("Updated vehicle", vehicle_id=str(vehicle_id))
        return VehicleResponse.model_validate(vehicle)

    async def delete_vehicle(self, vehicle_id: UUID) -> bool:
        """
        Delete a vehicle.

        Args:
            vehicle_id: Vehicle ID

        Returns:
            True if deleted, False if not found
        """
        query = select(Vehicle).where(Vehicle.id == vehicle_id)
        result = await self.db.execute(query)
        vehicle = result.scalar_one_or_none()

        if not vehicle:
            return False

        await self.db.delete(vehicle)
        await self.db.commit()

        logger.info("Deleted vehicle", vehicle_id=str(vehicle_id))
        return True

    async def get_vehicle_statistics(
        self, vehicle_id: UUID
    ) -> Optional[VehicleStatistics]:
        """
        Get statistics for a vehicle.

        Args:
            vehicle_id: Vehicle ID

        Returns:
            Vehicle statistics if vehicle exists
        """
        # Check if vehicle exists
        vehicle_query = select(Vehicle).where(Vehicle.id == vehicle_id)
        vehicle_result = await self.db.execute(vehicle_query)
        vehicle = vehicle_result.scalar_one_or_none()

        if not vehicle:
            return None

        # Get session count
        session_count_query = select(func.count(DataSession.id)).where(
            DataSession.vehicle_id == vehicle_id
        )
        session_count_result = await self.db.execute(session_count_query)
        total_sessions = session_count_result.scalar() or 0

        # Get telemetry statistics
        stats_query = select(
            func.count(TelemetryData.id).label("total_records"),
            func.sum(TelemetryData.dist_since_clear).label("total_distance"),
            func.sum(TelemetryData.run_time).label("total_runtime"),
            func.avg(TelemetryData.fuel_rate).label("avg_fuel_consumption"),
            func.max(TelemetryData.recorded_at).label("last_activity"),
        ).where(TelemetryData.vehicle_id == vehicle_id)

        stats_result = await self.db.execute(stats_query)
        stats = stats_result.first()

        return VehicleStatistics(
            total_sessions=total_sessions,
            total_records=stats.total_records or 0,
            total_distance=float(stats.total_distance or 0),
            total_runtime_hours=float(stats.total_runtime or 0) / 3600,
            avg_fuel_consumption=float(stats.avg_fuel_consumption or 0),
            last_activity=stats.last_activity,
        )


class SessionService:
    """Service for handling data session operations."""

    def __init__(self, db: AsyncSession):
        self.db = db

    async def create_session(
        self, session_data: DataSessionCreate
    ) -> DataSessionResponse:
        """
        Create a new data session.

        Args:
            session_data: Session creation data

        Returns:
            Created session
        """
        # Verify vehicle exists
        vehicle_query = select(Vehicle).where(Vehicle.id == session_data.vehicle_id)
        vehicle_result = await self.db.execute(vehicle_query)
        vehicle = vehicle_result.scalar_one_or_none()

        if not vehicle:
            raise ValueError(f"Vehicle {session_data.vehicle_id} not found")

        db_session = DataSession(**session_data.model_dump())
        self.db.add(db_session)
        await self.db.commit()
        await self.db.refresh(db_session)

        logger.info("Created session", session_id=str(db_session.id))
        return DataSessionResponse.model_validate(db_session)

    async def get_session(self, session_id: UUID) -> Optional[DataSessionResponse]:
        """
        Get a session by ID.

        Args:
            session_id: Session ID

        Returns:
            Session if found
        """
        query = select(DataSession).where(DataSession.id == session_id)
        result = await self.db.execute(query)
        session = result.scalar_one_or_none()

        if session:
            return DataSessionResponse.model_validate(session)
        return None

    async def get_sessions(
        self, vehicle_id: Optional[UUID] = None, skip: int = 0, limit: int = 100
    ) -> Tuple[List[DataSessionResponse], int]:
        """
        Get sessions with optional vehicle filtering.

        Args:
            vehicle_id: Optional vehicle ID filter
            skip: Number of records to skip
            limit: Maximum number of records

        Returns:
            Tuple of (sessions, total_count)
        """
        query = select(DataSession)
        count_query = select(func.count(DataSession.id))

        if vehicle_id:
            query = query.where(DataSession.vehicle_id == vehicle_id)
            count_query = count_query.where(DataSession.vehicle_id == vehicle_id)

        # Get total count
        count_result = await self.db.execute(count_query)
        total = count_result.scalar() or 0

        # Get sessions
        query = query.order_by(desc(DataSession.start_time)).offset(skip).limit(limit)
        result = await self.db.execute(query)
        sessions = result.scalars().all()

        return [DataSessionResponse.model_validate(s) for s in sessions], total

    async def update_session(
        self, session_id: UUID, session_data: DataSessionUpdate
    ) -> Optional[DataSessionResponse]:
        """
        Update a session.

        Args:
            session_id: Session ID
            session_data: Session update data

        Returns:
            Updated session if found
        """
        query = select(DataSession).where(DataSession.id == session_id)
        result = await self.db.execute(query)
        session = result.scalar_one_or_none()

        if not session:
            return None

        update_data = session_data.model_dump(exclude_unset=True)
        for key, value in update_data.items():
            setattr(session, key, value)

        await self.db.commit()
        await self.db.refresh(session)

        logger.info("Updated session", session_id=str(session_id))
        return DataSessionResponse.model_validate(session)

    async def delete_session(self, session_id: UUID) -> bool:
        """
        Delete a session.

        Args:
            session_id: Session ID

        Returns:
            True if deleted, False if not found
        """
        query = select(DataSession).where(DataSession.id == session_id)
        result = await self.db.execute(query)
        session = result.scalar_one_or_none()

        if not session:
            return False

        await self.db.delete(session)
        await self.db.commit()

        logger.info("Deleted session", session_id=str(session_id))
        return True

    async def get_session_statistics(
        self, session_id: UUID
    ) -> Optional[SessionStatistics]:
        """
        Get statistics for a session.

        Args:
            session_id: Session ID

        Returns:
            Session statistics if session exists
        """
        # Get session info
        session_query = select(DataSession).where(DataSession.id == session_id)
        session_result = await self.db.execute(session_query)
        session = session_result.scalar_one_or_none()

        if not session:
            return None

        # Get telemetry statistics
        stats_query = select(
            func.count(TelemetryData.id).label("total_records"),
            func.avg(TelemetryData.rpm).label("avg_rpm"),
            func.max(TelemetryData.rpm).label("max_rpm"),
            func.avg(TelemetryData.speed).label("avg_speed"),
            func.max(TelemetryData.speed).label("max_speed"),
            func.avg(TelemetryData.coolant_temp).label("avg_coolant_temp"),
            func.max(TelemetryData.coolant_temp).label("max_coolant_temp"),
            func.avg(TelemetryData.fuel_rate).label("avg_fuel_consumption"),
        ).where(TelemetryData.session_id == session_id)

        stats_result = await self.db.execute(stats_query)
        stats = stats_result.first()

        duration_minutes = None
        if session.end_time and session.start_time:
            duration_minutes = (
                session.end_time - session.start_time
            ).total_seconds() / 60

        return SessionStatistics(
            total_records=stats.total_records or 0,
            duration_minutes=duration_minutes,
            avg_rpm=float(stats.avg_rpm or 0),
            max_rpm=float(stats.max_rpm or 0),
            avg_speed=float(stats.avg_speed or 0),
            max_speed=float(stats.max_speed or 0),
            avg_coolant_temp=float(stats.avg_coolant_temp or 0),
            max_coolant_temp=float(stats.max_coolant_temp or 0),
            avg_fuel_consumption=float(stats.avg_fuel_consumption or 0),
        )
