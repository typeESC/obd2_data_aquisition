#!/usr/bin/env python3
"""
CAN Bus Log Analyzer for OBD2 Data Acquisition Project
======================================================

Analyzes GVRET CSV format CAN logs captured by ESP32 sniffer.
Helps with reverse engineering CAN messages by identifying patterns,
correlating with known OBD data, and finding dynamic bytes.

Supports:
- Loading GVRET CSV format logs (SavvyCAN compatible)
- Identifying unique CAN IDs and their frequencies
- Finding bytes that change vs remain constant
- Correlating with OBD PID data (RPM, speed, etc.)
- Exporting filtered logs for SavvyCAN
- Generating analysis reports

Author: OBD2 Data Acquisition Project
Date: 2025
"""

import argparse
import csv
import os
import sys
from collections import defaultdict
from dataclasses import dataclass, field
from datetime import datetime
from typing import Dict, List, Optional, Tuple
import json

# ============================================================================
# DATA STRUCTURES
# ============================================================================

@dataclass
class CANMessage:
    """Represents a single CAN message from the log"""
    timestamp_us: int
    can_id: int
    extended: bool
    direction: str  # 'Rx' or 'Tx'
    bus: int
    dlc: int
    data: bytes
    
    @classmethod
    def from_gvret_row(cls, row: List[str]) -> 'CANMessage':
        """Parse a GVRET CSV row into a CANMessage"""
        # GVRET format: Time Stamp,ID,Extended,Dir,Bus,LEN,D1,D2,D3,D4,D5,D6,D7,D8
        timestamp = int(row[0])
        can_id = int(row[1], 16)
        extended = row[2].lower() == 'true'
        direction = row[3]
        bus = int(row[4])
        dlc = int(row[5])
        
        data_bytes = []
        for i in range(6, 14):
            if i < len(row) and row[i]:
                data_bytes.append(int(row[i], 16))
            else:
                data_bytes.append(0)
        
        return cls(
            timestamp_us=timestamp,
            can_id=can_id,
            extended=extended,
            direction=direction,
            bus=bus,
            dlc=dlc,
            data=bytes(data_bytes[:dlc])
        )

@dataclass
class CANIDStats:
    """Statistics for a specific CAN ID"""
    can_id: int
    count: int = 0
    first_seen: int = 0
    last_seen: int = 0
    interval_min_us: int = 0
    interval_max_us: int = 0
    interval_avg_us: float = 0
    dlc: int = 0
    
    # For each byte position, track min/max/unique values
    byte_stats: List[Dict] = field(default_factory=list)
    
    # Sample messages
    samples: List[CANMessage] = field(default_factory=list)
    
    def __post_init__(self):
        if not self.byte_stats:
            self.byte_stats = [
                {'min': 255, 'max': 0, 'unique': set(), 'changes': 0}
                for _ in range(8)
            ]

@dataclass 
class OBDData:
    """OBD telemetry data for correlation"""
    timestamp_ms: int
    rpm: float = 0
    speed: int = 0
    coolant_temp: int = 0
    engine_load: float = 0
    throttle_pos: float = 0
    maf_rate: float = 0

# ============================================================================
# LOG PARSER
# ============================================================================

