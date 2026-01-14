#!/usr/bin/env python3
"""
CAN Reverse Engineering Tool - Universal Signal Identifier
===========================================================

Comprehensive tool for automotive CAN bus reverse engineering.
Features:
- Correlation analysis with OBD PIDs (when available)
- Autonomous mode without OBD reference (heuristic-based)
- Shannon entropy analysis for byte classification
- Bit-flip detection for signal boundary identification
- DBC matching from opendbc repository (dynamic download)
- Interactive matplotlib visualization
- Export to CSV and DBC format

Based on methodologies from:
- SavvyCAN (byte histograms, bit analysis)
- comma.ai opendbc (DBC validation, signal patterns)
- CANalyzat0r (entropy analysis, signal grouping)

Author: OBD2 Data Acquisition Project
Date: 2025
"""

import argparse
import os
import sys
import tempfile
import urllib.request
import warnings
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Tuple, Any

import matplotlib.pyplot as plt
import matplotlib.widgets as mwidgets
import numpy as np
import pandas as pd
import seaborn as sns
from scipy import stats
from scipy.fft import fft, fftfreq

warnings.filterwarnings('ignore')

# Optional cantools for DBC support
try:
    import cantools
    DBC_SUPPORT = True
except ImportError:
    DBC_SUPPORT = False
    print("⚠️  cantools not installed - DBC export disabled")
    print("   Install with: pip install cantools\n")


# =============================================================================
# CONSTANTS & CONFIGURATION
# =============================================================================

OPENDBC_BASE_URL = "https://raw.githubusercontent.com/commaai/opendbc/master/opendbc/dbc"

# Popular DBC files from opendbc for various manufacturers
OPENDBC_FILES = {
    # Toyota
    "toyota_prius_2010_pt": f"{OPENDBC_BASE_URL}/toyota_prius_2010_pt.dbc",
    "toyota_2017_ref_pt": f"{OPENDBC_BASE_URL}/toyota_2017_ref_pt.dbc",
    "toyota_adas": f"{OPENDBC_BASE_URL}/toyota_adas.dbc",
    "toyota_tss2_adas": f"{OPENDBC_BASE_URL}/toyota_tss2_adas.dbc",
    # Honda
    "honda_civic_touring_2016_can": f"{OPENDBC_BASE_URL}/honda_civic_touring_2016_can_generated.dbc",
    "honda_accord_2018_can": f"{OPENDBC_BASE_URL}/honda_accord_2018_can_generated.dbc",
    "honda_crv_2016_can": f"{OPENDBC_BASE_URL}/honda_crv_touring_2016_can_generated.dbc",
    # Hyundai/Kia
    "hyundai_kia_generic": f"{OPENDBC_BASE_URL}/hyundai_kia_generic.dbc",
    "hyundai_canfd": f"{OPENDBC_BASE_URL}/hyundai_canfd.dbc",
    # VW/Audi
    "vw_golf_mk4": f"{OPENDBC_BASE_URL}/vw_golf_mk4.dbc",
    "vw_mqb_2010": f"{OPENDBC_BASE_URL}/vw_mqb_2010.dbc",
    # GM
    "gm_global_a_powertrain": f"{OPENDBC_BASE_URL}/gm_global_a_powertrain_generated.dbc",
    "gm_global_a_chassis": f"{OPENDBC_BASE_URL}/gm_global_a_chassis.dbc",
    # Ford
    "ford_fusion_2018_pt": f"{OPENDBC_BASE_URL}/ford_fusion_2018_pt.dbc",
    "ford_lincoln_base_pt": f"{OPENDBC_BASE_URL}/ford_lincoln_base_pt.dbc",
    # Mazda
    "mazda_2017": f"{OPENDBC_BASE_URL}/mazda_2017.dbc",
    # Nissan
    "nissan_x_trail_2017": f"{OPENDBC_BASE_URL}/nissan_x_trail_2017_generated.dbc",
    "nissan_leaf_2018": f"{OPENDBC_BASE_URL}/nissan_leaf_2018_generated.dbc",
    # Subaru
    "subaru_global_2017": f"{OPENDBC_BASE_URL}/subaru_global_2017_generated.dbc",
    # Chrysler/Jeep
    "chrysler_pacifica_2017_hybrid": f"{OPENDBC_BASE_URL}/chrysler_pacifica_2017_hybrid_generated.dbc",
}

# Known signal characteristics for heuristic analysis
SIGNAL_HEURISTICS = {
    'rpm': {'min': 0, 'max': 8000, 'typical_rate_hz': 100, 'bytes': 2},
    'speed': {'min': 0, 'max': 300, 'typical_rate_hz': 50, 'bytes': 1},
    'throttle': {'min': 0, 'max': 100, 'typical_rate_hz': 50, 'bytes': 1},
    'coolant_temp': {'min': -40, 'max': 150, 'typical_rate_hz': 1, 'bytes': 1},
    'steering_angle': {'min': -720, 'max': 720, 'typical_rate_hz': 100, 'bytes': 2},
}


# =============================================================================
# DATA CLASSES
# =============================================================================

@dataclass
class ByteStatistics:
    """Statistics for a single byte position in a CAN ID"""
    can_id: int
    byte_pos: int
    mean: float
    std: float
    variance: float
    min_val: int
    max_val: int
    unique_count: int
    entropy: float
    transition_rate: float  # Bit-flip rate
    update_rate_hz: float
    classification: str  # 'sensor', 'counter', 'checksum', 'status', 'unused'


@dataclass
class SignalCandidate:
    """A candidate signal discovered through analysis"""
    can_id: int
    start_byte: int
    length: int  # 1 or 2 bytes
    endian: str  # 'little' or 'big'
    encoding: str  # 'uint8', 'int8', 'uint16_le', 'uint16_be', etc
    correlation: float
    pid_name: Optional[str]
    formula: str
    scale: float
    offset: float
    confidence: float
    classification: str
    samples: int


@dataclass
class DBCMatch:
    """Result of DBC matching"""
    dbc_name: str
    signal_name: str
    message_name: str
    can_id: int
    match_score: float
    decoded_values: Optional[np.ndarray] = None


# =============================================================================
# DATA LOADING
# =============================================================================

class DataLoader:
    """Load and parse CAN and OBD data files"""
    
    @staticmethod
    def load_gvret_can(filepath: str, max_rows: Optional[int] = None, 
                       sample_rate: float = 1.0) -> pd.DataFrame:
        """Load GVRET-format CAN log with absolute timestamps
        
        Args:
            filepath: Path to CSV file
            max_rows: Maximum rows to load (None = all)
            sample_rate: Keep only this fraction of data (0.0-1.0)
        """
        print(f"📂 Loading CAN log: {filepath}")
        
        # Check file size
        file_size_mb = os.path.getsize(filepath) / (1024 * 1024)
        print(f"   File size: {file_size_mb:.1f} MB")
        
        if file_size_mb > 100 and max_rows is None and sample_rate == 1.0:
            print(f"   ⚠️  Large file detected! Consider using --max-rows or --sample-rate")
        
        # Read CSV with error handling for malformed lines
        df = pd.read_csv(filepath, nrows=max_rows, on_bad_lines='skip')
        print(f"   Loaded {len(df):,} rows")
        df.columns = df.columns.str.lower().str.strip().str.replace(' ', '_')
        
        # Drop rows with NaN in critical columns
        original_len = len(df)
        
        # Parse timestamp - keep absolute microseconds
        if 'timestamp_us' in df.columns:
            df = df.dropna(subset=['timestamp_us'])
            df['timestamp_us'] = df['timestamp_us'].astype(np.int64)
        elif 'time_stamp' in df.columns:
            df = df.dropna(subset=['time_stamp'])
            df['timestamp_us'] = df['time_stamp'].astype(np.int64)
        elif 'timestamp_ms' in df.columns:
            df = df.dropna(subset=['timestamp_ms'])
            df['timestamp_us'] = (df['timestamp_ms'] * 1000).astype(np.int64)
        else:
            df['timestamp_us'] = np.arange(len(df)) * 10000
        
        if len(df) < original_len:
            print(f"   ⚠️  Dropped {original_len - len(df)} rows with invalid timestamps")
        
        df['time_sec'] = df['timestamp_us'] / 1_000_000
        
        # Parse hex ID
        if 'id' in df.columns and df['id'].dtype == 'object':
            df['id'] = df['id'].apply(lambda x: int(x, 16) if isinstance(x, str) else x)
        
        # Rename data columns to D0-D7
        for i in range(1, 9):
            if f'd{i}' in df.columns:
                df[f'D{i-1}'] = df[f'd{i}']
        
        # Convert hex strings to int
        for i in range(8):
            col = f'D{i}'
            if col in df.columns and df[col].dtype == 'object':
                df[col] = df[col].apply(lambda x: int(x, 16) if isinstance(x, str) and x else 0)
        
        duration = (df['timestamp_us'].max() - df['timestamp_us'].min()) / 1e6
        print(f"  ✓ {len(df):,} messages, {df['id'].nunique()} unique IDs, {duration:.1f}s")
        
        # Apply sampling if requested
        if sample_rate < 1.0:
            original_len = len(df)
            df = df.sample(frac=sample_rate, random_state=42).sort_values('timestamp_us')
            print(f"  📉 Sampled {len(df):,} messages ({sample_rate:.0%} of {original_len:,})")
        
        return df
    
    @staticmethod
    def load_obd_session(filepath: str) -> pd.DataFrame:
        """Load OBD session CSV with absolute timestamps"""
        print(f"📂 Loading OBD session: {filepath}")
        
        df = pd.read_csv(filepath)
        
        if 'timestamp_us' in df.columns:
            df['timestamp_us'] = df['timestamp_us'].astype(np.int64)
        elif 'timestamp_ms' in df.columns:
            df['timestamp_us'] = (df['timestamp_ms'] * 1000).astype(np.int64)
        else:
            raise ValueError("No timestamp column found")
        
        df['time_sec'] = df['timestamp_us'] / 1_000_000
        
        # Replace -1 with NaN
        pid_cols = [c for c in df.columns if c not in ['timestamp_us', 'timestamp_ms', 'time_sec']]
        for col in pid_cols:
            df[col] = df[col].replace(-1, np.nan)
        
        duration = (df['timestamp_us'].max() - df['timestamp_us'].min()) / 1e6
        print(f"  ✓ {len(df):,} samples, {len(pid_cols)} PIDs, {duration:.1f}s")
        
        return df


