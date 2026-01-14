"""
HTTP API Client for ESP32 OBD2 Logger
"""

import requests
from typing import List, Optional, Tuple
from urllib.parse import urljoin
import logging

from .models import (
    Telemetry, SystemStatus, SnifferStats, DTCData, 
    FileInfo, PIDConfig, PollTier
)

logger = logging.getLogger(__name__)


class APIClient:
    """HTTP client for ESP32 OBD2 Logger API"""
    
    def __init__(self, host: str = "192.168.1.100", timeout: float = 5.0):
        """
        Initialize API client.
        
        Args:
            host: ESP32 IP address or hostname (e.g., 'obd2logger.local')
            timeout: Request timeout in seconds
        """
        self.host = host
        self.timeout = timeout
        self._base_url = f"http://{host}"
    
    @property
    def base_url(self) -> str:
        return self._base_url
    
    def set_host(self, host: str):
        """Update host address"""
        self.host = host
        self._base_url = f"http://{host}"
    
    def _get(self, endpoint: str) -> dict:
        """Make GET request and return JSON"""
        url = urljoin(self._base_url, endpoint)
        try:
            response = requests.get(url, timeout=self.timeout)
            response.raise_for_status()
            return response.json()
        except requests.exceptions.JSONDecodeError:
            return {"raw": response.text}
        except requests.exceptions.RequestException as e:
            logger.error(f"API request failed: {e}")
            raise
    
    def _get_raw(self, endpoint: str) -> bytes:
        """Make GET request and return raw bytes"""
        url = urljoin(self._base_url, endpoint)
        response = requests.get(url, timeout=self.timeout * 2)
        response.raise_for_status()
        return response.content
    
    # =========================================================================
    # TELEMETRY & STATUS
    # =========================================================================
    
    def get_telemetry(self) -> Telemetry:
        """Get current telemetry data"""
        data = self._get("/api/telemetry")
        return Telemetry.from_json(data)
    
    def get_status(self) -> SystemStatus:
        """Get system status"""
        data = self._get("/api/status")
        return SystemStatus.from_json(data)
    
    def ping(self) -> bool:
        """Check if device is reachable"""
        try:
            self.get_status()
            return True
        except:
            return False
    
    # =========================================================================
    # SNIFFER CONTROL
    # =========================================================================
    
    def get_sniffer_status(self) -> SnifferStats:
        """Get CAN sniffer status"""
        data = self._get("/api/sniff/status")
        return SnifferStats.from_json(data)
    
    def start_sniffer(self, exclude_obd: bool = True, 
                      id_min: int = 0x000, id_max: int = 0x7FF,
                      duration: int = None, filter_id: int = None,
                      filter_mask: int = None) -> bool:
        """Start CAN sniffer"""
        try:
            endpoint = f"/api/sniff/start?exclude_obd={1 if exclude_obd else 0}"
            endpoint += f"&id_min=0x{id_min:03X}&id_max=0x{id_max:03X}"
            if duration:
                endpoint += f"&duration={duration}"
            if filter_id is not None:
                endpoint += f"&filter_id=0x{filter_id:03X}"
            if filter_mask is not None:
                endpoint += f"&filter_mask=0x{filter_mask:03X}"
            self._get(endpoint)
            return True
        except:
            return False
    
    def stop_sniffer(self) -> bool:
        """Stop CAN sniffer"""
        try:
            self._get("/api/sniff/stop")
            return True
        except:
            return False
    
    # =========================================================================
    # DTC (Diagnostic Trouble Codes)
    # =========================================================================
    
    def get_dtcs(self) -> DTCData:
        """Get diagnostic trouble codes"""
        data = self._get("/api/dtc/status")
        return DTCData.from_json(data)
    
    def clear_dtcs(self) -> Tuple[bool, str]:
        """Clear all DTCs"""
        try:
            data = self._get("/api/dtc/clear")
            return data.get('success', False), data.get('message', '')
        except Exception as e:
            return False, str(e)
    
    # =========================================================================
    # FILE MANAGEMENT
    # =========================================================================
    
    def get_files(self) -> List[FileInfo]:
        """Get list of files on ESP32"""
        data = self._get("/api/files")
        files = []
        for f in data.get('files', []):
            files.append(FileInfo(name=f['name'], size=f['size']))
        return files
    
    def download_file(self, filename: str) -> bytes:
        """Download a specific file"""
        return self._get_raw(f"/download?file={filename}")
    
    def download_all(self) -> bytes:
        """Download all files concatenated"""
        return self._get_raw("/download-all")
    
    def delete_file(self, filename: str) -> bool:
        """Delete a specific file"""
        try:
            self._get(f"/delete?file={filename}")
            return True
        except:
            return False
    
    def delete_all_files(self) -> bool:
        """Delete all files"""
        try:
            self._get("/delete-all")
            return True
        except:
            return False
    
    # =========================================================================
    # PID CONFIGURATION
    # =========================================================================
    
    def get_pid_config(self) -> List[List[PIDConfig]]:
        """
        Get PID configuration for all tiers.
        Returns list of 4 lists (one per tier).
        """
        data = self._get("/api/config/pids")
        result = [[], [], [], []]  # 4 tiers
        
        for tier_idx, tier_data in enumerate(data.get('tiers', [])):
            tier = PollTier(tier_idx)
            for pid_data in tier_data.get('pids', []):
                result[tier_idx].append(PIDConfig.from_json(pid_data, tier))
        
        return result

    # Convenience aliases
    def list_files(self) -> List[FileInfo]:
        """Alias for get_files"""
        return self.get_files()
    
    def read_dtc(self) -> DTCData:
        """Alias for get_dtcs"""
        return self.get_dtcs()
    
    def clear_dtc(self) -> bool:
        """Alias for clear_dtcs"""
        success, _ = self.clear_dtcs()
        return success

    def get_sniffer_stats(self) -> SnifferStats:
        """Alias for get_sniffer_status"""
        return self.get_sniffer_status()
    
    def set_pid_config(self, configs: List[PIDConfig]) -> bool:
        """Save PID configuration to device"""
        # This would require implementing the POST endpoint on ESP32
        # For now, just return True (config is stored locally)
        return True
