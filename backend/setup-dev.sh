#!/bin/bash

# OBD Data Logger API - Development Setup Script

set -e

echo "🚀 Setting up OBD Data Logger API development environment..."

# Check if Docker is installed
if ! command -v docker &> /dev/null; then
    echo "❌ Docker is not installed. Please install Docker first."
    exit 1
fi

# Check if Docker Compose is installed
if ! command -v docker-compose &> /dev/null; then
    echo "❌ Docker Compose is not installed. Please install Docker Compose first."
    exit 1
fi

# Create .env file if it doesn't exist
if [ ! -f .env ]; then
    echo "📝 Creating .env file from template..."
    cp .env.example .env
    echo "✅ .env file created. Please review and modify the values as needed."
else
    echo "✅ .env file already exists."
fi

# Build and start services
echo "🏗️  Building and starting services..."
docker-compose up -d --build

# Wait for services to be ready
echo "⏳ Waiting for services to be ready..."
sleep 30

# Check service health
echo "🔍 Checking service health..."

# Check database
if docker-compose exec db pg_isready -U obd_user -d obd_logger; then
    echo "✅ Database is ready"
else
    echo "❌ Database is not ready"
fi

# Check API
if curl -f http://localhost:8000/health &> /dev/null; then
    echo "✅ API is ready"
else
    echo "❌ API is not ready"
fi

# Check Redis
if docker-compose exec redis redis-cli ping | grep -q PONG; then
    echo "✅ Redis is ready"
else
    echo "❌ Redis is not ready"
fi

echo ""
echo "🎉 Setup complete!"
echo ""
echo "📍 Service URLs:"
echo "   API:         http://localhost:8000"
echo "   API Docs:    http://localhost:8000/api/v1/docs"
echo "   Database:    localhost:5432"
echo "   Redis:       localhost:6379"
echo ""
echo "🔧 Management commands:"
echo "   View logs:           docker-compose logs -f"
echo "   Stop services:       docker-compose down"
echo "   Restart services:    docker-compose restart"
echo "   Start pgAdmin:       docker-compose --profile admin up -d pgadmin"
echo ""
echo "📖 Check the README.md for more information."