# =============================================================================
# ENTROPY & STATISTICAL ANALYSIS
# =============================================================================

class EntropyAnalyzer:
    """Analyze entropy and statistical properties of CAN bytes"""
    
    @staticmethod
    def shannon_entropy(data: np.ndarray) -> float:
        """Calculate Shannon entropy of byte values (0-8 bits)"""
        if len(data) == 0:
            return 0.0
        
        # Count occurrences
        _, counts = np.unique(data, return_counts=True)
        probs = counts / len(data)
        
        # Shannon entropy: H = -Σ p(x) * log2(p(x))
        entropy = -np.sum(probs * np.log2(probs + 1e-10))
        return entropy
    
    @staticmethod
    def bit_transition_rate(data: np.ndarray) -> float:
        """Calculate average bit transition rate between consecutive values"""
        if len(data) < 2:
            return 0.0
        
        transitions = 0
        for i in range(1, len(data)):
            # XOR shows which bits changed
            diff = int(data[i]) ^ int(data[i-1])
            transitions += bin(diff).count('1')
        
        # Normalize: transitions per sample per bit
        return transitions / (len(data) - 1) / 8
    
    @staticmethod
    def classify_byte(stats: ByteStatistics) -> str:
        """Classify byte based on statistical properties"""
        
        # Unused: very low variance or single value
        if stats.unique_count <= 2 or stats.variance < 0.1:
            return 'unused'
        
        # Counter: high entropy (>7), sequential pattern, high transition rate
        if stats.entropy > 7.0 and stats.transition_rate > 0.3:
            return 'counter'
        
        # Checksum: high entropy, appears at end of message
        if stats.entropy > 6.5 and stats.byte_pos >= 6:
            return 'checksum'
        
        # Status/enum: low entropy (2-4), few unique values
        if stats.entropy < 4.0 and stats.unique_count < 20:
            return 'status'
        
        # Sensor: moderate entropy (3-6), moderate variance
        if 2.5 < stats.entropy < 7.0:
            return 'sensor'
        
        return 'unknown'
    
    def analyze_can_id(self, df: pd.DataFrame, can_id: int) -> List[ByteStatistics]:
        """Analyze all bytes of a specific CAN ID"""
        subset = df[df['id'] == can_id].copy()
        
        if len(subset) < 10:
            return []
        
        # Calculate update rate
        time_span = (subset['timestamp_us'].max() - subset['timestamp_us'].min()) / 1e6
        update_rate = len(subset) / time_span if time_span > 0 else 0
        
        results = []
        for byte_pos in range(8):
            col = f'D{byte_pos}'
            if col not in subset.columns:
                continue
            
            values = subset[col].dropna().values.astype(int)
            if len(values) < 10:
                continue
            
            stats = ByteStatistics(
                can_id=can_id,
                byte_pos=byte_pos,
                mean=np.mean(values),
                std=np.std(values),
                variance=np.var(values),
                min_val=int(np.min(values)),
                max_val=int(np.max(values)),
                unique_count=len(np.unique(values)),
                entropy=self.shannon_entropy(values),
                transition_rate=self.bit_transition_rate(values),
                update_rate_hz=update_rate,
                classification=''
            )
            stats.classification = self.classify_byte(stats)
            results.append(stats)
        
        return results


# =============================================================================
# BIT-FLIP ANALYSIS
# =============================================================================

class BitFlipDetector:
    """Detect signal boundaries using bit transition analysis"""
    
    @staticmethod
    def analyze_bit_transitions(df: pd.DataFrame, can_id: int) -> np.ndarray:
        """
        Count bit transitions for each of 64 bits in CAN frame.
        Returns array of shape (64,) with transition counts.
        """
        subset = df[df['id'] == can_id].copy()
        
        if len(subset) < 10:
            return np.zeros(64)
        
        # Build 64-bit representation of each frame
        transitions = np.zeros(64)
        
        for byte_pos in range(8):
            col = f'D{byte_pos}'
            if col not in subset.columns:
                continue
            
            values = subset[col].values.astype(np.uint8)
            
            for bit in range(8):
                bit_idx = byte_pos * 8 + bit
                # Extract bit values
                bit_values = (values >> bit) & 1
                # Count transitions
                transitions[bit_idx] = np.sum(np.abs(np.diff(bit_values)))
        
        return transitions
    
    @staticmethod
    def find_signal_boundaries(transitions: np.ndarray, threshold: float = 0.3) -> List[Tuple[int, int]]:
        """
        Find signal boundaries based on transition rate discontinuities.
        Returns list of (start_bit, end_bit) tuples.
        """
        if np.max(transitions) == 0:
            return []
        
        # Normalize
        norm_trans = transitions / np.max(transitions)
        
        # Find discontinuities (large changes in transition rate)
        boundaries = [0]
        for i in range(1, 64):
            if abs(norm_trans[i] - norm_trans[i-1]) > threshold:
                boundaries.append(i)
        boundaries.append(64)
        
        # Create signal spans
        signals = []
        for i in range(len(boundaries) - 1):
            start = boundaries[i]
            end = boundaries[i + 1]
            if end - start >= 4:  # Minimum 4 bits for a signal
                signals.append((start, end))
        
        return signals


# =============================================================================
# DBC MATCHER
# =============================================================================

