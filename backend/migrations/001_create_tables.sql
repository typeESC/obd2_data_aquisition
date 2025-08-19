-- OBD Data Logger Database Schema
-- This script creates the database structure for storing vehicle telemetry data

-- Enable UUID extension
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";

-- Create vehicles table to store basic vehicle information
CREATE TABLE IF NOT EXISTS vehicles (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    vin VARCHAR(17) UNIQUE,
    make VARCHAR(50),
    model VARCHAR(50),
    year INTEGER,
    license_plate VARCHAR(20),
    created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT NOW()
);

-- Create sessions table to group related telemetry data
CREATE TABLE IF NOT EXISTS data_sessions (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    vehicle_id UUID REFERENCES vehicles(id) ON DELETE CASCADE,
    session_name VARCHAR(100),
    start_time TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    end_time TIMESTAMP WITH TIME ZONE,
    total_records INTEGER DEFAULT 0,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT NOW()
);

-- Create main telemetry data table
CREATE TABLE IF NOT EXISTS telemetry_data (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    session_id UUID REFERENCES data_sessions(id) ON DELETE CASCADE,
    vehicle_id UUID REFERENCES vehicles(id) ON DELETE CASCADE,
    
    -- Timestamp from the device
    device_timestamp BIGINT NOT NULL, -- Original timestamp from ESP32
    recorded_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(), -- Server timestamp
    
    -- Engine parameters
    rpm NUMERIC(6,2), -- Engine RPM
    engine_load NUMERIC(5,2), -- Engine load percentage (0-100)
    coolant_temp INTEGER, -- Coolant temperature in Celsius
    oil_temp INTEGER, -- Oil temperature in Celsius
    run_time INTEGER, -- Engine runtime in seconds
    
    -- Vehicle movement
    speed INTEGER, -- Vehicle speed in km/h
    throttle_pos NUMERIC(5,2), -- Throttle position percentage (0-100)
    relative_throttle NUMERIC(5,2), -- Relative throttle position percentage
    
    -- Air and fuel system
    timing_advance NUMERIC(5,2), -- Timing advance in degrees
    intake_air_temp INTEGER, -- Intake air temperature in Celsius
    maf_rate NUMERIC(7,2), -- Mass airflow rate in g/s
    fuel_level NUMERIC(5,2), -- Fuel level percentage (0-100)
    fuel_rate NUMERIC(7,2), -- Fuel consumption rate in L/h
    ethanol_percentage NUMERIC(5,2), -- Ethanol fuel percentage
    commanded_lambda NUMERIC(8,4), -- Commanded equivalence ratio
    
    -- Electrical system
    module_voltage NUMERIC(4,2), -- Control module voltage in volts
    
    -- Diagnostic
    dist_since_clear INTEGER, -- Distance traveled since codes cleared in km
    
    -- Data quality indicators
    data_quality_score NUMERIC(3,2) DEFAULT 1.0, -- Score from 0 to 1
    missing_fields JSONB, -- Array of fields that were missing in the original data
    
    -- Indexes for performance
    created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT NOW()
);

-- Create batch processing table for handling data imports
CREATE TABLE IF NOT EXISTS data_batches (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    session_id UUID REFERENCES data_sessions(id) ON DELETE CASCADE,
    batch_size INTEGER NOT NULL,
    processed_records INTEGER DEFAULT 0,
    failed_records INTEGER DEFAULT 0,
    status VARCHAR(20) DEFAULT 'processing', -- processing, completed, failed
    error_message TEXT,
    started_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    completed_at TIMESTAMP WITH TIME ZONE,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW()
);

-- Create vehicle alerts table for threshold monitoring
CREATE TABLE IF NOT EXISTS vehicle_alerts (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    vehicle_id UUID REFERENCES vehicles(id) ON DELETE CASCADE,
    session_id UUID REFERENCES data_sessions(id) ON DELETE CASCADE,
    alert_type VARCHAR(50) NOT NULL, -- 'high_temp', 'low_fuel', 'high_rpm', etc.
    severity VARCHAR(20) NOT NULL, -- 'low', 'medium', 'high', 'critical'
    message TEXT NOT NULL,
    parameter_name VARCHAR(50),
    parameter_value NUMERIC,
    threshold_value NUMERIC,
    triggered_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    acknowledged BOOLEAN DEFAULT FALSE,
    acknowledged_at TIMESTAMP WITH TIME ZONE,
    acknowledged_by VARCHAR(100),
    created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW()
);

-- Create indexes for better performance
CREATE INDEX IF NOT EXISTS idx_telemetry_session_id ON telemetry_data(session_id);
CREATE INDEX IF NOT EXISTS idx_telemetry_vehicle_id ON telemetry_data(vehicle_id);
CREATE INDEX IF NOT EXISTS idx_telemetry_recorded_at ON telemetry_data(recorded_at);
CREATE INDEX IF NOT EXISTS idx_telemetry_device_timestamp ON telemetry_data(device_timestamp);
CREATE INDEX IF NOT EXISTS idx_telemetry_rpm ON telemetry_data(rpm);
CREATE INDEX IF NOT EXISTS idx_sessions_vehicle_id ON data_sessions(vehicle_id);
CREATE INDEX IF NOT EXISTS idx_sessions_start_time ON data_sessions(start_time);
CREATE INDEX IF NOT EXISTS idx_alerts_vehicle_id ON vehicle_alerts(vehicle_id);
CREATE INDEX IF NOT EXISTS idx_alerts_triggered_at ON vehicle_alerts(triggered_at);
CREATE INDEX IF NOT EXISTS idx_alerts_acknowledged ON vehicle_alerts(acknowledged);

