"""
API endpoints for telemetry data operations.
"""

from datetime import datetime
from typing import Optional
from uuid import UUID

from fastapi import APIRouter, Depends, HTTPException, Query, status, UploadFile, File
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.database import get_db
from app.services.telemetry import TelemetryService
from app.schemas import (
    TelemetryDataCreate,
    TelemetryDataBatchCreate,
    TelemetryDataResponse,
    DataBatchResponse,
    PaginatedResponse,
    APIResponse,
    CSVImportRequest,
    CSVImportResponse,
)
from app.core.logging import get_logger

logger = get_logger(__name__)
router = APIRouter()


@router.post(
    "/records",
    response_model=TelemetryDataResponse,
    status_code=status.HTTP_201_CREATED,
    summary="Create single telemetry record",
    description="Create a single telemetry data record for a specific session",
)
async def create_telemetry_record(
    session_id: UUID,
    data: TelemetryDataCreate,
    vehicle_id: Optional[UUID] = None,
    db: AsyncSession = Depends(get_db),
):
    """Create a single telemetry data record."""
    try:
        service = TelemetryService(db)
        return await service.create_single_record(session_id, data, vehicle_id)
    except ValueError as e:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail=str(e))
    except Exception as e:
        logger.error("Failed to create telemetry record", error=str(e))
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail="Failed to create telemetry record",
        )


@router.post(
    "/batch",
    response_model=DataBatchResponse,
    status_code=status.HTTP_201_CREATED,
    summary="Create batch of telemetry records",
    description="Create multiple telemetry data records in a single batch operation",
)
async def create_telemetry_batch(
    batch_data: TelemetryDataBatchCreate, db: AsyncSession = Depends(get_db)
):
    """Create a batch of telemetry data records."""
    try:
        service = TelemetryService(db)
        return await service.create_batch(batch_data)
    except ValueError as e:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail=str(e))
    except Exception as e:
        logger.error("Failed to create telemetry batch", error=str(e))
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail="Failed to create telemetry batch",
        )


@router.get(
    "/records",
    response_model=PaginatedResponse,
    summary="Get telemetry records",
    description="Retrieve telemetry data records with optional filtering and pagination",
)
async def get_telemetry_records(
    session_id: Optional[UUID] = Query(None, description="Filter by session ID"),
    vehicle_id: Optional[UUID] = Query(None, description="Filter by vehicle ID"),
    start_time: Optional[datetime] = Query(
        None, description="Filter by start time (ISO format)"
    ),
    end_time: Optional[datetime] = Query(
        None, description="Filter by end time (ISO format)"
    ),
    page: int = Query(1, ge=1, description="Page number"),
    size: int = Query(100, ge=1, le=1000, description="Page size"),
    db: AsyncSession = Depends(get_db),
):
    """Get telemetry data records with filtering and pagination."""
    try:
        service = TelemetryService(db)
        offset = (page - 1) * size

        records, total = await service.get_records(
            session_id=session_id,
            vehicle_id=vehicle_id,
            start_time=start_time,
            end_time=end_time,
            limit=size,
            offset=offset,
        )

        total_pages = (total + size - 1) // size

        return PaginatedResponse(
            items=records, total=total, page=page, size=size, pages=total_pages
        )
    except Exception as e:
        logger.error("Failed to get telemetry records", error=str(e))
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail="Failed to retrieve telemetry records",
        )


@router.post(
    "/import/csv",
    response_model=CSVImportResponse,
    status_code=status.HTTP_201_CREATED,
    summary="Import telemetry data from CSV",
    description="Import telemetry data from a CSV file",
)
async def import_csv_data(
    import_request: CSVImportRequest, db: AsyncSession = Depends(get_db)
):
    """Import telemetry data from CSV."""
    try:
        service = TelemetryService(db)
        return await service.import_csv(import_request)
    except ValueError as e:
        raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail=str(e))
    except Exception as e:
        logger.error("Failed to import CSV data", error=str(e))
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail="Failed to import CSV data",
        )


@router.post(
    "/upload/csv",
    response_model=CSVImportResponse,
    status_code=status.HTTP_201_CREATED,
    summary="Upload and import CSV file",
    description="Upload a CSV file and import telemetry data",
)
async def upload_csv_file(
    session_id: UUID,
    file: UploadFile = File(..., description="CSV file to upload"),
    delimiter: str = Query(",", max_length=1, description="CSV delimiter"),
    skip_header: bool = Query(
        True, description="Skip first row if it contains headers"
    ),
    db: AsyncSession = Depends(get_db),
):
    """Upload and import CSV file."""
    try:
        # Validate file type
        if not file.filename.lower().endswith(".csv"):
            raise HTTPException(
                status_code=status.HTTP_400_BAD_REQUEST,
                detail="Only CSV files are allowed",
            )

        # Read file content
        content = await file.read()

        # Create import request
        import_request = CSVImportRequest(
            session_id=session_id,
            file_content=content.decode("utf-8"),
            delimiter=delimiter,
            skip_header=skip_header,
        )

        service = TelemetryService(db)
        return await service.import_csv(import_request)

    except UnicodeDecodeError:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="Invalid file encoding. Please ensure the file is UTF-8 encoded.",
        )
    except ValueError as e:
        raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail=str(e))
    except Exception as e:
        logger.error("Failed to upload CSV file", error=str(e))
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail="Failed to upload CSV file",
        )


@router.get(
    "/records/{record_id}",
    response_model=TelemetryDataResponse,
    summary="Get single telemetry record",
    description="Retrieve a single telemetry data record by ID",
)
async def get_telemetry_record(record_id: UUID, db: AsyncSession = Depends(get_db)):
    """Get a single telemetry data record by ID."""
    try:
        # This would need to be implemented in the service
        # For now, raising not implemented
        raise HTTPException(
            status_code=status.HTTP_501_NOT_IMPLEMENTED,
            detail="Single record retrieval not yet implemented",
        )
    except Exception as e:
        logger.error(
            "Failed to get telemetry record", record_id=str(record_id), error=str(e)
        )
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail="Failed to retrieve telemetry record",
        )


@router.delete(
    "/records/{record_id}",
    response_model=APIResponse,
    summary="Delete telemetry record",
    description="Delete a single telemetry data record",
)
async def delete_telemetry_record(record_id: UUID, db: AsyncSession = Depends(get_db)):
    """Delete a telemetry data record."""
    try:
        # This would need to be implemented in the service
        # For now, raising not implemented
        raise HTTPException(
            status_code=status.HTTP_501_NOT_IMPLEMENTED,
            detail="Record deletion not yet implemented",
        )
    except Exception as e:
        logger.error(
            "Failed to delete telemetry record", record_id=str(record_id), error=str(e)
        )
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail="Failed to delete telemetry record",
        )