class DBCMatcher:
    """Match CAN data against known DBC files from opendbc"""
    
    def __init__(self):
        self.dbc_cache: Dict[str, Any] = {}
        self.temp_dir = tempfile.mkdtemp(prefix="can_reverser_dbc_")
    
    def download_dbc(self, name: str, url: str) -> Optional[str]:
        """Download DBC file from URL to temp directory"""
        if not DBC_SUPPORT:
            return None
        
        filepath = os.path.join(self.temp_dir, f"{name}.dbc")
        
        try:
            print(f"  ⬇️  Downloading {name}...")
            urllib.request.urlretrieve(url, filepath)
            return filepath
        except Exception as e:
            print(f"  ❌ Failed to download {name}: {e}")
            return None
    
    def load_dbc(self, name: str) -> Optional[Any]:
        """Load DBC file (from cache or download)"""
        if not DBC_SUPPORT:
            return None
        
        if name in self.dbc_cache:
            return self.dbc_cache[name]
        
        if name not in OPENDBC_FILES:
            print(f"  ❌ Unknown DBC: {name}")
            return None
        
        filepath = self.download_dbc(name, OPENDBC_FILES[name])
        if not filepath:
            return None
        
        try:
            db = cantools.database.load_file(filepath)
            self.dbc_cache[name] = db
            print(f"  ✓ Loaded {name}: {len(db.messages)} messages")
            return db
        except Exception as e:
            print(f"  ❌ Failed to parse {name}: {e}")
            return None
    
    def match_can_ids(self, can_df: pd.DataFrame, dbc_names: Optional[List[str]] = None) -> List[DBCMatch]:
        """
        Match CAN IDs from data against DBC files.
        Returns list of matches sorted by score.
        """
        if not DBC_SUPPORT:
            print("⚠️  cantools not installed - DBC matching disabled")
            return []
        
        if dbc_names is None:
            dbc_names = list(OPENDBC_FILES.keys())
        
        print(f"\n🔍 Testing {len(dbc_names)} DBC files against captured data...")
        
        can_ids = set(can_df['id'].unique())
        matches = []
        
        for dbc_name in dbc_names:
            db = self.load_dbc(dbc_name)
            if not db:
                continue
            
            dbc_ids = set()
            for msg in db.messages:
                dbc_ids.add(msg.frame_id)
            
            # Calculate match score
            common_ids = can_ids & dbc_ids
            if not common_ids:
                continue
            
            score = len(common_ids) / len(can_ids)
            
            for msg in db.messages:
                if msg.frame_id in can_ids:
                    for signal in msg.signals:
                        matches.append(DBCMatch(
                            dbc_name=dbc_name,
                            signal_name=signal.name,
                            message_name=msg.name,
                            can_id=msg.frame_id,
                            match_score=score
                        ))
        
        # Sort by score
        matches.sort(key=lambda x: x.match_score, reverse=True)
        
        if matches:
            best_dbc = matches[0].dbc_name
            best_score = matches[0].match_score
            print(f"\n  🏆 Best match: {best_dbc} (score: {best_score:.1%})")
        
        return matches
    
    def decode_with_dbc(self, can_df: pd.DataFrame, dbc_name: str, 
                        signal_name: str) -> Optional[np.ndarray]:
        """Decode a signal using DBC definition"""
        db = self.load_dbc(dbc_name)
        if not db:
            return None
        
        # Find signal
        target_msg = None
        target_signal = None
        for msg in db.messages:
            for sig in msg.signals:
                if sig.name == signal_name:
                    target_msg = msg
                    target_signal = sig
                    break
            if target_signal:
                break
        
        if not target_signal:
            return None
        
        # Decode
        subset = can_df[can_df['id'] == target_msg.frame_id].copy()
        if len(subset) == 0:
            return None
        
        decoded = []
        for _, row in subset.iterrows():
            data = bytes([int(row[f'D{i}']) for i in range(8)])
            try:
                values = target_msg.decode(data)
                val = values.get(signal_name, np.nan)
                # Convert NamedSignalValue to float if needed
                if hasattr(val, 'value'):
                    val = float(val.value)
                elif not isinstance(val, (int, float)):
                    val = float(val)
                decoded.append(val)
            except:
                decoded.append(np.nan)
        
        return np.array(decoded, dtype=float)


# =============================================================================
# CORRELATION ANALYSIS
# =============================================================================

class Correlator:
    """Correlate CAN data with OBD PIDs"""
    
    @staticmethod
    def test_byte_encodings(can_values: np.ndarray, pid_values: np.ndarray) -> Optional[SignalCandidate]:
        """Test different single-byte encodings"""
        if len(can_values) < 10 or len(pid_values) < 10:
            return None
        
        # Align lengths
        n = min(len(can_values), len(pid_values))
        can_values = can_values[:n]
        pid_values = pid_values[:n]
        
        best = None
        
        # Test uint8
        try:
            corr, _ = stats.pearsonr(can_values, pid_values)
            if abs(corr) > 0.5:
                slope, intercept = np.polyfit(can_values, pid_values, 1)
                best = {
                    'encoding': 'uint8',
                    'correlation': corr,
                    'scale': slope,
                    'offset': intercept,
                    'formula': f"{slope:.4f} * byte + {intercept:.2f}"
                }
        except:
            pass
        
        # Test int8
        try:
            signed = np.array([x if x < 128 else x - 256 for x in can_values])
            corr, _ = stats.pearsonr(signed, pid_values)
            if abs(corr) > (abs(best['correlation']) if best else 0.5):
                slope, intercept = np.polyfit(signed, pid_values, 1)
                best = {
                    'encoding': 'int8',
                    'correlation': corr,
                    'scale': slope,
                    'offset': intercept,
                    'formula': f"{slope:.4f} * signed + {intercept:.2f}"
                }
        except:
            pass
        
        # Test common scales
        for scale_factor in [0.25, 0.5, 2, 4, 10, 32, 64, 100]:
            try:
                scaled = can_values * scale_factor
                corr, _ = stats.pearsonr(scaled, pid_values)
                if abs(corr) > (abs(best['correlation']) if best else 0.5):
                    slope, intercept = np.polyfit(scaled, pid_values, 1)
                    best = {
                        'encoding': f'uint8 × {scale_factor}',
                        'correlation': corr,
                        'scale': slope * scale_factor,
                        'offset': intercept,
                        'formula': f"{slope:.4f} * (byte × {scale_factor}) + {intercept:.2f}"
                    }
            except:
                pass
        
        return best
    
    @staticmethod
    def test_multibyte_encodings(can_df: pd.DataFrame, can_id: int, 
                                  b1: int, b2: int, pid_values: np.ndarray) -> Optional[dict]:
        """Test 16-bit encodings (big/little endian)"""
        subset = can_df[can_df['id'] == can_id].copy()
        
        if len(subset) < 10:
            return None
        
        n = min(len(subset), len(pid_values))
        
        # Big-endian
        be_values = (subset[f'D{b1}'].values[:n] * 256 + subset[f'D{b2}'].values[:n])
        # Little-endian
        le_values = (subset[f'D{b2}'].values[:n] * 256 + subset[f'D{b1}'].values[:n])
        
        best = None
        
        try:
            corr_be, _ = stats.pearsonr(be_values, pid_values[:n])
            if abs(corr_be) > 0.5:
                slope, intercept = np.polyfit(be_values, pid_values[:n], 1)
                best = {
                    'encoding': 'uint16_be',
                    'endian': 'big',
                    'correlation': corr_be,
                    'scale': slope,
                    'offset': intercept,
                    'formula': f"(D{b1}<<8|D{b2}) × {slope:.6f} + {intercept:.2f}"
                }
        except:
            pass
        
        try:
            corr_le, _ = stats.pearsonr(le_values, pid_values[:n])
            if abs(corr_le) > (abs(best['correlation']) if best else 0.5):
                slope, intercept = np.polyfit(le_values, pid_values[:n], 1)
                best = {
                    'encoding': 'uint16_le',
                    'endian': 'little',
                    'correlation': corr_le,
                    'scale': slope,
                    'offset': intercept,
                    'formula': f"(D{b2}<<8|D{b1}) × {slope:.6f} + {intercept:.2f}"
                }
        except:
            pass
        
        return best
    
    def find_correlations(self, can_df: pd.DataFrame, obd_df: pd.DataFrame,
                          threshold: float = 0.7, min_rpm: int = 0) -> List[SignalCandidate]:
        """Find all significant correlations between CAN and OBD data"""
        print(f"\n🔍 Searching for correlations (threshold={threshold})...")
        
        # Find time overlap
        can_min = can_df['timestamp_us'].min()
        can_max = can_df['timestamp_us'].max()
        obd_min = obd_df['timestamp_us'].min()
        obd_max = obd_df['timestamp_us'].max()
        
        overlap_start = max(can_min, obd_min)
        overlap_end = min(can_max, obd_max)
        
        if overlap_start >= overlap_end:
            print("❌ No time overlap between CAN and OBD data!")
            return []
        
        duration = (overlap_end - overlap_start) / 1e6
        print(f"  Overlap: {duration:.1f}s")
        
        # Filter to overlap
        can_df = can_df[(can_df['timestamp_us'] >= overlap_start) & 
                        (can_df['timestamp_us'] <= overlap_end)].copy()
        obd_df = obd_df[(obd_df['timestamp_us'] >= overlap_start) & 
                        (obd_df['timestamp_us'] <= overlap_end)].copy()
        
        # Normalize time for merge
        can_df['time_sec'] = (can_df['timestamp_us'] - overlap_start) / 1e6
        obd_df['time_sec'] = (obd_df['timestamp_us'] - overlap_start) / 1e6
        
        # Filter by RPM if requested
        if 'rpm' in obd_df.columns and min_rpm > 0:
            obd_df = obd_df[obd_df['rpm'] > min_rpm].copy()
            print(f"  Filtered to RPM > {min_rpm}: {len(obd_df)} samples")
        
        # Get valid PIDs
        pid_cols = [c for c in obd_df.columns if c not in ['timestamp_us', 'timestamp_ms', 'time_sec']]
        valid_pids = [p for p in pid_cols if obd_df[p].notna().sum() >= 10 and obd_df[p].var() > 1.0]
        
        print(f"  Valid PIDs for correlation: {', '.join(valid_pids)}")
        
        results = []
        can_ids = sorted(can_df['id'].unique())
        
        # Ensure CAN IDs are integers
        can_ids = [int(x) for x in can_ids]
        
        # Single byte analysis
        print(f"\n  Testing single bytes...")
        for can_id in can_ids:
            can_subset = can_df[can_df['id'] == can_id].copy()
            
            for byte_pos in range(8):
                for pid_name in valid_pids:
                    # Merge by time
                    merged = pd.merge_asof(
                        can_subset.sort_values('time_sec'),
                        obd_df[['time_sec', pid_name]].sort_values('time_sec'),
                        on='time_sec',
                        tolerance=0.1,
                        direction='nearest'
                    ).dropna(subset=[f'D{byte_pos}', pid_name])
                    
                    if len(merged) < 10:
                        continue
                    
                    can_values = merged[f'D{byte_pos}'].values
                    pid_values = merged[pid_name].values
                    
                    result = self.test_byte_encodings(can_values, pid_values)
                    if result and abs(result['correlation']) >= threshold:
                        candidate = SignalCandidate(
                            can_id=can_id,
                            start_byte=byte_pos,
                            length=1,
                            endian='little',
                            encoding=result['encoding'],
                            correlation=result['correlation'],
                            pid_name=pid_name,
                            formula=result['formula'],
                            scale=result['scale'],
                            offset=result['offset'],
                            confidence=abs(result['correlation']),
                            classification='sensor',
                            samples=len(merged)
                        )
                        results.append(candidate)
                        print(f"    ✓ 0x{can_id:03X}[D{byte_pos}] ↔ {pid_name}: r={result['correlation']:.3f}")
        
        # Multi-byte analysis
        print(f"\n  Testing 16-bit combinations...")
        for can_id in can_ids:
            can_subset = can_df[can_df['id'] == can_id].copy()
            
            for pid_name in valid_pids:
                merged = pd.merge_asof(
                    can_subset.sort_values('time_sec'),
                    obd_df[['time_sec', pid_name]].sort_values('time_sec'),
                    on='time_sec',
                    tolerance=0.1,
                    direction='nearest'
                ).dropna(subset=[pid_name])
                
                if len(merged) < 10:
                    continue
                
                pid_values = merged[pid_name].values
                
                for b1 in range(7):
                    b2 = b1 + 1
                    result = self.test_multibyte_encodings(merged, can_id, b1, b2, pid_values)
                    
                    if result and abs(result['correlation']) >= threshold:
                        candidate = SignalCandidate(
                            can_id=can_id,
                            start_byte=b1,
                            length=2,
                            endian=result['endian'],
                            encoding=result['encoding'],
                            correlation=result['correlation'],
                            pid_name=pid_name,
                            formula=result['formula'],
                            scale=result['scale'],
                            offset=result['offset'],
                            confidence=abs(result['correlation']),
                            classification='sensor',
                            samples=len(merged)
                        )
                        results.append(candidate)
                        print(f"    ✓ 0x{can_id:03X}[D{b1}:D{b2}] ↔ {pid_name}: r={result['correlation']:.3f} ({result['endian']})")
        
        # Sort by correlation
        results.sort(key=lambda x: abs(x.correlation), reverse=True)
        print(f"\n✅ Found {len(results)} significant correlations")
        
        return results