class CANLogParser:
    """Parses GVRET CSV format CAN logs"""
    
    def __init__(self):
        self.messages: List[CANMessage] = []
        self.id_stats: Dict[int, CANIDStats] = {}
        self.obd_data: List[OBDData] = []
        
    def load_gvret_csv(self, filepath: str) -> int:
        """
        Load a GVRET format CSV file.
        Returns number of messages loaded.
        """
        print(f"Loading {filepath}...")
        count = 0
        
        with open(filepath, 'r', newline='', encoding='utf-8') as f:
            reader = csv.reader(f)
            
            # Skip header
            header = next(reader, None)
            if header and 'Time Stamp' not in header[0]:
                # No header, rewind
                f.seek(0)
                reader = csv.reader(f)
            
            for row in reader:
                if len(row) < 6:
                    continue
                    
                try:
                    msg = CANMessage.from_gvret_row(row)
                    self.messages.append(msg)
                    self._update_stats(msg)
                    count += 1
                except (ValueError, IndexError) as e:
                    # Skip malformed rows
                    continue
        
        print(f"Loaded {count} messages from {len(self.id_stats)} unique CAN IDs")
        return count
    
    def load_obd_csv(self, filepath: str) -> int:
        """
        Load OBD telemetry CSV for correlation.
        Returns number of records loaded.
        """
        print(f"Loading OBD data from {filepath}...")
        count = 0
        
        with open(filepath, 'r', newline='', encoding='utf-8') as f:
            reader = csv.DictReader(f)
            
            for row in reader:
                try:
                    obd = OBDData(
                        timestamp_ms=int(row.get('timestamp', 0)),
                        rpm=float(row.get('rpm', 0)),
                        speed=int(row.get('speed', 0)),
                        coolant_temp=int(row.get('coolant', 0)),
                        engine_load=float(row.get('load', 0)),
                        throttle_pos=float(row.get('throttle', 0)),
                        maf_rate=float(row.get('maf', 0))
                    )
                    self.obd_data.append(obd)
                    count += 1
                except (ValueError, KeyError):
                    continue
        
        print(f"Loaded {count} OBD records")
        return count
    
    def _update_stats(self, msg: CANMessage):
        """Update statistics for a CAN ID"""
        if msg.can_id not in self.id_stats:
            self.id_stats[msg.can_id] = CANIDStats(
                can_id=msg.can_id,
                first_seen=msg.timestamp_us,
                dlc=msg.dlc
            )
        
        stats = self.id_stats[msg.can_id]
        stats.count += 1
        stats.last_seen = msg.timestamp_us
        
        # Update byte statistics
        for i, byte_val in enumerate(msg.data):
            if i < len(stats.byte_stats):
                bs = stats.byte_stats[i]
                if byte_val < bs['min']:
                    bs['min'] = byte_val
                if byte_val > bs['max']:
                    bs['max'] = byte_val
                
                old_len = len(bs['unique'])
                bs['unique'].add(byte_val)
                if len(bs['unique']) > old_len:
                    bs['changes'] += 1
        
        # Keep up to 5 sample messages
        if len(stats.samples) < 5:
            stats.samples.append(msg)

# ============================================================================
# ANALYSIS FUNCTIONS
# ============================================================================

def analyze_frequencies(parser: CANLogParser) -> List[Tuple[int, int, float]]:
    """
    Analyze message frequencies for each CAN ID.
    Returns list of (can_id, count, frequency_hz) sorted by count descending.
    """
    results = []
    
    for can_id, stats in parser.id_stats.items():
        duration_s = (stats.last_seen - stats.first_seen) / 1_000_000
        freq_hz = stats.count / duration_s if duration_s > 0 else 0
        results.append((can_id, stats.count, freq_hz))
    
    return sorted(results, key=lambda x: x[1], reverse=True)


def find_dynamic_bytes(parser: CANLogParser, min_changes: int = 5) -> Dict[int, List[int]]:
    """
    Find byte positions that change significantly for each CAN ID.
    Returns dict mapping CAN ID to list of dynamic byte positions.
    """
    dynamic = {}
    
    for can_id, stats in parser.id_stats.items():
        dynamic_positions = []
        
        for i, bs in enumerate(stats.byte_stats[:stats.dlc]):
            if bs['changes'] >= min_changes:
                dynamic_positions.append(i)
        
        if dynamic_positions:
            dynamic[can_id] = dynamic_positions
    
    return dynamic


def find_rpm_candidates(parser: CANLogParser, threshold: float = 0.8) -> List[Tuple[int, int, int, float]]:
    """
    Find CAN ID + byte position combinations that might represent RPM.
    RPM typically uses 2 bytes (big or little endian).
    Returns list of (can_id, byte_pos, byte_pos+1, correlation) sorted by correlation.
    """
    if not parser.obd_data:
        print("No OBD data loaded - cannot correlate RPM")
        return []
    
    candidates = []
    
    for can_id, stats in parser.id_stats.items():
        if stats.dlc < 2:
            continue
            
        # Skip standard OBD response IDs
        if 0x7E8 <= can_id <= 0x7EF:
            continue
        
        # Check each 2-byte pair
        for byte_pos in range(stats.dlc - 1):
            # Collect values at this position
            values_be = []  # Big endian
            values_le = []  # Little endian
            
            for msg in parser.messages:
                if msg.can_id != can_id:
                    continue
                if msg.dlc <= byte_pos + 1:
                    continue
                
                be_val = (msg.data[byte_pos] << 8) | msg.data[byte_pos + 1]
                le_val = (msg.data[byte_pos + 1] << 8) | msg.data[byte_pos]
                values_be.append((msg.timestamp_us, be_val))
                values_le.append((msg.timestamp_us, le_val))
            
            # Simple correlation check with OBD RPM data
            # (In a real implementation, use proper time-based correlation)
            if values_be:
                be_range = max(v[1] for v in values_be) - min(v[1] for v in values_be)
                le_range = max(v[1] for v in values_le) - min(v[1] for v in values_le)
                
                # RPM typically ranges 0-8000, so we expect significant range
                if be_range > 500:
                    candidates.append((can_id, byte_pos, byte_pos + 1, be_range / 8000.0))
    
    return sorted(candidates, key=lambda x: x[3], reverse=True)[:20]


