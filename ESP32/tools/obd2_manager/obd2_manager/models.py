"""
Data models for OBD2 Manager
"""

from dataclasses import dataclass, field
from typing import Dict, List, Optional
from enum import IntEnum


class SystemState(IntEnum):
    """ESP32 system states"""
    INIT = 0
    IGNITION_OFF = 1
    IGNITION_ON = 2
    LOGGING = 3
    ERROR = 4
    SNIFF_ACTIVE = 5
    SNIFF_STORAGE_LOW = 6
    HYBRID_MODE = 7


class PollTier(IntEnum):
    """PID polling tiers"""
    CRITICAL = 0  # 100ms
    HIGH = 1      # 500ms
    MEDIUM = 2    # 2000ms
    LOW = 3       # 15000ms


# Tier intervals in milliseconds
TIER_INTERVALS = {
    PollTier.CRITICAL: 100,
    PollTier.HIGH: 500,
    PollTier.MEDIUM: 2000,
    PollTier.LOW: 15000,
}


@dataclass
class Telemetry:
    """Extended telemetry data from ESP32"""
    timestamp_us: int = 0
    
    # Tier 1 - Critical
    rpm: float = 0.0
    speed: int = 0
    throttle: float = 0.0
    load: float = 0.0
    maf: float = 0.0
    
    # Tier 2 - High
    coolant: int = 0
    manifold: int = 0
    fuel_level: float = 0.0
    intake_temp: int = 0
    runtime: int = 0
    
    # Tier 3 - Medium
    voltage: float = 0.0
    oil_temp: int = 0
    fuel_trim_short: float = 0.0
    fuel_trim_long: float = 0.0
    distance: int = 0
    ambient: int = 0
    timing: float = 0.0
    
    # Tier 4 - Low
    mil_status: int = 0
    dtc_count: int = 0
    pending_dtc: int = 0
    o2_b1s1: float = 0.0
    o2_b1s2: float = 0.0
    
    valid_mask: int = 0
    
    @classmethod
    def from_json(cls, data: dict) -> 'Telemetry':
        """Create from API JSON response"""
        t = cls()
        t.timestamp_us = data.get('timestamp_us', 0)
        
        tier1 = data.get('tier1', {})
        t.rpm = tier1.get('rpm', 0.0)
        t.speed = tier1.get('speed', 0)
        t.throttle = tier1.get('throttle', 0.0)
        t.load = tier1.get('load', 0.0)
        t.maf = tier1.get('maf', 0.0)
        
        tier2 = data.get('tier2', {})
        t.coolant = tier2.get('coolant', 0)
        t.manifold = tier2.get('manifold', 0)
        t.fuel_level = tier2.get('fuel_level', 0.0)
        t.intake_temp = tier2.get('intake_temp', 0)
        t.runtime = tier2.get('runtime', 0)
        
        tier3 = data.get('tier3', {})
        t.voltage = tier3.get('voltage', 0.0)
        t.oil_temp = tier3.get('oil_temp', 0)
        t.fuel_trim_short = tier3.get('fuel_trim_short', 0.0)
        t.fuel_trim_long = tier3.get('fuel_trim_long', 0.0)
        t.distance = tier3.get('distance', 0)
        t.ambient = tier3.get('ambient', 0)
        t.timing = tier3.get('timing', 0.0)
        
        tier4 = data.get('tier4', {})
        t.mil_status = tier4.get('mil_status', 0)
        t.dtc_count = tier4.get('dtc_count', 0)
        t.pending_dtc = tier4.get('pending_dtc', 0)
        t.o2_b1s1 = tier4.get('o2_b1s1', 0.0)
        t.o2_b1s2 = tier4.get('o2_b1s2', 0.0)
        
        t.valid_mask = data.get('valid_mask', 0)
        return t