# =============================================================================
# AUTONOMOUS MODE (No OBD Reference)
# =============================================================================

class AutonomousAnalyzer:
    """Analyze CAN data without OBD reference using heuristics"""
    
    def __init__(self):
        self.entropy_analyzer = EntropyAnalyzer()
        self.bitflip_detector = BitFlipDetector()
    
    def analyze(self, can_df: pd.DataFrame) -> List[SignalCandidate]:
        """
        Analyze CAN data without OBD reference.
        Uses entropy, variance, and bit-flip patterns to identify probable signals.
        """
        print("\n🔬 Autonomous analysis (no OBD reference)...")
        
        candidates = []
        can_ids = sorted(can_df['id'].unique())
        
        print(f"  Analyzing {len(can_ids)} CAN IDs...\n")
        
        for can_id in can_ids:
            # Get byte statistics
            byte_stats = self.entropy_analyzer.analyze_can_id(can_df, can_id)
            
            # Get bit transitions
            transitions = self.bitflip_detector.analyze_bit_transitions(can_df, can_id)
            
            for stats in byte_stats:
                if stats.classification == 'sensor':
                    # High confidence sensor candidate
                    confidence = 0.5 + (0.5 - abs(stats.entropy - 4.5) / 4.5) * 0.3
                    
                    candidates.append(SignalCandidate(
                        can_id=can_id,
                        start_byte=stats.byte_pos,
                        length=1,
                        endian='little',
                        encoding='uint8',
                        correlation=0.0,
                        pid_name=None,
                        formula=f"scale * D{stats.byte_pos} + offset",
                        scale=1.0,
                        offset=0.0,
                        confidence=confidence,
                        classification=stats.classification,
                        samples=stats.unique_count
                    ))
                    
                    print(f"  📊 0x{can_id:03X}[D{stats.byte_pos}]: {stats.classification} "
                          f"(entropy={stats.entropy:.2f}, var={stats.variance:.1f}, "
                          f"range={stats.min_val}-{stats.max_val})")
        
        # Look for 16-bit patterns (consecutive sensor bytes)
        for can_id in can_ids:
            byte_stats = self.entropy_analyzer.analyze_can_id(can_df, can_id)
            sensor_bytes = [s.byte_pos for s in byte_stats if s.classification == 'sensor']
            
            for i in range(len(sensor_bytes) - 1):
                if sensor_bytes[i+1] - sensor_bytes[i] == 1:
                    # Consecutive sensor bytes - likely 16-bit value
                    candidates.append(SignalCandidate(
                        can_id=can_id,
                        start_byte=sensor_bytes[i],
                        length=2,
                        endian='little',  # Assume little-endian (common)
                        encoding='uint16_le',
                        correlation=0.0,
                        pid_name=None,
                        formula=f"scale * (D{sensor_bytes[i]}|D{sensor_bytes[i+1]}<<8) + offset",
                        scale=1.0,
                        offset=0.0,
                        confidence=0.6,
                        classification='sensor_16bit',
                        samples=0
                    ))
        
        print(f"\n✅ Found {len(candidates)} signal candidates (heuristic-based)")
        
        return candidates


# =============================================================================
# VISUALIZATION (Matplotlib)
# =============================================================================