def find_speed_candidates(parser: CANLogParser) -> List[Tuple[int, int, float]]:
    """
    Find CAN ID + byte position combinations that might represent vehicle speed.
    Speed is typically 1 byte (0-255 km/h).
    Returns list of (can_id, byte_pos, correlation) sorted by likelihood.
    """
    candidates = []
    
    for can_id, stats in parser.id_stats.items():
        # Skip standard OBD response IDs
        if 0x7E8 <= can_id <= 0x7EF:
            continue
        
        for byte_pos in range(stats.dlc):
            bs = stats.byte_stats[byte_pos]
            
            # Speed typically:
            # - Has some dynamic range (car moving/stopping)
            # - Range is 0-200 or so
            # - Not usually 255 max
            if bs['changes'] > 3 and bs['max'] < 220 and bs['max'] > 0:
                score = bs['changes'] / max(stats.count, 1)
                candidates.append((can_id, byte_pos, score))
    
    return sorted(candidates, key=lambda x: x[2], reverse=True)[:20]

# ============================================================================
# EXPORT FUNCTIONS
# ============================================================================

def export_filtered_gvret(parser: CANLogParser, output_path: str, 
                          can_ids: Optional[List[int]] = None,
                          exclude_ids: Optional[List[int]] = None) -> int:
    """
    Export filtered messages to GVRET CSV format.
    Returns number of messages exported.
    """
    count = 0
    
    with open(output_path, 'w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        writer.writerow(['Time Stamp', 'ID', 'Extended', 'Dir', 'Bus', 'LEN',
                         'D1', 'D2', 'D3', 'D4', 'D5', 'D6', 'D7', 'D8'])
        
        for msg in parser.messages:
            if can_ids and msg.can_id not in can_ids:
                continue
            if exclude_ids and msg.can_id in exclude_ids:
                continue
            
            data_row = [f'{b:02X}' for b in msg.data]
            data_row.extend(['00'] * (8 - len(data_row)))
            
            writer.writerow([
                msg.timestamp_us,
                f'{msg.can_id:08X}',
                'true' if msg.extended else 'false',
                msg.direction,
                msg.bus,
                msg.dlc,
                *data_row
            ])
            count += 1
    
    print(f"Exported {count} messages to {output_path}")
    return count


def export_analysis_report(parser: CANLogParser, output_path: str):
    """Export detailed analysis report as JSON"""
    
    frequencies = analyze_frequencies(parser)
    dynamic_bytes = find_dynamic_bytes(parser)
    rpm_candidates = find_rpm_candidates(parser)
    speed_candidates = find_speed_candidates(parser)
    
    report = {
        'summary': {
            'total_messages': len(parser.messages),
            'unique_can_ids': len(parser.id_stats),
            'duration_seconds': (parser.messages[-1].timestamp_us - 
                                parser.messages[0].timestamp_us) / 1_000_000 
                                if parser.messages else 0,
            'obd_records': len(parser.obd_data)
        },
        'can_ids': [],
        'rpm_candidates': [
            {'can_id': f'0x{c[0]:03X}', 'bytes': f'{c[1]}-{c[2]}', 'score': c[3]}
            for c in rpm_candidates
        ],
        'speed_candidates': [
            {'can_id': f'0x{c[0]:03X}', 'byte': c[1], 'score': c[2]}
            for c in speed_candidates
        ]
    }
    
    for can_id, count, freq in frequencies:
        stats = parser.id_stats[can_id]
        
        id_report = {
            'can_id': f'0x{can_id:03X}',
            'count': count,
            'frequency_hz': round(freq, 2),
            'dlc': stats.dlc,
            'is_obd_response': 0x7E8 <= can_id <= 0x7EF,
            'dynamic_bytes': dynamic_bytes.get(can_id, []),
            'byte_analysis': []
        }
        
        for i, bs in enumerate(stats.byte_stats[:stats.dlc]):
            id_report['byte_analysis'].append({
                'position': i,
                'min': bs['min'],
                'max': bs['max'],
                'unique_values': len(bs['unique']),
                'is_dynamic': i in dynamic_bytes.get(can_id, [])
            })
        
        report['can_ids'].append(id_report)
    
    with open(output_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=2)
    
    print(f"Analysis report saved to {output_path}")

