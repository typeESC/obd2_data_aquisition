"""
API endpoints for vehicle and session operations.
"""

from typing import Optional
from uuid import UUID

from fastapi import APIRouter, Depends, HTTPException, Query, status
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.database import get_db
from app.services.vehicle import VehicleService, SessionService
from app.schemas import (
    VehicleCreate,
    VehicleUpdate,
    VehicleResponse,
    DataSessionCreate,
    DataSessionUpdate,
    DataSessionResponse,
    PaginatedResponse,
    APIResponse,
    SessionStatistics,
    VehicleStatistics,
)
from app.core.logging import get_logger

logger = get_logger(__name__)
router = APIRouter()


# Vehicle endpoints
@router.post(
    "/vehicles",
    response_model=VehicleResponse,
    status_code=status.HTTP_201_CREATED,
    summary="Create vehicle",
    description="Create a new vehicle record",
)
async def create_vehicle(
    vehicle_data: VehicleCreate, db: AsyncSession = Depends(get_db)
):
    """Create a new vehicle."""
    try:
        service = VehicleService(db)
        return await service.create_vehicle(vehicle_data)
    except Exception as e:
        logger.error("Failed to create vehicle", error=str(e))
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail="Failed to create vehicle",
        )


@router.get(
    "/vehicles",
    response_model=PaginatedResponse,
    summary="Get vehicles",
    description="Retrieve all vehicles with pagination",
)
async def get_vehicles(
    page: int = Query(1, ge=1, description="Page number"),
    size: int = Query(100, ge=1, le=1000, description="Page size"),
    db: AsyncSession = Depends(get_db),
):
    """Get all vehicles with pagination."""
    try:
        service = VehicleService(db)
        skip = (page - 1) * size

        vehicles, total = await service.get_vehicles(skip=skip, limit=size)
        total_pages = (total + size - 1) // size

        return PaginatedResponse(
            items=vehicles, total=total, page=page, size=size, pages=total_pages
        )
    except Exception as e:
        logger.error("Failed to get vehicles", error=str(e))
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail="Failed to retrieve vehicles",
        )


@router.get(
    "/vehicles/{vehicle_id}",
    response_model=VehicleResponse,
    summary="Get vehicle",
    description="Retrieve a specific vehicle by ID",
)
async def get_vehicle(vehicle_id: UUID, db: AsyncSession = Depends(get_db)):
    """Get a vehicle by ID."""
    try:
        service = VehicleService(db)
        vehicle = await service.get_vehicle(vehicle_id)

        if not vehicle:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND, detail="Vehicle not found"
            )

        return vehicle
    except HTTPException:
        raise
    except Exception as e:
        logger.error("Failed to get vehicle", vehicle_id=str(vehicle_id), error=str(e))
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail="Failed to retrieve vehicle",
        )


@router.put(
    "/vehicles/{vehicle_id}",
    response_model=VehicleResponse,
    summary="Update vehicle",
    description="Update a specific vehicle",
)
async def update_vehicle(
    vehicle_id: UUID, vehicle_data: VehicleUpdate, db: AsyncSession = Depends(get_db)
):
    """Update a vehicle."""
    try:
        service = VehicleService(db)
        vehicle = await service.update_vehicle(vehicle_id, vehicle_data)

        if not vehicle:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND, detail="Vehicle not found"
            )

        return vehicle
    except HTTPException:
        raise
    except Exception as e:
        logger.error(
            "Failed to update vehicle", vehicle_id=str(vehicle_id), error=str(e)
        )
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail="Failed to update vehicle",
        )


@router.delete(
    "/vehicles/{vehicle_id}",
    response_model=APIResponse,
    summary="Delete vehicle",
    description="Delete a specific vehicle and all associated data",
)
async def delete_vehicle(vehicle_id: UUID, db: AsyncSession = Depends(get_db)):
    """Delete a vehicle."""
    try:
        service = VehicleService(db)
        deleted = await service.delete_vehicle(vehicle_id)

        if not deleted:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND, detail="Vehicle not found"
            )

        return APIResponse(message="Vehicle deleted successfully")
    except HTTPException:
        raise
    except Exception as e:
        logger.error(
            "Failed to delete vehicle", vehicle_id=str(vehicle_id), error=str(e)
        )
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail="Failed to delete vehicle",
        )