class Visualizer:
    """Interactive matplotlib visualization for CAN analysis"""
    
    def __init__(self, can_df: pd.DataFrame, obd_df: Optional[pd.DataFrame] = None):
        self.can_df = can_df
        self.obd_df = obd_df
        self.fig = None
        self.axes = None
    
    def plot_correlation_overview(self, candidates: List[SignalCandidate]):
        """Plot overview of all found correlations"""
        if not candidates:
            print("No candidates to visualize")
            return
        
        # Create correlation matrix data
        data = {}
        for c in candidates:
            key = f"0x{c.can_id:03X}[{c.start_byte}]"
            if c.pid_name:
                if key not in data:
                    data[key] = {}
                data[key][c.pid_name] = c.correlation
        
        if not data:
            return
        
        # Convert to DataFrame for heatmap
        df = pd.DataFrame(data).T.fillna(0)
        
        fig, ax = plt.subplots(figsize=(12, max(6, len(df) * 0.4)))
        
        sns.heatmap(df, annot=True, fmt='.2f', cmap='RdBu_r', center=0,
                    vmin=-1, vmax=1, ax=ax, cbar_kws={'label': 'Correlation'})
        
        ax.set_title('CAN Signal ↔ OBD PID Correlation Matrix', fontsize=14, fontweight='bold')
        ax.set_xlabel('OBD PID')
        ax.set_ylabel('CAN ID [Byte]')
        
        plt.tight_layout()
        plt.show()
    
    def plot_all_correlations(self, candidates: List[SignalCandidate], max_plots: int = 20):
        """
        Plot time-series for all correlations found.
        Shows CAN signal and OBD PID side by side for visual verification.
        """
        if self.obd_df is None:
            print("No OBD data available")
            return
        
        # Filter to candidates with PID correlation
        corr_candidates = [c for c in candidates if c.pid_name and abs(c.correlation) > 0.5]
        
        if not corr_candidates:
            print("No correlations to visualize")
            return
        
        # Sort by correlation strength
        corr_candidates.sort(key=lambda x: abs(x.correlation), reverse=True)
        corr_candidates = corr_candidates[:max_plots]
        
        n_plots = len(corr_candidates)
        n_cols = 2
        n_rows = (n_plots + 1) // 2
        
        fig, axes = plt.subplots(n_rows, n_cols, figsize=(16, 4 * n_rows))
        if n_rows == 1:
            axes = axes.reshape(1, -1)
        
        fig.suptitle('CAN ↔ OBD Correlations - Time Series Verification', 
                     fontsize=16, fontweight='bold', y=1.02)
        
        for idx, candidate in enumerate(corr_candidates):
            row = idx // n_cols
            col = idx % n_cols
            ax = axes[row, col]
            
            can_id = candidate.can_id
            pid_name = candidate.pid_name
            
            # Get CAN data
            can_subset = self.can_df[self.can_df['id'] == can_id].copy()
            
            # Handle multi-byte
            if candidate.length == 2:
                b1 = candidate.start_byte
                b2 = b1 + 1
                if candidate.endian == 'little':
                    can_subset['signal'] = can_subset[f'D{b1}'] + can_subset[f'D{b2}'] * 256
                else:
                    can_subset['signal'] = can_subset[f'D{b1}'] * 256 + can_subset[f'D{b2}']
                byte_label = f'D{b1}:D{b2}'
            else:
                can_subset['signal'] = can_subset[f'D{candidate.start_byte}']
                byte_label = f'D{candidate.start_byte}'
            
            # Dual axis plot
            color1 = '#3498db'
            color2 = '#e74c3c'
            
            ax.set_xlabel('Time (s)')
            ax.set_ylabel(f'CAN {byte_label}', color=color1)
            line1 = ax.plot(can_subset['time_sec'], can_subset['signal'], 
                           color=color1, alpha=0.7, linewidth=0.8, label=f'CAN')[0]
            ax.tick_params(axis='y', labelcolor=color1)
            
            ax2 = ax.twinx()
            ax2.set_ylabel(f'{pid_name}', color=color2)
            line2 = ax2.plot(self.obd_df['time_sec'], self.obd_df[pid_name], 
                            color=color2, alpha=0.7, linewidth=0.8, label=f'OBD')[0]
            ax2.tick_params(axis='y', labelcolor=color2)
            
            # Title with correlation
            corr_sign = '+' if candidate.correlation > 0 else ''
            ax.set_title(f'0x{can_id:03X}[{byte_label}] ↔ {pid_name}\n'
                        f'r = {corr_sign}{candidate.correlation:.3f} | {candidate.encoding}',
                        fontsize=10)
            ax.grid(True, alpha=0.3)
            
            # Legend
            ax.legend([line1, line2], [f'CAN {byte_label}', f'OBD {pid_name}'], 
                     loc='upper right', fontsize=8)
        
        # Hide empty subplots
        for idx in range(n_plots, n_rows * n_cols):
            row = idx // n_cols
            col = idx % n_cols
            axes[row, col].set_visible(False)
        
        plt.tight_layout()
        plt.show()
    
    def plot_correlations_by_pid(self, candidates: List[SignalCandidate]):
        """
        Plot correlations grouped by PID - one window per PID with all CAN signals overlaid.
        This allows comparing which CAN signal best matches each PID.
        """
        if self.obd_df is None:
            print("No OBD data available")
            return
        
        # Filter to candidates with PID correlation
        corr_candidates = [c for c in candidates if c.pid_name and abs(c.correlation) > 0.3]
        
        if not corr_candidates:
            print("No correlations to visualize")
            return
        
        # Group by PID
        pids = {}
        for c in corr_candidates:
            if c.pid_name not in pids:
                pids[c.pid_name] = []
            pids[c.pid_name].append(c)
        
        # Sort each group by correlation
        for pid in pids:
            pids[pid].sort(key=lambda x: abs(x.correlation), reverse=True)
        
        # Color palette for CAN signals
        colors = ['#3498db', '#2ecc71', '#9b59b6', '#f39c12', '#1abc9c', 
                  '#e67e22', '#34495e', '#16a085', '#c0392b', '#8e44ad']
        
        print(f"\n📊 Creating {len(pids)} plots (one per PID)...")
        
        for pid_name, pid_candidates in pids.items():
            fig, ax1 = plt.subplots(figsize=(16, 8))
            
            # Plot OBD PID (reference) on right axis
            ax2 = ax1.twinx()
            obd_color = '#e74c3c'
            ax2.plot(self.obd_df['time_sec'], self.obd_df[pid_name], 
                    color=obd_color, linewidth=2, alpha=0.9, label=f'OBD {pid_name}')
            ax2.set_ylabel(f'OBD {pid_name}', color=obd_color, fontsize=12)
            ax2.tick_params(axis='y', labelcolor=obd_color)
            
            # Plot all CAN signals for this PID on left axis
            legend_handles = []
            legend_labels = []
            
            for idx, candidate in enumerate(pid_candidates[:10]):  # Limit to top 10
                can_id = candidate.can_id
                can_subset = self.can_df[self.can_df['id'] == can_id].copy()
                
                # Handle multi-byte
                if candidate.length == 2:
                    b1 = candidate.start_byte
                    b2 = b1 + 1
                    if candidate.endian == 'little':
                        can_subset['signal'] = can_subset[f'D{b1}'] + can_subset[f'D{b2}'] * 256
                    else:
                        can_subset['signal'] = can_subset[f'D{b1}'] * 256 + can_subset[f'D{b2}']
                    byte_label = f'D{b1}:D{b2}'
                else:
                    can_subset['signal'] = can_subset[f'D{candidate.start_byte}']
                    byte_label = f'D{candidate.start_byte}'
                
                # Normalize CAN signal to 0-1 for overlay comparison
                sig_min = can_subset['signal'].min()
                sig_max = can_subset['signal'].max()
                if sig_max > sig_min:
                    can_subset['signal_norm'] = (can_subset['signal'] - sig_min) / (sig_max - sig_min)
                else:
                    can_subset['signal_norm'] = 0.5
                
                color = colors[idx % len(colors)]
                corr_sign = '+' if candidate.correlation > 0 else ''
                label = f'0x{can_id:03X}[{byte_label}] r={corr_sign}{candidate.correlation:.2f}'
                
                line, = ax1.plot(can_subset['time_sec'], can_subset['signal_norm'], 
                               color=color, linewidth=1.2, alpha=0.7, label=label)
                legend_handles.append(line)
                legend_labels.append(label)
            
            ax1.set_xlabel('Time (seconds)', fontsize=12)
            ax1.set_ylabel('CAN Signals (normalized 0-1)', fontsize=12)
            ax1.set_ylim(-0.1, 1.1)
            ax1.grid(True, alpha=0.3)
            
            # Combined legend
            obd_line = plt.Line2D([0], [0], color=obd_color, linewidth=2, label=f'OBD {pid_name}')
            all_handles = legend_handles + [obd_line]
            all_labels = legend_labels + [f'OBD {pid_name} (right axis)']
            
            ax1.legend(all_handles, all_labels, loc='upper left', fontsize=9, 
                      ncol=2, framealpha=0.9)
            
            # Title
            n_candidates = len(pid_candidates)
            best_corr = pid_candidates[0].correlation if pid_candidates else 0
            best_can = pid_candidates[0] if pid_candidates else None
            
            if best_can:
                if best_can.length == 2:
                    best_label = f'0x{best_can.can_id:03X}[D{best_can.start_byte}:D{best_can.start_byte+1}]'
                else:
                    best_label = f'0x{best_can.can_id:03X}[D{best_can.start_byte}]'
                title = f'PID: {pid_name.upper()} — {n_candidates} correlations found\n'
                title += f'Best match: {best_label} (r = {best_corr:.3f})'
            else:
                title = f'PID: {pid_name.upper()}'
            
            fig.suptitle(title, fontsize=14, fontweight='bold')
            
            plt.tight_layout()
            plt.show()
    
    def plot_dual_axis(self, can_id: int, byte_pos: int, pid_name: str):
        """Plot CAN signal and OBD PID with dual Y-axes"""
        if self.obd_df is None:
            print("No OBD data available for dual-axis plot")
            return
        
        can_subset = self.can_df[self.can_df['id'] == can_id].copy()
        
        fig, ax1 = plt.subplots(figsize=(14, 6))
        
        # CAN signal (left axis)
        color1 = '#3498db'
        ax1.set_xlabel('Time (seconds)')
        ax1.set_ylabel(f'CAN 0x{can_id:03X} D{byte_pos}', color=color1)
        ax1.plot(can_subset['time_sec'], can_subset[f'D{byte_pos}'], 
                 color=color1, alpha=0.8, linewidth=1, label=f'CAN D{byte_pos}')
        ax1.tick_params(axis='y', labelcolor=color1)
        
        # OBD PID (right axis)
        ax2 = ax1.twinx()
        color2 = '#e74c3c'
        ax2.set_ylabel(f'OBD {pid_name}', color=color2)
        ax2.plot(self.obd_df['time_sec'], self.obd_df[pid_name], 
                 color=color2, alpha=0.8, linewidth=1, label=f'OBD {pid_name}')
        ax2.tick_params(axis='y', labelcolor=color2)
        
        plt.title(f'CAN 0x{can_id:03X}[D{byte_pos}] vs OBD {pid_name}', fontsize=14, fontweight='bold')
        fig.tight_layout()
        plt.show()
    
    def plot_entropy_analysis(self, byte_stats: List[ByteStatistics]):
        """Plot entropy analysis for all CAN IDs"""
        if not byte_stats:
            print("No statistics to visualize")
            return
        
        # Group by CAN ID
        can_ids = sorted(set(s.can_id for s in byte_stats))
        
        fig, axes = plt.subplots(2, 2, figsize=(14, 10))
        
        # 1. Entropy by byte position
        ax = axes[0, 0]
        for can_id in can_ids[:10]:  # Limit to 10 IDs
            id_stats = [s for s in byte_stats if s.can_id == can_id]
            x = [s.byte_pos for s in id_stats]
            y = [s.entropy for s in id_stats]
            ax.plot(x, y, 'o-', label=f'0x{can_id:03X}', alpha=0.7)
        ax.set_xlabel('Byte Position')
        ax.set_ylabel('Shannon Entropy (bits)')
        ax.set_title('Entropy by Byte Position')
        ax.legend(loc='upper right', fontsize=8)
        ax.grid(True, alpha=0.3)
        
        # 2. Variance distribution
        ax = axes[0, 1]
        variances = [s.variance for s in byte_stats]
        classifications = [s.classification for s in byte_stats]
        colors = {'sensor': 'green', 'counter': 'red', 'status': 'blue', 
                  'checksum': 'orange', 'unused': 'gray', 'unknown': 'purple'}
        c = [colors.get(cl, 'black') for cl in classifications]
        ax.scatter(range(len(variances)), variances, c=c, alpha=0.6)
        ax.set_xlabel('Byte Index')
        ax.set_ylabel('Variance')
        ax.set_title('Variance Distribution (colored by classification)')
        ax.set_yscale('log')
        ax.grid(True, alpha=0.3)
        
        # 3. Classification pie chart
        ax = axes[1, 0]
        class_counts = {}
        for s in byte_stats:
            class_counts[s.classification] = class_counts.get(s.classification, 0) + 1
        ax.pie(class_counts.values(), labels=class_counts.keys(), autopct='%1.1f%%',
               colors=[colors.get(k, 'gray') for k in class_counts.keys()])
        ax.set_title('Byte Classification Distribution')
        
        # 4. Transition rate vs entropy
        ax = axes[1, 1]
        for s in byte_stats:
            ax.scatter(s.entropy, s.transition_rate, 
                      c=colors.get(s.classification, 'black'), alpha=0.6)
        ax.set_xlabel('Entropy (bits)')
        ax.set_ylabel('Bit Transition Rate')
        ax.set_title('Entropy vs Transition Rate')
        ax.grid(True, alpha=0.3)
        
        plt.tight_layout()
        plt.show()
    
    def plot_interactive_timeline(self, can_id: int):
        """Interactive timeline with slider for a specific CAN ID"""
        subset = self.can_df[self.can_df['id'] == can_id].copy()
        
        if len(subset) == 0:
            print(f"No data for CAN ID 0x{can_id:03X}")
            return
        
        fig, axes = plt.subplots(4, 2, figsize=(16, 12))
        fig.suptitle(f'CAN ID 0x{can_id:03X} - Byte Analysis', fontsize=14, fontweight='bold')
        
        lines = []
        
        for i in range(8):
            ax = axes[i // 2, i % 2]
            line, = ax.plot(subset['time_sec'], subset[f'D{i}'], linewidth=0.8)
            lines.append(line)
            ax.set_ylabel(f'D{i}')
            ax.set_ylim(-5, 260)
            ax.grid(True, alpha=0.3)
            
            if i >= 6:
                ax.set_xlabel('Time (seconds)')
        
        plt.tight_layout()
        plt.subplots_adjust(bottom=0.15)
        
        # Add slider
        ax_slider = plt.axes([0.15, 0.02, 0.7, 0.03])
        time_range = subset['time_sec'].max() - subset['time_sec'].min()
        slider = mwidgets.Slider(ax_slider, 'Window Start', 0, max(0, time_range - 10), 
                                 valinit=0, valstep=0.5)
        
        window_size = min(10, time_range)
        
        def update(val):
            start = slider.val
            end = start + window_size
            for i, ax in enumerate(axes.flat):
                ax.set_xlim(start, end)
            fig.canvas.draw_idle()
        
        slider.on_changed(update)
        update(0)
        
        plt.show()
    
    def plot_scatter_regression(self, can_id: int, byte_pos: int, pid_name: str):
        """Scatter plot with regression line"""
        if self.obd_df is None:
            return
        
        can_subset = self.can_df[self.can_df['id'] == can_id].copy()
        
        # Merge
        merged = pd.merge_asof(
            can_subset.sort_values('time_sec'),
            self.obd_df[['time_sec', pid_name]].sort_values('time_sec'),
            on='time_sec',
            tolerance=0.1,
            direction='nearest'
        ).dropna()
        
        if len(merged) < 10:
            print("Not enough data for scatter plot")
            return
        
        x = merged[f'D{byte_pos}'].values
        y = merged[pid_name].values
        
        # Regression
        slope, intercept = np.polyfit(x, y, 1)
        corr, _ = stats.pearsonr(x, y)
        
        fig, ax = plt.subplots(figsize=(10, 8))
        
        ax.scatter(x, y, alpha=0.5, s=20, c='#3498db')
        
        # Regression line
        x_line = np.linspace(x.min(), x.max(), 100)
        ax.plot(x_line, slope * x_line + intercept, 'r--', linewidth=2,
                label=f'y = {slope:.4f}x + {intercept:.2f}\nr = {corr:.3f}')
        
        ax.set_xlabel(f'CAN 0x{can_id:03X} D{byte_pos}')
        ax.set_ylabel(f'OBD {pid_name}')
        ax.set_title(f'Correlation: 0x{can_id:03X}[D{byte_pos}] vs {pid_name}', 
                     fontsize=14, fontweight='bold')
        ax.legend()
        ax.grid(True, alpha=0.3)
        
        plt.tight_layout()
        plt.show()
    
    def plot_dbc_verification(self, dbc_matcher: 'DBCMatcher', dbc_names: Optional[List[str]] = None):
        """
        Visualize DBC verification: decode CAN signals using known DBCs
        and compare with OBD PIDs to verify if they match.
        
        This is the DBC verification step - tests if existing DBC files
        (like Toyota, Honda from opendbc) can decode your CAN data correctly.
        """
        if self.obd_df is None:
            print("⚠️ OBD data required for DBC verification")
            return
        
        if not DBC_SUPPORT:
            print("⚠️ cantools not installed - DBC verification disabled")
            return
        
        print("\n" + "="*60)
        print("🔍 DBC VERIFICATION - Testing existing DBC files")
        print("="*60)
        
        # Get DBC matches
        matches = dbc_matcher.match_can_ids(self.can_df, dbc_names)
        
        if not matches:
            print("❌ No DBC matches found for the captured CAN IDs")
            return
        
        # Group matches by DBC and signal
        dbc_signals = {}
        for m in matches:
            key = (m.dbc_name, m.signal_name)
            if key not in dbc_signals:
                dbc_signals[key] = m
        
        # For each unique DBC signal, decode and compare with OBD PIDs
        obd_pids = [col for col in self.obd_df.columns if col not in ['time_sec', 'timestamp']]
        
        print(f"\n📊 Testing {len(dbc_signals)} DBC signals against {len(obd_pids)} OBD PIDs...")
        
        # Find correlations between DBC-decoded signals and OBD PIDs
        dbc_obd_correlations = []
        
        for (dbc_name, signal_name), match in dbc_signals.items():
            decoded = dbc_matcher.decode_with_dbc(self.can_df, dbc_name, signal_name)
            if decoded is None or len(decoded) < 10:
                continue
            
            # Get timestamps for decoded signal
            can_subset = self.can_df[self.can_df['id'] == match.can_id].copy()
            if len(can_subset) != len(decoded):
                continue
            
            can_subset = can_subset.sort_values('time_sec')
            can_subset['decoded'] = decoded
            
            # Skip if no variance
            if np.nanstd(decoded) < 0.001:
                continue
            
            # Correlate with each OBD PID
            for pid_name in obd_pids:
                merged = pd.merge_asof(
                    can_subset[['time_sec', 'decoded']].sort_values('time_sec'),
                    self.obd_df[['time_sec', pid_name]].sort_values('time_sec'),
                    on='time_sec',
                    tolerance=0.1,
                    direction='nearest'
                ).dropna()
                
                if len(merged) < 10:
                    continue
                
                try:
                    corr, _ = stats.pearsonr(merged['decoded'], merged[pid_name])
                    if not np.isnan(corr) and abs(corr) > 0.5:
                        dbc_obd_correlations.append({
                            'dbc_name': dbc_name,
                            'signal_name': signal_name,
                            'can_id': match.can_id,
                            'pid_name': pid_name,
                            'correlation': corr,
                            'samples': len(merged),
                            'decoded_values': merged['decoded'].values,
                            'obd_values': merged[pid_name].values,
                            'time_sec': merged['time_sec'].values
                        })
                except:
                    continue
        
        # Sort by absolute correlation
        dbc_obd_correlations.sort(key=lambda x: abs(x['correlation']), reverse=True)
        
        if not dbc_obd_correlations:
            print("❌ No significant correlations found between DBC signals and OBD PIDs")
            print("   The existing DBCs may not be compatible with your vehicle.")
            return
        
        # Print summary
        print(f"\n✅ Found {len(dbc_obd_correlations)} DBC signal ↔ OBD PID matches!\n")
        print("Best matches:")
        print("-" * 80)
        
        shown = set()
        for i, match in enumerate(dbc_obd_correlations[:15]):
            key = (match['dbc_name'], match['signal_name'], match['pid_name'])
            if key in shown:
                continue
            shown.add(key)
            
            corr_sign = '+' if match['correlation'] > 0 else ''
            print(f"  {i+1:2d}. {match['dbc_name']:15s} → {match['signal_name']:25s}")
            print(f"      CAN ID 0x{match['can_id']:03X} ↔ OBD {match['pid_name']:12s}  "
                  f"(r = {corr_sign}{match['correlation']:.3f}, n = {match['samples']})")
        
        # Visualize top matches
        print(f"\n📈 Plotting top DBC verification matches...\n")
        
        # Group by OBD PID for visualization
        pid_dbc_matches = {}
        for match in dbc_obd_correlations:
            pid = match['pid_name']
            if pid not in pid_dbc_matches:
                pid_dbc_matches[pid] = []
            if len(pid_dbc_matches[pid]) < 5:  # Max 5 DBC signals per PID
                pid_dbc_matches[pid].append(match)
        
        # Plot each PID with its DBC matches
        for pid_name, pid_matches in list(pid_dbc_matches.items())[:6]:  # Max 6 plots
            fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(14, 8), 
                                            gridspec_kw={'height_ratios': [3, 1]})
            
            colors = plt.cm.Set2(np.linspace(0, 1, len(pid_matches)))
            
            # Top plot: Time series comparison
            obd_subset = self.obd_df[['time_sec', pid_name]].copy()
            
            # Normalize OBD for comparison
            obd_min, obd_max = obd_subset[pid_name].min(), obd_subset[pid_name].max()
            if obd_max > obd_min:
                obd_norm = (obd_subset[pid_name] - obd_min) / (obd_max - obd_min)
            else:
                obd_norm = obd_subset[pid_name] * 0 + 0.5
            
            ax1.plot(obd_subset['time_sec'], obd_norm, 'k-', linewidth=2, 
                     alpha=0.8, label=f'OBD {pid_name} (reference)')
            
            legend_lines = []
            legend_labels = []
            
            for idx, match in enumerate(pid_matches):
                # Normalize DBC signal
                dbc_min, dbc_max = match['decoded_values'].min(), match['decoded_values'].max()
                if dbc_max > dbc_min:
                    dbc_norm = (match['decoded_values'] - dbc_min) / (dbc_max - dbc_min)
                else:
                    dbc_norm = match['decoded_values'] * 0 + 0.5
                
                corr_sign = '+' if match['correlation'] > 0 else ''
                label = f"DBC: {match['signal_name']} (r={corr_sign}{match['correlation']:.2f})"
                
                line, = ax1.plot(match['time_sec'], dbc_norm, '-', 
                                color=colors[idx], linewidth=1.2, alpha=0.7, label=label)
                legend_lines.append(line)
                legend_labels.append(label)
            
            ax1.set_ylabel('Normalized Value (0-1)', fontsize=11)
            ax1.set_ylim(-0.1, 1.1)
            ax1.set_title(f'DBC Verification: {pid_name.upper()}\n'
                         f'Comparing DBC signals from {pid_matches[0]["dbc_name"]}',
                         fontsize=13, fontweight='bold')
            ax1.legend(loc='upper right', fontsize=9, framealpha=0.9)
            ax1.grid(True, alpha=0.3)
            
            # Bottom plot: Raw OBD values
            ax2.plot(obd_subset['time_sec'], obd_subset[pid_name], 'k-', linewidth=1.5)
            ax2.set_xlabel('Time (seconds)', fontsize=11)
            ax2.set_ylabel(f'{pid_name}', fontsize=11)
            ax2.grid(True, alpha=0.3)
            
            plt.tight_layout()
            plt.show()
        
        # Summary of DBC compatibility
        print("\n" + "="*60)
        print("📋 DBC VERIFICATION SUMMARY")
        print("="*60)
        
        dbc_compat = {}
        for match in dbc_obd_correlations:
            dbc = match['dbc_name']
            if dbc not in dbc_compat:
                dbc_compat[dbc] = {'good': 0, 'excellent': 0, 'total': 0}
            dbc_compat[dbc]['total'] += 1
            if abs(match['correlation']) > 0.8:
                dbc_compat[dbc]['excellent'] += 1
            elif abs(match['correlation']) > 0.6:
                dbc_compat[dbc]['good'] += 1
        
        for dbc_name, stats in dbc_compat.items():
            excellent = stats['excellent']
            good = stats['good']
            total = stats['total']
            score = (excellent * 2 + good) / max(1, total * 2) * 100
            
            if score > 50:
                icon = "✅"
            elif score > 25:
                icon = "⚠️"
            else:
                icon = "❌"
            
            print(f"  {icon} {dbc_name}: {excellent} excellent + {good} good matches "
                  f"({total} total) → Score: {score:.0f}%")
        
        print("\n💡 Signals with r > 0.8 can likely be used directly from the DBC.")
        print("   Signals with 0.6 < r < 0.8 may need scale/offset adjustment.")


