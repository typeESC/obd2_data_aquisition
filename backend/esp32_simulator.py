"""
Example script showing how to send telemetry data from ESP32 to the FastAPI backend.

This script simulates the ESP32 device sending OBD-II data to the API.
For actual ESP32 integration, this would be implemented in C using HTTP client libraries.
"""

import json
import time
import random
import requests
from datetime import datetime
from uuid import uuid4

# API Configuration
API_BASE_URL = "http://localhost:8000/api/v1"
VEHICLE_ID = "550e8400-e29b-41d4-a716-446655440000"  # Default vehicle from migration
SESSION_ID = "550e8400-e29b-41d4-a716-446655440001"  # Default session from migration

# ESP32 simulation parameters
BATCH_SIZE = 10  # Number of records to send in each batch
SEND_INTERVAL = 5  # Seconds between batches


class OBDSimulator:
    """Simulates OBD-II data from ESP32 device."""
    
    def __init__(self):
        self.engine_running = False
        self.rpm = 0
        self.speed = 0
        self.coolant_temp = 20
        self.fuel_level = 100
        self.voltage = 12.5
        self.run_time = 0
        
    def start_engine(self):
        """Simulate engine start."""
        self.engine_running = True
        self.rpm = 800 + random.randint(-50, 50)  # Idle RPM
        self.coolant_temp = 20
        print("🚗 Engine started")
        
    def stop_engine(self):
        """Simulate engine stop."""
        self.engine_running = False
        self.rpm = 0
        self.speed = 0
        print("🛑 Engine stopped")
        
    def update_parameters(self):
        """Update OBD parameters based on driving simulation."""
        if not self.engine_running:
            return
            
        # Simulate driving patterns
        if random.random() < 0.3:  # 30% chance of acceleration
            self.rpm = min(6000, self.rpm + random.randint(100, 500))
            self.speed = min(120, self.speed + random.randint(5, 15))
        elif random.random() < 0.2:  # 20% chance of deceleration
            self.rpm = max(800, self.rpm - random.randint(50, 200))
            self.speed = max(0, self.speed - random.randint(5, 10))
        else:  # Maintain current state with small variations
            self.rpm += random.randint(-50, 50)
            self.rpm = max(800, min(6000, self.rpm))
            
        # Update other parameters
        self.coolant_temp = min(110, self.coolant_temp + random.uniform(-1, 2))
        self.fuel_level = max(0, self.fuel_level - random.uniform(0, 0.1))
        self.voltage = 12.5 + random.uniform(-0.5, 0.5)
        self.run_time += 1
        
    def get_telemetry_data(self):
        """Generate telemetry data in API format."""
        timestamp = int(time.time() * 1000)  # ESP32 timestamp in milliseconds
        
        return {
            "device_timestamp": timestamp,
            "rpm": round(self.rpm, 1) if self.engine_running else 0,
            "speed": int(self.speed),
            "coolant_temp": int(self.coolant_temp),
            "engine_load": round(random.uniform(10, 80), 1) if self.engine_running else 0,
            "timing_advance": round(random.uniform(-5, 15), 1) if self.engine_running else 0,
            "intake_air_temp": int(self.coolant_temp - random.randint(5, 15)),
            "maf_rate": round(random.uniform(2, 25), 2) if self.engine_running else 0,
            "throttle_pos": round(random.uniform(0, 75), 1) if self.engine_running else 0,
            "run_time": self.run_time,
            "dist_since_clear": random.randint(0, 1000),
            "fuel_level": round(self.fuel_level, 1),
            "module_voltage": round(self.voltage, 2),
            "commanded_lambda": round(random.uniform(0.95, 1.05), 4) if self.engine_running else 1.0,
            "relative_throttle": round(random.uniform(0, 60), 1) if self.engine_running else 0,
            "ethanol_percentage": round(random.uniform(8, 12), 1),
            "oil_temp": int(self.coolant_temp + random.randint(-5, 20)),
            "fuel_rate": round(random.uniform(5, 15), 2) if self.engine_running else 0
        }


def send_telemetry_batch(data_batch):
    """Send a batch of telemetry data to the API."""
    url = f"{API_BASE_URL}/telemetry/batch"
    
    payload = {
        "session_id": SESSION_ID,
        "vehicle_id": VEHICLE_ID,
        "data": data_batch
    }
    
    try:
        response = requests.post(url, json=payload, timeout=10)
        response.raise_for_status()
        
        result = response.json()
        print(f"✅ Batch sent successfully: {result['processed_records']}/{result['batch_size']} records")
        return True
        
    except requests.exceptions.RequestException as e:
        print(f"❌ Failed to send batch: {e}")
        return False


def send_single_record(data):
    """Send a single telemetry record to the API."""
    url = f"{API_BASE_URL}/telemetry/records"
    
    params = {"session_id": SESSION_ID}
    
    try:
        response = requests.post(url, params=params, json=data, timeout=10)
        response.raise_for_status()
        
        result = response.json()
        print(f"✅ Record sent: RPM={data['rpm']}, Speed={data['speed']} km/h")
        return True
        
    except requests.exceptions.RequestException as e:
        print(f"❌ Failed to send record: {e}")
        return False


def check_api_health():
    """Check if the API is running and healthy."""
    try:
        response = requests.get(f"{API_BASE_URL.replace('/api/v1', '')}/health", timeout=5)
        response.raise_for_status()
        
        health = response.json()
        if health["status"] == "healthy":
            print("✅ API is healthy and ready")
            return True
        else:
            print(f"⚠️  API status: {health['status']}")
            return False
            
    except requests.exceptions.RequestException as e:
        print(f"❌ API health check failed: {e}")
        return False


def main():
    """Main simulation loop."""
    print("🚀 OBD Data Logger ESP32 Simulator")
    print(f"📍 API URL: {API_BASE_URL}")
    print(f"🚗 Vehicle ID: {VEHICLE_ID}")
    print(f"📊 Session ID: {SESSION_ID}")
    print()
    
    # Check API health
    if not check_api_health():
        print("❌ API is not available. Please start the API server first.")
        return
    
    simulator = OBDSimulator()
    
    try:
        print("🔄 Starting simulation...")
        print("Press Ctrl+C to stop")
        print()
        
        # Start engine
        simulator.start_engine()
        
        batch_data = []
        
        while True:
            # Update vehicle parameters
            simulator.update_parameters()
            
            # Get telemetry data
            telemetry = simulator.get_telemetry_data()
            batch_data.append(telemetry)
            
            # Send batch when full
            if len(batch_data) >= BATCH_SIZE:
                print(f"📤 Sending batch of {len(batch_data)} records...")
                success = send_telemetry_batch(batch_data)
                
                if success:
                    batch_data = []  # Clear batch
                else:
                    print("⚠️  Keeping data for retry...")
                
                print()
            
            # Wait before next reading
            time.sleep(SEND_INTERVAL)
            
    except KeyboardInterrupt:
        print("\n🛑 Simulation stopped by user")
        
        # Send remaining data if any
        if batch_data:
            print(f"📤 Sending remaining {len(batch_data)} records...")
            send_telemetry_batch(batch_data)
        
        simulator.stop_engine()
        print("👋 Goodbye!")


if __name__ == "__main__":
    main()