@router.get(
    "/vehicles/{vehicle_id}/statistics",
    response_model=VehicleStatistics,
    summary="Get vehicle statistics",
    description="Get comprehensive statistics for a specific vehicle",
)
async def get_vehicle_statistics(vehicle_id: UUID, db: AsyncSession = Depends(get_db)):
    """Get vehicle statistics."""
    try:
        service = VehicleService(db)
        stats = await service.get_vehicle_statistics(vehicle_id)

        if not stats:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND, detail="Vehicle not found"
            )

        return stats
    except HTTPException:
        raise
    except Exception as e:
        logger.error(
            "Failed to get vehicle statistics", vehicle_id=str(vehicle_id), error=str(e)
        )
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail="Failed to retrieve vehicle statistics",
        )


# Session endpoints
@router.post(
    "/sessions",
    response_model=DataSessionResponse,
    status_code=status.HTTP_201_CREATED,
    summary="Create session",
    description="Create a new data session for a vehicle",
)
async def create_session(
    session_data: DataSessionCreate, db: AsyncSession = Depends(get_db)
):
    """Create a new data session."""
    try:
        service = SessionService(db)
        return await service.create_session(session_data)
    except ValueError as e:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail=str(e))
    except Exception as e:
        logger.error("Failed to create session", error=str(e))
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail="Failed to create session",
        )


@router.get(
    "/sessions",
    response_model=PaginatedResponse,
    summary="Get sessions",
    description="Retrieve data sessions with optional vehicle filtering",
)
async def get_sessions(
    vehicle_id: Optional[UUID] = Query(None, description="Filter by vehicle ID"),
    page: int = Query(1, ge=1, description="Page number"),
    size: int = Query(100, ge=1, le=1000, description="Page size"),
    db: AsyncSession = Depends(get_db),
):
    """Get data sessions with optional filtering."""
    try:
        service = SessionService(db)
        skip = (page - 1) * size

        sessions, total = await service.get_sessions(
            vehicle_id=vehicle_id, skip=skip, limit=size
        )

        total_pages = (total + size - 1) // size

        return PaginatedResponse(
            items=sessions, total=total, page=page, size=size, pages=total_pages
        )
    except Exception as e:
        logger.error("Failed to get sessions", error=str(e))
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail="Failed to retrieve sessions",
        )


@router.get(
    "/sessions/{session_id}",
    response_model=DataSessionResponse,
    summary="Get session",
    description="Retrieve a specific data session by ID",
)
async def get_session(session_id: UUID, db: AsyncSession = Depends(get_db)):
    """Get a session by ID."""
    try:
        service = SessionService(db)
        session = await service.get_session(session_id)

        if not session:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND, detail="Session not found"
            )

        return session
    except HTTPException:
        raise
    except Exception as e:
        logger.error("Failed to get session", session_id=str(session_id), error=str(e))
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail="Failed to retrieve session",
        )


@router.put(
    "/sessions/{session_id}",
    response_model=DataSessionResponse,
    summary="Update session",
    description="Update a specific data session",
)
async def update_session(
    session_id: UUID,
    session_data: DataSessionUpdate,
    db: AsyncSession = Depends(get_db),
):
    """Update a session."""
    try:
        service = SessionService(db)
        session = await service.update_session(session_id, session_data)

        if not session:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND, detail="Session not found"
            )

        return session
    except HTTPException:
        raise
    except Exception as e:
        logger.error(
            "Failed to update session", session_id=str(session_id), error=str(e)
        )
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail="Failed to update session",
        )


@router.delete(
    "/sessions/{session_id}",
    response_model=APIResponse,
    summary="Delete session",
    description="Delete a specific data session and all associated telemetry data",
)
async def delete_session(session_id: UUID, db: AsyncSession = Depends(get_db)):
    """Delete a session."""
    try:
        service = SessionService(db)
        deleted = await service.delete_session(session_id)

        if not deleted:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND, detail="Session not found"
            )

        return APIResponse(message="Session deleted successfully")
    except HTTPException:
        raise
    except Exception as e:
        logger.error(
            "Failed to delete session", session_id=str(session_id), error=str(e)
        )
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail="Failed to delete session",
        )


@router.get(
    "/sessions/{session_id}/statistics",
    response_model=SessionStatistics,
    summary="Get session statistics",
    description="Get comprehensive statistics for a specific session",
)
async def get_session_statistics(session_id: UUID, db: AsyncSession = Depends(get_db)):
    """Get session statistics."""
    try:
        service = SessionService(db)
        stats = await service.get_session_statistics(session_id)

        if not stats:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND, detail="Session not found"
            )

        return stats
    except HTTPException:
        raise
    except Exception as e:
        logger.error(
            "Failed to get session statistics", session_id=str(session_id), error=str(e)
        )
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail="Failed to retrieve session statistics",
        )