# =============================================================================
# EXPORT
# =============================================================================

class Exporter:
    """Export results to CSV and DBC formats"""
    
    @staticmethod
    def to_csv(candidates: List[SignalCandidate], filepath: str):
        """Export candidates to CSV"""
        data = []
        for c in candidates:
            data.append({
                'can_id': f'0x{c.can_id:03X}',
                'can_id_dec': c.can_id,
                'start_byte': c.start_byte,
                'length': c.length,
                'endian': c.endian,
                'encoding': c.encoding,
                'correlation': c.correlation,
                'pid_name': c.pid_name or '',
                'formula': c.formula,
                'scale': c.scale,
                'offset': c.offset,
                'confidence': c.confidence,
                'classification': c.classification,
                'samples': c.samples
            })
        
        df = pd.DataFrame(data)
        df.to_csv(filepath, index=False)
        print(f"  ✓ Exported {len(candidates)} signals to {filepath}")
    
    @staticmethod
    def to_dbc(candidates: List[SignalCandidate], filepath: str, db_name: str = "DISCOVERED"):
        """Export candidates to DBC file"""
        if not DBC_SUPPORT:
            print("  ❌ cantools not installed - cannot export DBC")
            return
        
        db = cantools.database.Database()
        
        # Group by CAN ID
        messages = {}
        for c in candidates:
            if c.can_id not in messages:
                messages[c.can_id] = []
            messages[c.can_id].append(c)
        
        for can_id, signals in messages.items():
            msg_signals = []
            for i, sig in enumerate(signals):
                # Create signal name
                if sig.pid_name:
                    name = f"{sig.pid_name.upper()}"
                else:
                    name = f"SIG_{sig.start_byte}"
                
                # Avoid duplicates
                existing_names = [s.name for s in msg_signals]
                if name in existing_names:
                    name = f"{name}_{i}"
                
                signal = cantools.database.Signal(
                    name=name,
                    start=sig.start_byte * 8,
                    length=sig.length * 8,
                    byte_order='little_endian' if sig.endian == 'little' else 'big_endian',
                    is_signed=sig.encoding.startswith('int'),
                    scale=sig.scale if sig.scale != 0 else 1.0,
                    offset=sig.offset,
                    minimum=0,
                    maximum=65535 if sig.length == 2 else 255,
                    unit='',
                    comment=f'Correlation: {sig.correlation:.3f}, Formula: {sig.formula}'
                )
                msg_signals.append(signal)
            
            msg = cantools.database.Message(
                frame_id=can_id,
                name=f"MSG_{can_id:03X}",
                length=8,
                signals=msg_signals
            )
            db.messages.append(msg)
        
        # Write DBC
        with open(filepath, 'w') as f:
            f.write(db.as_dbc_string())
        
        print(f"  ✓ Exported {len(messages)} messages to {filepath}")


