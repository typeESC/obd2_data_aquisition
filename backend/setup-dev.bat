@echo off
REM OBD Data Logger API - Development Setup Script for Windows

echo 🚀 Setting up OBD Data Logger API development environment...

REM Check if Docker is installed
docker --version >nul 2>&1
if errorlevel 1 (
    echo ❌ Docker is not installed. Please install Docker Desktop first.
    pause
    exit /b 1
)

REM Check if Docker Compose is installed
docker-compose --version >nul 2>&1
if errorlevel 1 (
    echo ❌ Docker Compose is not installed. Please install Docker Compose first.
    pause
    exit /b 1
)

REM Create .env file if it doesn't exist
if not exist .env (
    echo 📝 Creating .env file from template...
    copy .env.example .env
    echo ✅ .env file created. Please review and modify the values as needed.
) else (
    echo ✅ .env file already exists.
)

REM Build and start services
echo 🏗️  Building and starting services...
docker-compose up -d --build

REM Wait for services to be ready
echo ⏳ Waiting for services to be ready...
timeout /t 30 /nobreak >nul

REM Check service health
echo 🔍 Checking service health...

REM Check API
curl -f http://localhost:8000/health >nul 2>&1
if errorlevel 1 (
    echo ❌ API is not ready
) else (
    echo ✅ API is ready
)

echo.
echo 🎉 Setup complete!
echo.
echo 📍 Service URLs:
echo    API:         http://localhost:8000
echo    API Docs:    http://localhost:8000/api/v1/docs
echo    Database:    localhost:5432
echo    Redis:       localhost:6379
echo.
echo 🔧 Management commands:
echo    View logs:           docker-compose logs -f
echo    Stop services:       docker-compose down
echo    Restart services:    docker-compose restart
echo    Start pgAdmin:       docker-compose --profile admin up -d pgadmin
echo.
echo 📖 Check the README.md for more information.
pause