-- Create composite indexes for common queries
CREATE INDEX IF NOT EXISTS idx_telemetry_session_time ON telemetry_data(session_id, recorded_at);
CREATE INDEX IF NOT EXISTS idx_telemetry_vehicle_time ON telemetry_data(vehicle_id, recorded_at);

-- Create function to update updated_at timestamp
CREATE OR REPLACE FUNCTION update_updated_at_column()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = NOW();
    RETURN NEW;
END;
$$ language 'plpgsql';

-- Create triggers for updating timestamps
CREATE TRIGGER update_vehicles_updated_at BEFORE UPDATE ON vehicles
    FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

CREATE TRIGGER update_sessions_updated_at BEFORE UPDATE ON data_sessions
    FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

CREATE TRIGGER update_telemetry_updated_at BEFORE UPDATE ON telemetry_data
    FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

-- Create function to automatically update session statistics
CREATE OR REPLACE FUNCTION update_session_stats()
RETURNS TRIGGER AS $$
BEGIN
    IF TG_OP = 'INSERT' THEN
        UPDATE data_sessions 
        SET total_records = total_records + 1,
            end_time = NEW.recorded_at
        WHERE id = NEW.session_id;
        RETURN NEW;
    ELSIF TG_OP = 'DELETE' THEN
        UPDATE data_sessions 
        SET total_records = total_records - 1
        WHERE id = OLD.session_id;
        RETURN OLD;
    END IF;
    RETURN NULL;
END;
$$ language 'plpgsql';

-- Create trigger for session statistics
CREATE TRIGGER update_session_stats_trigger
    AFTER INSERT OR DELETE ON telemetry_data
    FOR EACH ROW EXECUTE FUNCTION update_session_stats();

-- Create view for latest telemetry data per vehicle
CREATE OR REPLACE VIEW latest_telemetry AS
SELECT DISTINCT ON (vehicle_id) 
    vehicle_id,
    session_id,
    device_timestamp,
    recorded_at,
    rpm,
    speed,
    coolant_temp,
    engine_load,
    fuel_level,
    module_voltage,
    throttle_pos
FROM telemetry_data
ORDER BY vehicle_id, recorded_at DESC;

-- Create view for session summaries
CREATE OR REPLACE VIEW session_summaries AS
SELECT 
    s.id,
    s.vehicle_id,
    s.session_name,
    s.start_time,
    s.end_time,
    s.total_records,
    EXTRACT(EPOCH FROM (s.end_time - s.start_time))/60 as duration_minutes,
    COALESCE(AVG(t.rpm), 0) as avg_rpm,
    COALESCE(MAX(t.rpm), 0) as max_rpm,
    COALESCE(AVG(t.speed), 0) as avg_speed,
    COALESCE(MAX(t.speed), 0) as max_speed,
    COALESCE(AVG(t.coolant_temp), 0) as avg_coolant_temp,
    COALESCE(MAX(t.coolant_temp), 0) as max_coolant_temp,
    COALESCE(AVG(t.fuel_rate), 0) as avg_fuel_consumption
FROM data_sessions s
LEFT JOIN telemetry_data t ON s.id = t.session_id
GROUP BY s.id, s.vehicle_id, s.session_name, s.start_time, s.end_time, s.total_records;

-- Insert default data
-- Create a default vehicle for testing
INSERT INTO vehicles (id, vin, make, model, year, license_plate) 
VALUES ('550e8400-e29b-41d4-a716-446655440000', 'DEFAULT_VIN_12345', 'Unknown', 'Unknown', 2023, 'DEV-0001')
ON CONFLICT (vin) DO NOTHING;

-- Create default session
INSERT INTO data_sessions (id, vehicle_id, session_name) 
VALUES ('550e8400-e29b-41d4-a716-446655440001', '550e8400-e29b-41d4-a716-446655440000', 'Default Session')
ON CONFLICT (id) DO NOTHING;

-- Add comments for documentation
COMMENT ON TABLE vehicles IS 'Stores basic vehicle information and metadata';
COMMENT ON TABLE data_sessions IS 'Groups telemetry data into logical sessions or trips';
COMMENT ON TABLE telemetry_data IS 'Main table storing all OBD-II telemetry data from vehicles';
COMMENT ON TABLE data_batches IS 'Tracks batch processing operations for data imports';
COMMENT ON TABLE vehicle_alerts IS 'Stores alerts and notifications based on telemetry thresholds';

COMMENT ON COLUMN telemetry_data.device_timestamp IS 'Original timestamp from the ESP32 device in milliseconds';
COMMENT ON COLUMN telemetry_data.recorded_at IS 'Server timestamp when the data was received';
COMMENT ON COLUMN telemetry_data.data_quality_score IS 'Quality score from 0-1 based on data completeness and validity';
COMMENT ON COLUMN telemetry_data.missing_fields IS 'JSON array of field names that were missing in the original data';