# =============================================================================
# MAIN CLASS
# =============================================================================

class CANReverser:
    """Main class orchestrating all reverse engineering operations"""
    
    def __init__(self):
        self.loader = DataLoader()
        self.entropy_analyzer = EntropyAnalyzer()
        self.bitflip_detector = BitFlipDetector()
        self.dbc_matcher = DBCMatcher()
        self.correlator = Correlator()
        self.autonomous = AutonomousAnalyzer()
        self.exporter = Exporter()
        
        self.can_df = None
        self.obd_df = None
        self.candidates = []
        self.byte_stats = []
        self.dbc_matches = []
    
    def load_data(self, can_file: str, obd_file: Optional[str] = None,
                  max_rows: Optional[int] = None, sample_rate: float = 1.0):
        """Load CAN and optionally OBD data"""
        self.can_df = self.loader.load_gvret_can(can_file, max_rows, sample_rate)
        
        if obd_file:
            self.obd_df = self.loader.load_obd_session(obd_file)
    
    def analyze_entropy(self):
        """Run entropy analysis on all CAN IDs"""
        print("\n📊 Running entropy analysis...")
        
        self.byte_stats = []
        for can_id in self.can_df['id'].unique():
            stats = self.entropy_analyzer.analyze_can_id(self.can_df, can_id)
            self.byte_stats.extend(stats)
        
        # Summary
        classifications = {}
        for s in self.byte_stats:
            classifications[s.classification] = classifications.get(s.classification, 0) + 1
        
        print(f"\n  Byte classification summary:")
        for cls, count in sorted(classifications.items(), key=lambda x: -x[1]):
            print(f"    {cls}: {count}")
    
    def match_dbcs(self, manufacturers: Optional[List[str]] = None):
        """Match CAN data against known DBCs"""
        if manufacturers:
            dbc_names = [k for k in OPENDBC_FILES.keys() 
                        if any(m.lower() in k.lower() for m in manufacturers)]
        else:
            dbc_names = None
        
        self.dbc_matches = self.dbc_matcher.match_can_ids(self.can_df, dbc_names)
        
        if self.dbc_matches:
            print(f"\n  Top 10 signal matches:")
            for match in self.dbc_matches[:10]:
                print(f"    {match.dbc_name}: 0x{match.can_id:03X} → {match.signal_name}")
    
    def find_correlations(self, threshold: float = 0.7, min_rpm: int = 0):
        """Find correlations with OBD PIDs"""
        if self.obd_df is None:
            print("⚠️  No OBD data - using autonomous analysis")
            self.candidates = self.autonomous.analyze(self.can_df)
        else:
            self.candidates = self.correlator.find_correlations(
                self.can_df, self.obd_df, threshold, min_rpm
            )
    
    def visualize(self, mode: str = 'overview'):
        """Show visualizations"""
        vis = Visualizer(self.can_df, self.obd_df)
        
        if mode == 'overview' and self.candidates:
            vis.plot_correlation_overview(self.candidates)
        elif mode == 'timeseries' and self.candidates:
            vis.plot_all_correlations(self.candidates)
        elif mode == 'bypid' and self.candidates:
            vis.plot_correlations_by_pid(self.candidates)
        elif mode == 'entropy' and self.byte_stats:
            vis.plot_entropy_analysis(self.byte_stats)
        elif mode == 'dbc':
            # DBC verification mode - test existing DBCs against your data
            vis.plot_dbc_verification(self.dbc_matcher, 
                                      list(OPENDBC_FILES.keys()) if not self.dbc_matches 
                                      else list(set(m.dbc_name for m in self.dbc_matches)))
        elif mode == 'interactive':
            # Show first CAN ID
            can_id = self.can_df['id'].iloc[0]
            vis.plot_interactive_timeline(can_id)
    
    def export(self, csv_path: str = 'correlations.csv', dbc_path: str = 'discovered.dbc'):
        """Export results"""
        if self.candidates:
            print("\n📤 Exporting results...")
            self.exporter.to_csv(self.candidates, csv_path)
            self.exporter.to_dbc(self.candidates, dbc_path)


