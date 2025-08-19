"""
Main FastAPI application for OBD Data Logger API.
"""

from contextlib import asynccontextmanager
from datetime import datetime

from fastapi import FastAPI, Request, status
from fastapi.middleware.cors import CORSMiddleware
from fastapi.middleware.trustedhost import TrustedHostMiddleware
from fastapi.responses import JSONResponse
from fastapi.exceptions import RequestValidationError
from starlette.exceptions import HTTPException as StarletteHTTPException

from app.core.config import settings
from app.core.database import create_tables
from app.core.logging import setup_logging, get_logger
from app.api import telemetry, vehicles
from app.schemas import HealthCheckResponse, APIResponse

# Setup logging
setup_logging()
logger = get_logger(__name__)


@asynccontextmanager
async def lifespan(app: FastAPI):
    """Application lifespan manager."""
    # Startup
    logger.info("Starting OBD Data Logger API", version=settings.VERSION)

    # Create database tables
    try:
        await create_tables()
        logger.info("Database tables created successfully")
    except Exception as e:
        logger.error("Failed to create database tables", error=str(e))
        raise

    yield

    # Shutdown
    logger.info("Shutting down OBD Data Logger API")


# Create FastAPI application
app = FastAPI(
    title=settings.PROJECT_NAME,
    description=settings.DESCRIPTION,
    version=settings.VERSION,
    openapi_url=f"{settings.API_V1_STR}/openapi.json",
    docs_url=f"{settings.API_V1_STR}/docs",
    redoc_url=f"{settings.API_V1_STR}/redoc",
    lifespan=lifespan,
)

# Add middleware
app.add_middleware(
    CORSMiddleware,
    allow_origins=settings.CORS_ORIGINS,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

app.add_middleware(TrustedHostMiddleware, allowed_hosts=settings.ALLOWED_HOSTS)


# Exception handlers
@app.exception_handler(StarletteHTTPException)
async def http_exception_handler(request: Request, exc: StarletteHTTPException):
    """Handle HTTP exceptions."""
    logger.warning(
        "HTTP exception",
        status_code=exc.status_code,
        detail=exc.detail,
        path=request.url.path,
    )
    return JSONResponse(
        status_code=exc.status_code,
        content=APIResponse(
            success=False, message=exc.detail or "HTTP error occurred"
        ).model_dump(),
    )


@app.exception_handler(RequestValidationError)
async def validation_exception_handler(request: Request, exc: RequestValidationError):
    """Handle validation errors."""
    logger.warning("Validation error", errors=exc.errors(), path=request.url.path)
    return JSONResponse(
        status_code=status.HTTP_422_UNPROCESSABLE_ENTITY,
        content=APIResponse(
            success=False, message="Validation error", data={"errors": exc.errors()}
        ).model_dump(),
    )


@app.exception_handler(Exception)
async def general_exception_handler(request: Request, exc: Exception):
    """Handle general exceptions."""
    logger.error(
        "Unhandled exception", error=str(exc), path=request.url.path, exc_info=True
    )
    return JSONResponse(
        status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
        content=APIResponse(
            success=False, message="Internal server error"
        ).model_dump(),
    )


# Health check endpoint
@app.get(
    "/health",
    response_model=HealthCheckResponse,
    summary="Health check",
    description="Check API health status and dependencies",
)
async def health_check():
    """Health check endpoint."""
    try:
        # Test database connection
        from app.core.database import engine
        from sqlalchemy import text

        async with engine.begin() as conn:
            await conn.execute(text("SELECT 1"))

        db_status = "connected"
    except Exception as e:
        logger.error("Database health check failed", error=str(e))
        db_status = "disconnected"

    return HealthCheckResponse(
        status="healthy" if db_status == "connected" else "unhealthy",
        timestamp=datetime.utcnow(),
        version=settings.VERSION,
        database=db_status,
        dependencies={"database": db_status, "environment": settings.ENVIRONMENT},
    )


# Root endpoint
@app.get(
    "/",
    response_model=APIResponse,
    summary="API root",
    description="API root endpoint with basic information",
)
async def root():
    """Root endpoint."""
    return APIResponse(
        message=f"Welcome to {settings.PROJECT_NAME} v{settings.VERSION}",
        data={
            "docs_url": f"{settings.API_V1_STR}/docs",
            "redoc_url": f"{settings.API_V1_STR}/redoc",
            "health_url": "/health",
        },
    )


# Include API routers
app.include_router(
    telemetry.router, prefix=f"{settings.API_V1_STR}/telemetry", tags=["Telemetry Data"]
)

app.include_router(
    vehicles.router, prefix=f"{settings.API_V1_STR}", tags=["Vehicles & Sessions"]
)


# Middleware for request logging
@app.middleware("http")
async def log_requests(request: Request, call_next):
    """Log all requests."""
    start_time = datetime.utcnow()

    # Process request
    response = await call_next(request)

    # Log request details
    process_time = (datetime.utcnow() - start_time).total_seconds()
    logger.info(
        "Request processed",
        method=request.method,
        path=request.url.path,
        status_code=response.status_code,
        process_time=process_time,
    )

    return response


if __name__ == "__main__":
    import uvicorn

    uvicorn.run(
        "app.main:app",
        host="0.0.0.0",
        port=8000,
        reload=settings.DEBUG,
        log_level=(
            settings.LOG_LEVEL.lower()
            if hasattr(settings.LOG_LEVEL, "lower")
            else str(settings.LOG_LEVEL).lower()
        ),
    )