# ============================================================================
# CLI INTERFACE
# ============================================================================

def print_summary(parser: CANLogParser):
    """Print summary to console"""
    print("\n" + "="*60)
    print("CAN LOG ANALYSIS SUMMARY")
    print("="*60)
    
    if not parser.messages:
        print("No messages loaded!")
        return
    
    duration = (parser.messages[-1].timestamp_us - parser.messages[0].timestamp_us) / 1_000_000
    
    print(f"Total Messages:    {len(parser.messages)}")
    print(f"Unique CAN IDs:    {len(parser.id_stats)}")
    print(f"Duration:          {duration:.2f} seconds")
    print(f"Average Rate:      {len(parser.messages)/duration:.1f} msg/sec")
    
    print("\n" + "-"*60)
    print("TOP 15 CAN IDs BY FREQUENCY")
    print("-"*60)
    print(f"{'CAN ID':<10} {'Count':<10} {'Freq (Hz)':<12} {'DLC':<5} {'Dynamic Bytes'}")
    print("-"*60)
    
    frequencies = analyze_frequencies(parser)
    dynamic_bytes = find_dynamic_bytes(parser)
    
    for can_id, count, freq in frequencies[:15]:
        stats = parser.id_stats[can_id]
        dyn = dynamic_bytes.get(can_id, [])
        dyn_str = ','.join(str(d) for d in dyn) if dyn else '-'
        
        # Mark OBD response IDs
        marker = "*" if 0x7E8 <= can_id <= 0x7EF else " "
        
        print(f"0x{can_id:03X}{marker}     {count:<10} {freq:<12.1f} {stats.dlc:<5} {dyn_str}")
    
    print("\n* = Standard OBD-II response ID")
    
    # RPM candidates
    rpm_candidates = find_rpm_candidates(parser)
    if rpm_candidates:
        print("\n" + "-"*60)
        print("POTENTIAL RPM SIGNAL CANDIDATES")
        print("-"*60)
        for can_id, b1, b2, score in rpm_candidates[:5]:
            print(f"  0x{can_id:03X} bytes [{b1}:{b2}] - score: {score:.2f}")
    
    # Speed candidates
    speed_candidates = find_speed_candidates(parser)
    if speed_candidates:
        print("\n" + "-"*60)
        print("POTENTIAL SPEED SIGNAL CANDIDATES")
        print("-"*60)
        for can_id, byte_pos, score in speed_candidates[:5]:
            print(f"  0x{can_id:03X} byte [{byte_pos}] - score: {score:.2f}")
    
    print("\n" + "="*60)


def main():
    parser = argparse.ArgumentParser(
        description='CAN Bus Log Analyzer for reverse engineering',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s canlog.csv                     # Analyze a CAN log
  %(prog)s canlog.csv -o report.json      # Export analysis report
  %(prog)s canlog.csv --obd session.csv   # Correlate with OBD data
  %(prog)s canlog.csv --export filtered.csv --ids 0x100,0x200
        """)
    
    parser.add_argument('canlog', help='GVRET format CAN log file (CSV)')
    parser.add_argument('--obd', '-b', help='OBD telemetry CSV for correlation')
    parser.add_argument('--output', '-o', help='Output analysis report (JSON)')
    parser.add_argument('--export', '-e', help='Export filtered log (CSV)')
    parser.add_argument('--ids', help='Filter by CAN IDs (comma-separated hex, e.g., 0x100,0x200)')
    parser.add_argument('--exclude', help='Exclude CAN IDs (comma-separated hex)')
    parser.add_argument('--min-count', type=int, default=10,
                       help='Minimum message count for analysis (default: 10)')
    
    args = parser.parse_args()
    
    # Validate input file
    if not os.path.exists(args.canlog):
        print(f"Error: File not found: {args.canlog}")
        sys.exit(1)
    
    # Create parser and load data
    log_parser = CANLogParser()
    log_parser.load_gvret_csv(args.canlog)
    
    if args.obd:
        log_parser.load_obd_csv(args.obd)
    
    # Print summary
    print_summary(log_parser)
    
    # Export report if requested
    if args.output:
        export_analysis_report(log_parser, args.output)
    
    # Export filtered log if requested
    if args.export:
        can_ids = None
        exclude_ids = None
        
        if args.ids:
            can_ids = [int(x, 0) for x in args.ids.split(',')]
        if args.exclude:
            exclude_ids = [int(x, 0) for x in args.exclude.split(',')]
        
        export_filtered_gvret(log_parser, args.export, can_ids, exclude_ids)


if __name__ == '__main__':
    main()