# =============================================================================
# CLI MAIN
# =============================================================================

def main():
    parser = argparse.ArgumentParser(
        description='Universal CAN Reverse Engineering Tool',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # With OBD reference data:
  python can_reverser.py canlog.csv session.csv --threshold 0.6

  # Autonomous mode (no OBD):
  python can_reverser.py canlog.csv --autonomous

  # Test Toyota DBCs:
  python can_reverser.py canlog.csv --match-dbc toyota

  # Verify existing DBCs against your data:
  python can_reverser.py canlog.csv session.csv --match-dbc toyota --visualize dbc

  # Large file with sampling (keep 20% of data):
  python can_reverser.py large_canlog.csv session.csv --sample-rate 0.2

  # Load only first 100K rows:
  python can_reverser.py large_canlog.csv session.csv --max-rows 100000

  # Full analysis with visualization:
  python can_reverser.py canlog.csv session.csv --visualize all --export
        """
    )
    
    parser.add_argument('canlog', help='CAN log file (GVRET CSV format)')
    parser.add_argument('obdlog', nargs='?', help='OBD session CSV file (optional)')
    parser.add_argument('--threshold', type=float, default=0.7, 
                        help='Correlation threshold (default: 0.7)')
    parser.add_argument('--min-rpm', type=int, default=0,
                        help='Minimum RPM filter (default: 0)')
    parser.add_argument('--autonomous', action='store_true',
                        help='Force autonomous mode (no OBD reference)')
    parser.add_argument('--match-dbc', nargs='*', metavar='MANUFACTURER',
                        help='Match against DBCs (toyota, honda, vw, gm, ford, etc)')
    parser.add_argument('--visualize', nargs='?', const='bypid',
                        choices=['overview', 'timeseries', 'bypid', 'entropy', 'dbc', 'interactive', 'all'],
                        help='Show visualizations (default: bypid - one window per PID, dbc = verify existing DBCs)')
    parser.add_argument('--export', action='store_true',
                        help='Export to CSV and DBC')
    parser.add_argument('--csv-out', default='correlations.csv',
                        help='Output CSV file (default: correlations.csv)')
    parser.add_argument('--dbc-out', default='discovered.dbc',
                        help='Output DBC file (default: discovered.dbc)')
    parser.add_argument('--max-rows', type=int, metavar='N',
                        help='Load only first N rows (for large files)')
    parser.add_argument('--sample-rate', type=float, default=1.0, metavar='RATE',
                        help='Sample rate 0.0-1.0 (e.g., 0.2 = keep 20%%, for large files)')
    
    args = parser.parse_args()
    
    print("\n" + "=" * 70)
    print("  CAN Reverse Engineering Tool - Universal Signal Identifier")
    print("=" * 70 + "\n")
    
    # Initialize
    reverser = CANReverser()
    
    # Load data
    if args.autonomous:
        reverser.load_data(args.canlog, max_rows=args.max_rows, 
                          sample_rate=args.sample_rate)
    else:
        reverser.load_data(args.canlog, args.obdlog, max_rows=args.max_rows,
                          sample_rate=args.sample_rate)
    
    # Entropy analysis
    reverser.analyze_entropy()
    
    # DBC matching
    if args.match_dbc is not None:
        manufacturers = args.match_dbc if args.match_dbc else None
        reverser.match_dbcs(manufacturers)
    
    # Correlation analysis
    reverser.find_correlations(args.threshold, args.min_rpm)
    
    # Visualization
    if args.visualize:
        if args.visualize == 'all':
            reverser.visualize('bypid')
            reverser.visualize('overview')
            reverser.visualize('entropy')
            if args.match_dbc is not None:
                reverser.visualize('dbc')
        else:
            reverser.visualize(args.visualize)
    
    # Export
    if args.export:
        reverser.export(args.csv_out, args.dbc_out)
    
    print("\n✅ Analysis complete!\n")


if __name__ == '__main__':
    main()
