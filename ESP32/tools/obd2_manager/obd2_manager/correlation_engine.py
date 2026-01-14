"""
CAN Signal Correlation Engine
Based on can_reverser.py methodology
"""

import numpy as np
import pandas as pd
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple
from scipy import stats
import warnings

warnings.filterwarnings('ignore')


@dataclass
class ByteStats:
    """Statistics for a single byte position"""
    can_id: int
    byte_pos: int
    mean: float
    std: float
    min_val: int
    max_val: int
    unique_count: int
    entropy: float
    update_rate_hz: float
    classification: str  # 'sensor', 'counter', 'checksum', 'status', 'unused'


@dataclass
class CorrelationResult:
    """Result of correlation analysis"""
    can_id: int
    byte_pos: int
    length: int  # 1 or 2
    endian: str
    pid_name: str
    correlation: float
    scale: float
    offset: float
    formula: str
    confidence: float
    samples: int


class CorrelationEngine:
    """
    Analyze CAN signals and correlate with OBD PIDs.
    Uses entropy analysis, correlation, and heuristics.
    """
    
    # Known PID characteristics for heuristic matching
    PID_HINTS = {
        'rpm': {'min': 0, 'max': 8000, 'bytes': 2, 'scale': 0.25},
        'speed': {'min': 0, 'max': 255, 'bytes': 1, 'scale': 1.0},
        'throttle': {'min': 0, 'max': 100, 'bytes': 1, 'scale': 100/255},
        'load': {'min': 0, 'max': 100, 'bytes': 1, 'scale': 100/255},
        'coolant': {'min': -40, 'max': 215, 'bytes': 1, 'offset': -40},
        'voltage': {'min': 0, 'max': 20, 'bytes': 2, 'scale': 0.001},
    }
    
    def __init__(self):
        self.byte_stats: Dict[Tuple[int, int], ByteStats] = {}
        self.correlations: List[CorrelationResult] = []
    
    @staticmethod
    def shannon_entropy(data: np.ndarray) -> float:
        """Calculate Shannon entropy of byte values"""
        if len(data) == 0:
            return 0.0
        
        # Count unique values
        _, counts = np.unique(data, return_counts=True)
        probabilities = counts / len(data)
        
        # Shannon entropy
        entropy = -np.sum(probabilities * np.log2(probabilities + 1e-10))
        return entropy
    
    @staticmethod
    def classify_byte(stats: ByteStats) -> str:
        """
        Classify byte based on statistics.
        
        Classifications:
        - 'unused': No variation (entropy ~0)
        - 'counter': Sequential pattern (entropy ~8, linear trend)
        - 'checksum': High entropy, correlated with other bytes
        - 'status': Low unique count, discrete values
        - 'sensor': Continuous variation
        """
        if stats.entropy < 0.5 and stats.unique_count <= 2:
            return 'unused'
        elif stats.unique_count <= 8 and stats.entropy < 2.5:
            return 'status'
        elif stats.entropy > 7.0:
            return 'counter'  # or checksum
        elif stats.std > 10 and stats.unique_count > 20:
            return 'sensor'
        else:
            return 'unknown'
    
    def analyze_can_id(self, can_df: pd.DataFrame, can_id: int) -> List[ByteStats]:
        """
        Analyze all bytes of a CAN ID.
        
        Args:
            can_df: CAN log DataFrame
            can_id: CAN ID to analyze
            
        Returns:
            List of ByteStats for each byte position
        """
        filtered = can_df[can_df['id'] == can_id]
        if len(filtered) < 10:
            return []
        
        results = []
        duration_s = (filtered['timestamp_us'].max() - filtered['timestamp_us'].min()) / 1_000_000
        update_rate = len(filtered) / max(duration_s, 0.001)
        
        for byte_pos in range(8):
            col = f'd{byte_pos}'
            if col not in filtered.columns:
                continue
            
            data = filtered[col].values
            
            stats = ByteStats(
                can_id=can_id,
                byte_pos=byte_pos,
                mean=float(np.mean(data)),
                std=float(np.std(data)),
                min_val=int(np.min(data)),
                max_val=int(np.max(data)),
                unique_count=len(np.unique(data)),
                entropy=self.shannon_entropy(data),
                update_rate_hz=update_rate,
                classification='unknown'
            )
            stats.classification = self.classify_byte(stats)
            
            self.byte_stats[(can_id, byte_pos)] = stats
            results.append(stats)
        
        return results
    
    def correlate_with_pid(self,
                           can_df: pd.DataFrame,
                           obd_df: pd.DataFrame,
                           can_id: int,
                           pid_column: str,
                           byte_positions: Optional[List[int]] = None) -> List[CorrelationResult]:
        """
        Correlate CAN bytes with an OBD PID.
        
        Args:
            can_df: CAN log DataFrame
            obd_df: OBD session DataFrame with pid_column
            can_id: CAN ID to analyze
            pid_column: Column name in obd_df (e.g., 'rpm', 'speed')
            byte_positions: Byte positions to check (default: all)
            
        Returns:
            List of correlation results, sorted by correlation strength
        """
        if pid_column not in obd_df.columns:
            return []
        
        can_filtered = can_df[can_df['id'] == can_id].copy()
        if len(can_filtered) < 10:
            return []
        
        if byte_positions is None:
            byte_positions = list(range(8))
        
        # Resample OBD to CAN timestamps
        obd_sorted = obd_df[['timestamp_us', pid_column]].dropna().sort_values('timestamp_us')
        can_sorted = can_filtered.sort_values('timestamp_us')
        
        merged = pd.merge_asof(
            can_sorted,
            obd_sorted,
            on='timestamp_us',
            direction='nearest',
            tolerance=100000  # 100ms tolerance
        )
        
        merged = merged.dropna(subset=[pid_column])
        if len(merged) < 10:
            return []
        
        pid_values = merged[pid_column].values
        results = []
        
        # Test single bytes
        for pos in byte_positions:
            col = f'd{pos}'
            if col not in merged.columns:
                continue
            
            byte_values = merged[col].values
            
            # Skip if no variation
            if np.std(byte_values) < 1 or np.std(pid_values) < 0.1:
                continue
            
            # Pearson correlation
            corr, p_value = stats.pearsonr(byte_values, pid_values)
            
            if abs(corr) > 0.7:
                # Calculate linear regression for scale/offset
                slope, intercept, r_value, _, _ = stats.linregress(byte_values, pid_values)
                
                results.append(CorrelationResult(
                    can_id=can_id,
                    byte_pos=pos,
                    length=1,
                    endian='n/a',
                    pid_name=pid_column,
                    correlation=corr,
                    scale=slope,
                    offset=intercept,
                    formula=f"{pid_column} = {slope:.4f} * byte[{pos}] + {intercept:.2f}",
                    confidence=abs(corr) * (1 - p_value),
                    samples=len(merged)
                ))
        
        # Test 16-bit pairs
        for hi in byte_positions:
            for lo in byte_positions:
                if hi == lo:
                    continue
                
                hi_col, lo_col = f'd{hi}', f'd{lo}'
                if hi_col not in merged.columns or lo_col not in merged.columns:
                    continue
                
                # Big endian: high byte * 256 + low byte
                word_be = merged[hi_col] * 256 + merged[lo_col]
                corr_be, p_be = stats.pearsonr(word_be.values, pid_values)
                
                # Little endian: low byte * 256 + high byte
                word_le = merged[lo_col] * 256 + merged[hi_col]
                corr_le, p_le = stats.pearsonr(word_le.values, pid_values)
                
                # Use better correlation
                if abs(corr_be) > abs(corr_le) and abs(corr_be) > 0.8:
                    slope, intercept, r_value, _, _ = stats.linregress(word_be.values, pid_values)
                    results.append(CorrelationResult(
                        can_id=can_id,
                        byte_pos=hi,
                        length=2,
                        endian='big',
                        pid_name=pid_column,
                        correlation=corr_be,
                        scale=slope,
                        offset=intercept,
                        formula=f"{pid_column} = {slope:.6f} * (byte[{hi}]*256 + byte[{lo}]) + {intercept:.2f}",
                        confidence=abs(corr_be) * (1 - p_be),
                        samples=len(merged)
                    ))
                elif abs(corr_le) > 0.8:
                    slope, intercept, r_value, _, _ = stats.linregress(word_le.values, pid_values)
                    results.append(CorrelationResult(
                        can_id=can_id,
                        byte_pos=lo,
                        length=2,
                        endian='little',
                        pid_name=pid_column,
                        correlation=corr_le,
                        scale=slope,
                        offset=intercept,
                        formula=f"{pid_column} = {slope:.6f} * (byte[{lo}]*256 + byte[{hi}]) + {intercept:.2f}",
                        confidence=abs(corr_le) * (1 - p_le),
                        samples=len(merged)
                    ))
        
        # Sort by correlation strength
        results.sort(key=lambda x: abs(x.correlation), reverse=True)
        self.correlations.extend(results)
        
        return results
    
    def auto_correlate(self,
                       can_df: pd.DataFrame,
                       obd_df: pd.DataFrame,
                       min_correlation: float = 0.8) -> List[CorrelationResult]:
        """
        Automatically find correlations between all CAN IDs and OBD PIDs.
        
        Args:
            can_df: CAN log DataFrame
            obd_df: OBD session DataFrame
            min_correlation: Minimum correlation threshold
            
        Returns:
            List of all significant correlations found
        """
        # OBD columns to correlate
        pid_columns = ['rpm', 'speed', 'throttle', 'load', 'maf', 
                       'coolant', 'voltage', 'fuel_level']
        
        # Filter to columns that exist
        pid_columns = [c for c in pid_columns if c in obd_df.columns]
        
        # Get unique CAN IDs (exclude OBD response IDs)
        can_ids = can_df['id'].unique()
        can_ids = [cid for cid in can_ids if cid not in [0x7E8, 0x7E9, 0x7EA, 0x7DF]]
        
        all_results = []
        total = len(can_ids) * len(pid_columns)
        done = 0
        
        print(f"🔍 Auto-correlating {len(can_ids)} CAN IDs with {len(pid_columns)} PIDs...")
        
        for can_id in can_ids:
            for pid_col in pid_columns:
                results = self.correlate_with_pid(can_df, obd_df, can_id, pid_col)
                
                # Keep only strong correlations
                for r in results:
                    if abs(r.correlation) >= min_correlation:
                        all_results.append(r)
                
                done += 1
            
            # Progress update every 10 IDs
            if done % (len(pid_columns) * 10) == 0:
                print(f"   Progress: {done}/{total} ({100*done/total:.0f}%)")
        
        # Sort by confidence
        all_results.sort(key=lambda x: x.confidence, reverse=True)
        
        print(f"✓ Found {len(all_results)} correlations (≥{min_correlation*100:.0f}%)")
        
        return all_results
    
    def get_best_matches(self, 
                         results: List[CorrelationResult],
                         per_pid: int = 3) -> Dict[str, List[CorrelationResult]]:
        """
        Get best matches grouped by PID.
        
        Args:
            results: List of correlation results
            per_pid: Maximum matches per PID
            
        Returns:
            Dict mapping PID name to list of best matches
        """
        by_pid: Dict[str, List[CorrelationResult]] = {}
        
        for r in results:
            if r.pid_name not in by_pid:
                by_pid[r.pid_name] = []
            
            if len(by_pid[r.pid_name]) < per_pid:
                by_pid[r.pid_name].append(r)
        
        return by_pid
    
    def export_to_dbc(self, 
                      results: List[CorrelationResult],
                      filepath: str,
                      db_name: str = "AUTO_DISCOVERED"):
        """
        Export correlations to DBC format.
        
        Args:
            results: Correlation results to export
            filepath: Output DBC file path
            db_name: Database name
        """
        lines = [
            'VERSION ""',
            '',
            'NS_ :',
            '',
            'BS_:',
            '',
            'BU_:',
            '',
        ]
        
        # Group by CAN ID
        by_id: Dict[int, List[CorrelationResult]] = {}
        for r in results:
            if r.can_id not in by_id:
                by_id[r.can_id] = []
            by_id[r.can_id].append(r)
        
        # Generate messages
        for can_id, signals in by_id.items():
            msg_name = f"MSG_{can_id:03X}"
            lines.append(f'BO_ {can_id} {msg_name}: 8 Vector__XXX')
            
            for sig in signals:
                sig_name = f"{sig.pid_name.upper()}_{sig.byte_pos}"
                start_bit = sig.byte_pos * 8
                length = sig.length * 8
                byte_order = 0 if sig.endian == 'little' else 1
                
                lines.append(
                    f' SG_ {sig_name} : {start_bit}|{length}@{byte_order}+ '
                    f'({sig.scale},{sig.offset}) [0|0] "" Vector__XXX'
                )
            
            lines.append('')
        
        with open(filepath, 'w') as f:
            f.write('\n'.join(lines))
        
        print(f"✓ Exported {len(results)} signals to {filepath}")
