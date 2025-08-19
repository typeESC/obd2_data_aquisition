"""
Core configuration settings for the OBD Data Logger API.
"""

from functools import lru_cache
from typing import Optional, List

from pydantic import PostgresDsn, validator, Field
from pydantic_settings import BaseSettings


class Settings(BaseSettings):
    """Application settings."""

    # API Settings
    API_V1_STR: str = "/api/v1"
    PROJECT_NAME: str = "OBD Data Logger API"
    VERSION: str = "1.0.0"
    DESCRIPTION: str = "FastAPI backend for OBD-II vehicle data logging and analysis"

    # Security
    SECRET_KEY: str = Field(default="your-super-secret-key-change-this-in-production")
    ACCESS_TOKEN_EXPIRE_MINUTES: int = 60 * 24 * 8  # 8 days
    ALGORITHM: str = "HS256"

    # CORS
    ALLOWED_HOSTS: List[str] = ["*"]
    CORS_ORIGINS: List[str] = [
        "http://localhost:3000",
        "http://localhost:8000",
        "http://localhost:8080",
    ]

    # Database
    POSTGRES_SERVER: str = Field(default="localhost")
    POSTGRES_USER: str = Field(default="obd_user")
    POSTGRES_PASSWORD: str = Field(default="obd_password")
    POSTGRES_DB: str = Field(default="obd_logger")
    POSTGRES_PORT: int = Field(default=5432)
    DATABASE_URL: Optional[PostgresDsn] = None

    @validator("DATABASE_URL", pre=True)
    @classmethod
    def assemble_db_connection(cls, v: Optional[str], values: dict) -> str:
        if isinstance(v, str):
            return v
        return PostgresDsn.build(
            scheme="postgresql+asyncpg",
            username=values.get("POSTGRES_USER"),
            password=values.get("POSTGRES_PASSWORD"),
            host=values.get("POSTGRES_SERVER"),
            port=values.get("POSTGRES_PORT"),
            path=f"/{values.get('POSTGRES_DB') or ''}",
        )

    # Redis (for caching and rate limiting)
    REDIS_URL: str = Field(default="redis://localhost:6379/0")

    # Logging
    LOG_LEVEL: str = Field(default="INFO")
    LOG_FORMAT: str = Field(default="json")

    # Rate Limiting
    RATE_LIMIT_PER_MINUTE: int = Field(default=100)

    # Data Processing
    MAX_BATCH_SIZE: int = Field(default=1000)
    MAX_REQUEST_SIZE: int = Field(default=10 * 1024 * 1024)  # 10MB

    # Monitoring
    ENABLE_METRICS: bool = Field(default=True)
    METRICS_PORT: int = Field(default=8001)

    # Alert Thresholds
    HIGH_RPM_THRESHOLD: float = Field(default=6000.0)
    HIGH_COOLANT_TEMP_THRESHOLD: int = Field(default=105)
    LOW_FUEL_THRESHOLD: float = Field(default=10.0)
    HIGH_OIL_TEMP_THRESHOLD: int = Field(default=140)
    LOW_VOLTAGE_THRESHOLD: float = Field(default=11.5)

    # Data Retention
    DATA_RETENTION_DAYS: int = Field(default=365)
    CLEANUP_INTERVAL_HOURS: int = Field(default=24)

    # Environment
    ENVIRONMENT: str = Field(default="development")
    DEBUG: bool = Field(default=True)

    class Config:
        env_file = ".env"
        case_sensitive = True


@lru_cache()
def get_settings() -> Settings:
    """Get cached settings instance."""
    return Settings()


# Create settings instance
settings = get_settings()
