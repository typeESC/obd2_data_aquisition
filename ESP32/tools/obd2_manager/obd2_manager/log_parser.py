"""
Log file parser for OBD sessions and CAN sniffer logs
"""

import os
from pathlib import Path
from typing import Optional, Tuple, List, Dict
import pandas as pd
import numpy as np


class LogParser:
    """Parse OBD session and CAN sniffer log files"""
    
    # OBD session CSV columns (extended format)
    OBD_COLUMNS = [
        'timestamp_us', 'rpm', 'speed', 'throttle', 'load', 'maf',
        'coolant', 'manifold', 'fuel_level', 'intake_temp', 'runtime',
        'voltage', 'oil_temp', 'fuel_trim_short', 'fuel_trim_long', 
        'distance', 'ambient', 'timing',
        'mil_status', 'dtc_count', 'valid_mask'
    ]
    
    # GVRET CAN log columns
    CAN_COLUMNS = [
        'timestamp_us', 'id', 'extended', 'bus', 'len',
        'd0', 'd1', 'd2', 'd3', 'd4', 'd5', 'd6', 'd7'
    ]
    
    @staticmethod
    def detect_file_type(filepath: str) -> str:
        """
        Detect if file is OBD session or CAN log.
        Returns 'obd', 'can', or 'unknown'.
        """
        name = os.path.basename(filepath).lower()
        
        if name.startswith('session_'):
            return 'obd'
        elif name.startswith('canlog_'):
            return 'can'
        
        # Check by content
        try:
            with open(filepath, 'r') as f:
                header = f.readline().lower()
                if 'rpm' in header and 'speed' in header:
                    return 'obd'
                elif 'timestamp_us' in header and ('id' in header or 'identifier' in header):
                    return 'can'
        except:
            pass
        
        return 'unknown'
    
    @staticmethod
    def load_obd_session(filepath: str, 
                         max_rows: Optional[int] = None,
                         drop_incomplete: bool = True) -> pd.DataFrame:
        """
        Load OBD session CSV file.
        
        Args:
            filepath: Path to session CSV
            max_rows: Maximum rows to load
            drop_incomplete: Remove rows with missing data
            
        Returns:
            DataFrame with OBD telemetry data
        """
        print(f"📂 Loading OBD session: {filepath}")
        
        df = pd.read_csv(filepath, nrows=max_rows, on_bad_lines='skip')
        df.columns = df.columns.str.lower().str.strip()
        
        original_len = len(df)
        
        # Remove rows with missing data
        if drop_incomplete:
            df = df.dropna()
            dropped = original_len - len(df)
            if dropped > 0:
                print(f"   ⚠️ Removed {dropped} rows with missing data")
        
        # Replace infinities with NaN and drop them too
        df = df.replace([np.inf, -np.inf], np.nan)
        df = df.dropna()
        
        if len(df) == 0:
            print("   ❌ No valid data after cleanup!")
            # Return empty dataframe with expected columns
            df = pd.DataFrame(columns=['timestamp_us', 'time_s'])
            return df
        
        # Ensure timestamp column
        if 'timestamp_us' not in df.columns:
            if 'timestamp' in df.columns:
                df['timestamp_us'] = df['timestamp']
            else:
                df['timestamp_us'] = np.arange(len(df)) * 50000  # 50ms default
        
        # Convert to numeric, coercing errors
        df['timestamp_us'] = pd.to_numeric(df['timestamp_us'], errors='coerce')
        df = df.dropna(subset=['timestamp_us'])
        df['timestamp_us'] = df['timestamp_us'].astype(np.int64)
        
        # Calculate relative time in seconds
        if len(df) > 0:
            t0 = df['timestamp_us'].iloc[0]
            df['time_s'] = (df['timestamp_us'] - t0) / 1_000_000
            print(f"   ✓ Loaded {len(df):,} rows, duration: {df['time_s'].max():.1f}s")
        else:
            df['time_s'] = 0.0
            print(f"   ✓ Loaded {len(df):,} rows")
        
        return df
    
    @staticmethod
    def load_can_log(filepath: str,
                     max_rows: Optional[int] = None,
                     sample_rate: float = 1.0,
                     drop_incomplete: bool = True) -> pd.DataFrame:
        """
        Load GVRET-format CAN log.
        
        Args:
            filepath: Path to CAN log CSV
            max_rows: Maximum rows to load
            sample_rate: Fraction of data to keep (0.0-1.0)
            drop_incomplete: Remove rows with missing data
            
        Returns:
            DataFrame with CAN messages
        """
        print(f"📂 Loading CAN log: {filepath}")
        
        # Check file size
        file_size_mb = os.path.getsize(filepath) / (1024 * 1024)
        print(f"   File size: {file_size_mb:.1f} MB")
        
        df = pd.read_csv(filepath, nrows=max_rows, on_bad_lines='skip')
        df.columns = df.columns.str.lower().str.strip().str.replace(' ', '_')
        
        original_len = len(df)
        
        # Remove rows with missing data (only on key columns initially)
        if drop_incomplete:
            # Don't drop on all columns yet - wait until we normalize column names
            pass
        
        # Replace infinities with NaN
        df = df.replace([np.inf, -np.inf], np.nan)
        
        # Normalize column names
        col_map = {
            'time_stamp': 'timestamp_us',
            'identifier': 'id',
            'can_id': 'id',
        }
        df.rename(columns=col_map, inplace=True)
        
        # Handle timestamp - coerce errors
        if 'timestamp_us' not in df.columns:
            if 'timestamp_ms' in df.columns:
                df['timestamp_ms'] = pd.to_numeric(df['timestamp_ms'], errors='coerce')
                df['timestamp_us'] = (df['timestamp_ms'] * 1000)
            else:
                df['timestamp_us'] = np.arange(len(df)) * 1000
        
        # Convert timestamp to numeric, coercing errors
        df['timestamp_us'] = pd.to_numeric(df['timestamp_us'], errors='coerce')
        
        # Normalize CAN ID to integer with error handling
        if 'id' in df.columns:
            def safe_parse_id(x):
                try:
                    if pd.isna(x):
                        return np.nan
                    if isinstance(x, str):
                        x = x.strip()
                        if x.startswith('0x') or x.startswith('0X'):
                            return int(x, 16)
                        elif all(c in '0123456789abcdefABCDEF' for c in x):
                            return int(x, 16)
                        else:
                            return int(x)
                    return int(x)
                except (ValueError, TypeError):
                    return np.nan
            
            df['id'] = df['id'].apply(safe_parse_id)
        
        # Drop rows with missing timestamp or id
        df = df.dropna(subset=['timestamp_us', 'id'] if 'id' in df.columns else ['timestamp_us'])
        
        dropped = original_len - len(df)
        if dropped > 0:
            print(f"   ⚠️ Removed {dropped} rows with missing/invalid data")
        
        if len(df) == 0:
            print("   ❌ No valid data after cleanup!")
            df = pd.DataFrame(columns=['timestamp_us', 'time_s', 'id'])
            return df
        
        df['timestamp_us'] = df['timestamp_us'].astype(np.int64)
        df['id'] = df['id'].astype(int)
        
        # Extract data bytes
        data_cols = ['d0', 'd1', 'd2', 'd3', 'd4', 'd5', 'd6', 'd7']
        for i, col in enumerate(data_cols):
            if col not in df.columns:
                # Try alternative naming
                alt_names = [f'data{i}', f'byte{i}', f'b{i}']
                for alt in alt_names:
                    if alt in df.columns:
                        df[col] = pd.to_numeric(df[alt], errors='coerce').fillna(0)
                        break
                else:
                    df[col] = 0
            else:
                df[col] = pd.to_numeric(df[col], errors='coerce').fillna(0)
        
        # Sample if requested
        if sample_rate < 1.0:
            sample_original = len(df)
            df = df.sample(frac=sample_rate).sort_values('timestamp_us').reset_index(drop=True)
            print(f"   Sampled {len(df):,} from {sample_original:,} rows ({sample_rate*100:.0f}%)")
        
        # Calculate relative time
        if len(df) > 0:
            t0 = df['timestamp_us'].iloc[0]
            df['time_s'] = (df['timestamp_us'] - t0) / 1_000_000
            print(f"   ✓ Loaded {len(df):,} rows, {df['id'].nunique()} unique IDs, "
                  f"duration: {df['time_s'].max():.1f}s")
        else:
            df['time_s'] = 0.0
            print(f"   ✓ Loaded {len(df):,} rows")
        
        return df
    
    @staticmethod
    def load_paired_logs(obd_path: str, 
                         can_path: str,
                         max_rows: Optional[int] = None) -> Tuple[pd.DataFrame, pd.DataFrame]:
        """
        Load paired OBD and CAN logs, synchronizing timestamps.
        
        Args:
            obd_path: Path to OBD session CSV
            can_path: Path to CAN log CSV
            max_rows: Maximum rows per file
            
        Returns:
            Tuple of (obd_df, can_df) with synchronized timestamps
        """
        obd_df = LogParser.load_obd_session(obd_path, max_rows)
        can_df = LogParser.load_can_log(can_path, max_rows)
        
        # Find overlapping time range
        obd_start = obd_df['timestamp_us'].min()
        obd_end = obd_df['timestamp_us'].max()
        can_start = can_df['timestamp_us'].min()
        can_end = can_df['timestamp_us'].max()
        
        overlap_start = max(obd_start, can_start)
        overlap_end = min(obd_end, can_end)
        
        if overlap_start >= overlap_end:
            print("⚠️  Warning: No timestamp overlap between files!")
            print(f"   OBD: {obd_start} - {obd_end}")
            print(f"   CAN: {can_start} - {can_end}")
        else:
            # Filter to overlapping region
            obd_df = obd_df[(obd_df['timestamp_us'] >= overlap_start) & 
                            (obd_df['timestamp_us'] <= overlap_end)].copy()
            can_df = can_df[(can_df['timestamp_us'] >= overlap_start) & 
                            (can_df['timestamp_us'] <= overlap_end)].copy()
            
            # Recalculate relative time from common start
            obd_df['time_s'] = (obd_df['timestamp_us'] - overlap_start) / 1_000_000
            can_df['time_s'] = (can_df['timestamp_us'] - overlap_start) / 1_000_000
            
            duration = (overlap_end - overlap_start) / 1_000_000
            print(f"✓ Synchronized: {duration:.1f}s overlap")
            print(f"   OBD: {len(obd_df):,} rows, CAN: {len(can_df):,} rows")
        
        return obd_df, can_df
    
    @staticmethod
    def get_can_signals(can_df: pd.DataFrame, 
                        can_id: int,
                        byte_positions: List[int] = None) -> pd.DataFrame:
        """
        Extract signal values for a specific CAN ID.
        
        Args:
            can_df: CAN log DataFrame
            can_id: CAN ID to filter
            byte_positions: List of byte positions to extract (default: all)
            
        Returns:
            DataFrame with timestamp and byte values
        """
        filtered = can_df[can_df['id'] == can_id].copy()
        
        if byte_positions is None:
            byte_positions = list(range(8))
        
        cols = ['timestamp_us', 'time_s']
        for pos in byte_positions:
            cols.append(f'd{pos}')
        
        return filtered[cols].reset_index(drop=True)
    
    @staticmethod
    def compute_16bit_signals(can_df: pd.DataFrame,
                              can_id: int,
                              byte_pairs: List[Tuple[int, int]] = None,
                              endian: str = 'big') -> pd.DataFrame:
        """
        Compute 16-bit values from byte pairs.
        
        Args:
            can_df: CAN log DataFrame
            can_id: CAN ID to filter
            byte_pairs: List of (high_byte, low_byte) tuples
            endian: 'big' or 'little'
            
        Returns:
            DataFrame with 16-bit signal values
        """
        filtered = can_df[can_df['id'] == can_id].copy()
        
        if byte_pairs is None:
            byte_pairs = [(0, 1), (2, 3), (4, 5), (6, 7)]
        
        result = filtered[['timestamp_us', 'time_s']].copy()
        
        for hi, lo in byte_pairs:
            col_name = f'word_{hi}_{lo}'
            if endian == 'big':
                result[col_name] = filtered[f'd{hi}'] * 256 + filtered[f'd{lo}']
            else:
                result[col_name] = filtered[f'd{lo}'] * 256 + filtered[f'd{hi}']
        
        return result.reset_index(drop=True)
    
    @staticmethod
    def resample_to_obd(can_signals: pd.DataFrame,
                        obd_df: pd.DataFrame,
                        method: str = 'nearest') -> pd.DataFrame:
        """
        Resample CAN signals to match OBD timestamps.
        
        Args:
            can_signals: CAN signal DataFrame with timestamp_us
            obd_df: OBD DataFrame with timestamp_us
            method: Resampling method ('nearest', 'linear')
            
        Returns:
            CAN signals resampled to OBD timestamps
        """
        # Use merge_asof for nearest timestamp matching
        can_signals = can_signals.sort_values('timestamp_us')
        obd_times = obd_df[['timestamp_us', 'time_s']].sort_values('timestamp_us')
        
        result = pd.merge_asof(
            obd_times,
            can_signals,
            on='timestamp_us',
            direction='nearest',
            suffixes=('_obd', '')
        )
        
        return result
