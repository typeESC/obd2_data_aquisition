"""
Basic tests for the OBD Data Logger API.
"""
import pytest
from fastapi.testclient import TestClient
from sqlalchemy.ext.asyncio import AsyncSession, create_async_engine, async_sessionmaker
from sqlalchemy.pool import StaticPool

from app.main import app
from app.core.database import get_db, Base
from app.core.config import settings

# Test database URL
TEST_DATABASE_URL = "sqlite+aiosqlite:///:memory:"

# Create test engine
test_engine = create_async_engine(
    TEST_DATABASE_URL,
    connect_args={"check_same_thread": False},
    poolclass=StaticPool,
)

TestingSessionLocal = async_sessionmaker(
    test_engine, class_=AsyncSession, expire_on_commit=False
)


async def override_get_db():
    """Override database dependency for testing."""
    async with TestingSessionLocal() as session:
        yield session


app.dependency_overrides[get_db] = override_get_db

client = TestClient(app)


@pytest.fixture(scope="session", autouse=True)
async def setup_test_db():
    """Set up test database."""
    async with test_engine.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)
    yield
    async with test_engine.begin() as conn:
        await conn.run_sync(Base.metadata.drop_all)


def test_health_check():
    """Test health check endpoint."""
    response = client.get("/health")
    assert response.status_code == 200
    data = response.json()
    assert data["status"] in ["healthy", "unhealthy"]
    assert "timestamp" in data
    assert "version" in data


def test_root_endpoint():
    """Test root endpoint."""
    response = client.get("/")
    assert response.status_code == 200
    data = response.json()
    assert data["success"] is True
    assert settings.PROJECT_NAME in data["message"]


def test_api_docs():
    """Test API documentation endpoint."""
    response = client.get(f"{settings.API_V1_STR}/docs")
    assert response.status_code == 200


def test_openapi_schema():
    """Test OpenAPI schema endpoint."""
    response = client.get(f"{settings.API_V1_STR}/openapi.json")
    assert response.status_code == 200
    schema = response.json()
    assert "openapi" in schema
    assert "info" in schema
    assert "paths" in schema


def test_create_vehicle():
    """Test vehicle creation."""
    vehicle_data = {
        "vin": "TEST123456789",
        "make": "Test",
        "model": "TestModel",
        "year": 2023,
        "license_plate": "TEST-001"
    }
    
    response = client.post(f"{settings.API_V1_STR}/vehicles", json=vehicle_data)
    assert response.status_code == 201
    data = response.json()
    assert data["vin"] == vehicle_data["vin"]
    assert data["make"] == vehicle_data["make"]
    assert "id" in data


def test_get_vehicles():
    """Test getting vehicles list."""
    response = client.get(f"{settings.API_V1_STR}/vehicles")
    assert response.status_code == 200
    data = response.json()
    assert "items" in data
    assert "total" in data
    assert "page" in data
    assert "size" in data


def test_create_session():
    """Test session creation."""
    # First create a vehicle
    vehicle_data = {
        "vin": "SESSION123456789",
        "make": "Test",
        "model": "TestModel",
        "year": 2023
    }
    
    vehicle_response = client.post(f"{settings.API_V1_STR}/vehicles", json=vehicle_data)
    assert vehicle_response.status_code == 201
    vehicle_id = vehicle_response.json()["id"]
    
    # Create session
    session_data = {
        "vehicle_id": vehicle_id,
        "session_name": "Test Session"
    }
    
    response = client.post(f"{settings.API_V1_STR}/sessions", json=session_data)
    assert response.status_code == 201
    data = response.json()
    assert data["vehicle_id"] == vehicle_id
    assert data["session_name"] == session_data["session_name"]


def test_telemetry_validation():
    """Test telemetry data validation."""
    # Test invalid data
    invalid_data = {
        "session_id": "invalid-uuid",
        "data": {
            "device_timestamp": "not_a_number",
            "rpm": -100  # Invalid RPM
        }
    }
    
    response = client.post(
        f"{settings.API_V1_STR}/telemetry/records",
        params={"session_id": "invalid-uuid"},
        json=invalid_data["data"]
    )
    assert response.status_code in [400, 422]  # Validation error


if __name__ == "__main__":
    pytest.main([__file__])