@dataclass
class SystemStatus:
    """ESP32 system status"""
    state: str = "UNKNOWN"
    state_code: int = 0
    records: int = 0
    heap_free: int = 0
    storage_total: int = 0
    storage_used: int = 0
    storage_free: int = 0
    sniff_state: str = "IDLE"
    uptime_ms: int = 0
    
    @classmethod
    def from_json(cls, data: dict) -> 'SystemStatus':
        return cls(
            state=data.get('state', 'UNKNOWN'),
            state_code=data.get('state_code', 0),
            records=data.get('records', 0),
            heap_free=data.get('heap_free', 0),
            storage_total=data.get('storage_total', 0),
            storage_used=data.get('storage_used', 0),
            storage_free=data.get('storage_free', 0),
            sniff_state=data.get('sniff_state', 'IDLE'),
            uptime_ms=data.get('uptime_ms', 0),
        )
    
    @property
    def storage_percent(self) -> float:
        if self.storage_total == 0:
            return 0.0
        return (self.storage_used / self.storage_total) * 100


@dataclass
class SnifferStats:
    """CAN sniffer statistics"""
    state: str = "IDLE"
    can_mode: str = "OBD_ONLY"
    messages_captured: int = 0
    messages_logged: int = 0
    unique_ids: int = 0
    bytes_written: int = 0
    buffer_overflows: int = 0
    storage_free_kb: float = 0.0
    storage_percent_used: float = 0.0
    current_file: str = ""
    
    @classmethod
    def from_json(cls, data: dict) -> 'SnifferStats':
        return cls(
            state=data.get('state', 'IDLE'),
            can_mode=data.get('can_mode', 'OBD_ONLY'),
            messages_captured=data.get('messages_captured', 0),
            messages_logged=data.get('messages_logged', 0),
            unique_ids=data.get('unique_ids', 0),
            bytes_written=data.get('bytes_written', 0),
            buffer_overflows=data.get('buffer_overflows', 0),
            storage_free_kb=data.get('storage_free_kb', 0.0),
            storage_percent_used=data.get('storage_percent_used', 0.0),
            current_file=data.get('current_file', ''),
        )


@dataclass
class DTCData:
    """Diagnostic Trouble Codes"""
    mil_on: bool = False
    confirmed_count: int = 0
    pending_count: int = 0
    confirmed: List[str] = field(default_factory=list)
    pending: List[str] = field(default_factory=list)
    
    @classmethod
    def from_json(cls, data: dict) -> 'DTCData':
        return cls(
            mil_on=data.get('mil_on', False),
            confirmed_count=data.get('confirmed_count', 0),
            pending_count=data.get('pending_count', 0),
            confirmed=data.get('confirmed', []),
            pending=data.get('pending', []),
        )


@dataclass
class FileInfo:
    """File information from ESP32"""
    name: str
    size: int
    
    @property
    def size_kb(self) -> float:
        return self.size / 1024
    
    @property
    def is_session(self) -> bool:
        return self.name.startswith('session_')
    
    @property
    def is_canlog(self) -> bool:
        return self.name.startswith('canlog_')


@dataclass
class PIDConfig:
    """PID configuration"""
    pid: int
    name: str
    unit: str
    tier: PollTier
    enabled: bool = True
    
    @classmethod
    def from_json(cls, data: dict, tier: PollTier) -> 'PIDConfig':
        pid_str = data.get('pid', '0x00')
        pid_val = int(pid_str, 16) if pid_str.startswith('0x') else int(pid_str)
        return cls(
            pid=pid_val,
            name=data.get('name', ''),
            unit=data.get('unit', ''),
            tier=tier,
            enabled=data.get('enabled', True),
        )


@dataclass
class SignalMatch:
    """CAN signal correlation match"""
    can_id: int
    byte_pos: int
    length: int  # 1 or 2 bytes
    endian: str  # 'little' or 'big'
    pid_name: str
    correlation: float
    formula: str
    scale: float
    offset: float
    confidence: float
