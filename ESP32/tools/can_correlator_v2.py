#!/usr/bin/env python3
"""
CAN-OBD Correlator for Reverse Engineering v2.0
===============================================

Correlates CAN bus messages with OBD PIDs to identify signal mappings.
Features:
- Byte and multi-byte (16-bit) analysis
- Big-endian and little-endian detection  
- DBC file loading and validation
- Normalized overlay plots for visual correlation
- Automatic formula detection (scale + offset)
- Bit-level analysis

Usage:
    python can_correlator_v2.py canlog.csv session.csv
    python can_correlator_v2.py canlog.csv session.csv --dbc mycar.dbc
    python can_correlator_v2.py canlog.csv session.csv --pid rpm --html rpm_analysis.html

Author: OBD2 Data Acquisition Project
Date: 2025
"""

import argparse
import pandas as pd
import numpy as np
from scipy import stats
from pathlib import Path
import plotly.graph_objects as go
from plotly.subplots import make_subplots
import warnings
warnings.filterwarnings('ignore')

try:
    import cantools
    DBC_SUPPORT = True
except ImportError:
    DBC_SUPPORT = False
    print("⚠️  cantools not installed - DBC support disabled")
    print("   Install with: pip install cantools\n")


# ============================================================================
# DATA LOADING
# ============================================================================

def load_gvret_can(filepath):
    """Load GVRET-format CAN log"""
    print(f"📂 Loading CAN log: {filepath}")
    
    df = pd.read_csv(filepath)
    
    # Normalize column names (GVRET uses uppercase)
    df.columns = df.columns.str.lower().str.strip().str.replace(' ', '_')
    
    # Parse timestamp - keep absolute microseconds, don't normalize yet
    if 'timestamp_us' in df.columns:
        df['timestamp_us'] = df['timestamp_us'].astype(int)
        df['time_sec'] = df['timestamp_us'] / 1_000_000
    elif 'timestamp_ms' in df.columns:
        df['timestamp_us'] = (df['timestamp_ms'] * 1000).astype(int)
        df['time_sec'] = df['timestamp_ms'] / 1000
    elif 'time_stamp' in df.columns:
        df['timestamp_us'] = df['time_stamp'].astype(int)
        df['time_sec'] = df['time_stamp'] / 1_000_000  # GVRET is microseconds
    else:
        print("  ⚠️  No timestamp column found, using sequential time")
        df['timestamp_us'] = np.arange(len(df)) * 10000  # 10ms spacing
        df['time_sec'] = np.arange(len(df)) * 0.01
    
    # Parse hex ID
    if 'id' in df.columns:
        if df['id'].dtype == 'object':
            df['id'] = df['id'].apply(lambda x: int(x, 16) if isinstance(x, str) else x)
    
    # Rename data columns to D0-D7
    for i in range(1, 9):
        old_name = f'd{i}'
        if old_name in df.columns:
            df[f'D{i-1}'] = df[old_name]
    
    # Convert hex strings to int
    for i in range(8):
        col = f'D{i}'
        if col in df.columns and df[col].dtype == 'object':
            df[col] = df[col].apply(lambda x: int(x, 16) if isinstance(x, str) and x != '' else 0)
    
    duration = (df['timestamp_us'].max() - df['timestamp_us'].min()) / 1_000_000
    print(f"  ✓ {len(df)} messages, {df['id'].nunique()} unique IDs")
    print(f"  Duration: {duration:.1f}s")
    print(f"  Timestamp range: {df['timestamp_us'].min()} - {df['timestamp_us'].max()}")
    
    return df


def load_obd_session(filepath):
    """Load OBD session CSV"""
    print(f"📂 Loading OBD session: {filepath}")
    
    df = pd.read_csv(filepath)
    
    # Parse timestamp - keep absolute microseconds
    if 'timestamp_us' in df.columns:
        df['timestamp_us'] = df['timestamp_us'].astype(int)
        df['time_sec'] = df['timestamp_us'] / 1_000_000
    elif 'timestamp_ms' in df.columns:
        df['timestamp_us'] = (df['timestamp_ms'] * 1000).astype(int)
        df['time_sec'] = df['timestamp_ms'] / 1000
    else:
        raise ValueError("No timestamp column found")
    
    # Replace -1 (invalid sensor readings) with NaN
    pid_cols = [col for col in df.columns if col not in ['timestamp_us', 'timestamp_ms', 'time_sec']]
    for col in pid_cols:
        df[col] = df[col].replace(-1, np.nan)
    
    # Show statistics for each PID
    duration = (df['timestamp_us'].max() - df['timestamp_us'].min()) / 1_000_000
    print(f"  ✓ {len(df)} samples, {len(pid_cols)} PIDs:")
    print(f"\n  {'PID':<12} {'Valid':<8} {'Range':<20} {'Variance':<12} {'Status'}")
    print(f"  {'-'*12} {'-'*8} {'-'*20} {'-'*12} {'-'*20}")
    
    for col in pid_cols:
        valid = df[col].notna().sum()
        if valid > 0:
            variance = df[col].var()
            min_val = df[col].min()
            max_val = df[col].max()
            range_str = f"{min_val:.1f} - {max_val:.1f}"
            
            # Check for insufficient variation
            if variance < 0.1:
                status = "⚠️  QUASE CONSTANTE"
            elif valid < 50:
                status = "⚠️  POUCOS DADOS"
            else:
                status = "✓ OK"
            
            print(f"  {col:<12} {valid:<8} {range_str:<20} {variance:<12.2f} {status}")
        else:
            print(f"  {col:<12} {0:<8} {'N/A':<20} {'N/A':<12} ❌ SEM DADOS")
    
    print(f"\n  Duration: {duration:.1f}s")
    print(f"  Timestamp range: {df['timestamp_us'].min()} - {df['timestamp_us'].max()}")
    
    return df


def load_dbc(filepath):
    """Load DBC file for signal validation"""
    if not DBC_SUPPORT:
        return None
    
    print(f"📂 Loading DBC: {filepath}")
    try:
        db = cantools.database.load_file(filepath)
        print(f"  ✓ {len(db.messages)} messages, {sum(len(m.signals) for m in db.messages)} signals")
        return db
    except Exception as e:
        print(f"  ❌ Failed: {e}")
        return None


# ============================================================================
# ENCODING ANALYSIS
# ============================================================================

def test_byte_encodings(can_values, pid_values):
    """
    Test different byte interpretations:
    - Direct uint8
    - Signed int8
    - Scaled (common factors: 0.25, 0.5, 2, 4, 10, 32, 64, 100)
    """
    results = []
    
    # Direct uint8
    if len(can_values) >= 10:
        corr, _ = stats.pearsonr(can_values, pid_values)
        if abs(corr) > 0.3:
            slope, intercept = np.polyfit(can_values, pid_values, 1)
            results.append({
                'encoding': 'uint8',
                'correlation': corr,
                'formula': f"{slope:.4f} * byte + {intercept:.2f}",
                'scale': slope,
                'offset': intercept,
                'values': can_values
            })
    
    # Signed int8
    can_signed = np.array([x if x < 128 else x - 256 for x in can_values])
    if len(can_signed) >= 10:
        corr, _ = stats.pearsonr(can_signed, pid_values)
        if abs(corr) > 0.3:
            slope, intercept = np.polyfit(can_signed, pid_values, 1)
            results.append({
                'encoding': 'int8',
                'correlation': corr,
                'formula': f"{slope:.4f} * signed + {intercept:.2f}",
                'scale': slope,
                'offset': intercept,
                'values': can_signed
            })
    
    # Common scaling factors
    for scale_factor in [0.25, 0.5, 2, 4, 10, 32, 64, 100]:
        scaled = can_values * scale_factor
        if len(scaled) >= 10:
            corr, _ = stats.pearsonr(scaled, pid_values)
            if abs(corr) > 0.3:
                slope, intercept = np.polyfit(scaled, pid_values, 1)
                results.append({
                    'encoding': f'uint8 × {scale_factor}',
                    'correlation': corr,
                    'formula': f"{slope:.4f} * (byte × {scale_factor}) + {intercept:.2f}",
                    'scale': scale_factor * slope,
                    'offset': intercept,
                    'values': scaled
                })
    
    if not results:
        return None
    
    # Return best correlation
    results.sort(key=lambda x: abs(x['correlation']), reverse=True)
    return results[0]


def test_multibyte_encodings(can_df, can_id, byte1, byte2, pid_values):
    """
    Test 16-bit interpretations:
    - Big-endian: (byte1 << 8) | byte2
    - Little-endian: (byte2 << 8) | byte1
    """
    can_subset = can_df[can_df['id'] == can_id].copy()
    
    if len(can_subset) < 10:
        return None
    
    # Big-endian
    be_values = (can_subset[f'D{byte1}'] * 256 + can_subset[f'D{byte2}']).values
    corr_be, _ = stats.pearsonr(be_values[:len(pid_values)], pid_values)
    
    # Little-endian
    le_values = (can_subset[f'D{byte2}'] * 256 + can_subset[f'D{byte1}']).values
    corr_le, _ = stats.pearsonr(le_values[:len(pid_values)], pid_values)
    
    if abs(corr_be) > abs(corr_le) and abs(corr_be) > 0.5:
        slope, intercept = np.polyfit(be_values[:len(pid_values)], pid_values, 1)
        return {
            'bytes': f'{byte1}:{byte2}',
            'endian': 'big-endian',
            'correlation': corr_be,
            'formula': f"(D{byte1}<<8|D{byte2}) × {slope:.4f} + {intercept:.2f}",
            'scale': slope,
            'offset': intercept,
            'values': be_values
        }
    elif abs(corr_le) > 0.5:
        slope, intercept = np.polyfit(le_values[:len(pid_values)], pid_values, 1)
        return {
            'bytes': f'{byte1}:{byte2}',
            'endian': 'little-endian',
            'correlation': corr_le,
            'formula': f"(D{byte2}<<8|D{byte1}) × {slope:.4f} + {intercept:.2f}",
            'scale': slope,
            'offset': intercept,
            'values': le_values
        }
    
    return None


# ============================================================================
# CORRELATION ANALYSIS
# ============================================================================

def correlate_byte_with_pid(can_df, can_id, byte_pos, obd_df, pid_name):
    """Calculate correlation between CAN byte and OBD PID"""
    
    can_subset = can_df[can_df['id'] == can_id].copy()
    
    if len(can_subset) < 10 or pid_name not in obd_df.columns:
        return None
    
    # Merge by time (100ms tolerance)
    merged = pd.merge_asof(
        can_subset.sort_values('time_sec'),
        obd_df[['time_sec', pid_name]].sort_values('time_sec'),
        on='time_sec',
        tolerance=0.1,
        direction='nearest'
    )
    
    merged = merged.dropna(subset=[f'D{byte_pos}', pid_name])
    
    if len(merged) < 10:
        return None
    
    can_values = merged[f'D{byte_pos}'].values
    pid_values = merged[pid_name].values
    
    # Test different encodings
    best = test_byte_encodings(can_values, pid_values)
    
    if not best or abs(best['correlation']) < 0.5:
        return None
    
    return {
        'can_id': can_id,
        'byte_pos': byte_pos,
        'pid_name': pid_name,
        'correlation': best['correlation'],
        'formula': best['formula'],
        'encoding': best['encoding'],
        'scale': best['scale'],
        'offset': best['offset'],
        'n_samples': len(merged)
    }


def find_all_correlations(can_df, obd_df, threshold=0.7, test_multibyte=True, min_rpm=0, focus_pid=None):
    """
    Find all significant correlations.
    Tests both single bytes and 16-bit combinations.
    
    Args:
        min_rpm: Minimum RPM to filter data (default 0 = all data, set 500 for engine running only)
        focus_pid: If set, don't filter by RPM even if min_rpm is set
    """
    print(f"\n🔍 Searching for correlations (threshold={threshold})...\n")
    
    # Find time overlap between CAN and OBD using absolute timestamps
    can_time_min = can_df['timestamp_us'].min()
    can_time_max = can_df['timestamp_us'].max()
    obd_time_min = obd_df['timestamp_us'].min()
    obd_time_max = obd_df['timestamp_us'].max()
    
    overlap_start = max(can_time_min, obd_time_min)
    overlap_end = min(can_time_max, obd_time_max)
    
    if overlap_start >= overlap_end:
        print(f"❌ Sem overlap entre dados CAN e OBD!")
        print(f"   CAN: {can_time_min} - {can_time_max} ({(can_time_max - can_time_min)/1e6:.1f}s)")
        print(f"   OBD: {obd_time_min} - {obd_time_max} ({(obd_time_max - obd_time_min)/1e6:.1f}s)")
        return pd.DataFrame()
    
    duration_overlap = (overlap_end - overlap_start) / 1_000_000
    print(f"📊 Intervalo comum (timestamp absoluto):")
    print(f"   {overlap_start} - {overlap_end} ({duration_overlap:.1f}s)")
    
    # Filter both to common time range
    can_df_orig = can_df.copy()
    obd_df_orig = obd_df.copy()
    
    can_df = can_df[(can_df['timestamp_us'] >= overlap_start) & (can_df['timestamp_us'] <= overlap_end)].copy()
    obd_df = obd_df[(obd_df['timestamp_us'] >= overlap_start) & (obd_df['timestamp_us'] <= overlap_end)].copy()
    
    print(f"   CAN messages: {len(can_df)} (de {len(can_df_orig)})")
    print(f"   OBD samples: {len(obd_df)} (de {len(obd_df_orig)})\n")
    
    # Now normalize time_sec to start at overlap_start for correlation
    can_df['time_sec'] = (can_df['timestamp_us'] - overlap_start) / 1_000_000
    obd_df['time_sec'] = (obd_df['timestamp_us'] - overlap_start) / 1_000_000
    
    # Filter by RPM if requested (but not if focusing on RPM analysis)
    if 'rpm' in obd_df.columns and min_rpm > 0 and focus_pid != 'rpm':
        obd_before = len(obd_df)
        obd_df = obd_df[obd_df['rpm'] > min_rpm].copy()
        
        if len(obd_df) == 0:
            print(f"❌ Nenhum dado com RPM > {min_rpm} no intervalo comum")
            return pd.DataFrame()
        
        print(f"🚗 Filtrando por RPM > {min_rpm}")
        print(f"   Amostras OBD: {len(obd_df)} de {obd_before} ({len(obd_df)/obd_before*100:.1f}%)")
        
        # Re-filter CAN for this narrower time range
        can_df = can_df[
            (can_df['time_sec'] >= obd_df['time_sec'].min()) &
            (can_df['time_sec'] <= obd_df['time_sec'].max())
        ].copy()
        print(f"   CAN messages: {len(can_df)}\n")
    
    results = []
    can_ids = sorted(can_df['id'].unique())
    pid_names = [col for col in obd_df.columns if col not in ['timestamp_us', 'timestamp_ms', 'time_sec']]
    
    # Filter PIDs with valid data AND sufficient variance
    valid_pids = []
    print("📊 Checking PIDs for sufficient variation:")
    for pid in pid_names:
        valid_count = obd_df[pid].notna().sum()
        variance = obd_df[pid].var()
        
        if valid_count < 10:
            print(f"  ⚠️  Skipping {pid}: only {valid_count} valid samples")
        elif variance < 1.0 and focus_pid != pid:
            print(f"  ⚠️  Skipping {pid}: variance too low ({variance:.3f}) - data quase constante!")
            print(f"      → Carro provavelmente parado. Capture dados com variação (acelere, mova o carro)")
        elif variance < 1.0 and focus_pid == pid:
            print(f"  ⚠️  {pid}: variance muito baixa ({variance:.3f}), mas continuando porque foi especificado --pid")
            valid_pids.append(pid)
        else:
            valid_pids.append(pid)
            print(f"  ✓ {pid}: {valid_count} samples, variance={variance:.2f}")
    
    if not valid_pids:
        print("\n❌ Nenhum PID com dados variáveis suficientes!")
        print("💡 Dica: Capture novos dados com o carro em MOVIMENTO ou acelerando.")
        print("   Os dados atuais parecem ser de carro parado/idle constante.")
        print("\n📊 Estatísticas do intervalo comum:")
        for pid in pid_names:
            vals = obd_df[pid].dropna()
            if len(vals) > 0:
                print(f"   {pid}: {vals.min():.1f} - {vals.max():.1f} (unique: {vals.nunique()})")
        return pd.DataFrame()
    
    print(f"\n📊 PIDs válidos para análise: {', '.join(valid_pids)}\n")
    
    # Single byte analysis
    total_tests = len(can_ids) * 8 * len(valid_pids)
    print(f"📊 Testing single bytes: {len(can_ids)} IDs × 8 bytes × {len(valid_pids)} PIDs = {total_tests}")
    
    for can_id in can_ids:
        for byte_pos in range(8):
            for pid_name in valid_pids:
                result = correlate_byte_with_pid(can_df, can_id, byte_pos, obd_df, pid_name)
                if result and abs(result['correlation']) >= threshold:
                    results.append(result)
                    print(f"  ✓ 0x{can_id:03X}[D{byte_pos}] ↔ {pid_name}: r={result['correlation']:.3f} | {result['formula']}")
    
    # Multi-byte analysis
    if test_multibyte:
        print(f"\n📊 Testing 16-bit combinations...")
        for can_id in can_ids:
            for pid_name in valid_pids:
                
                can_subset = can_df[can_df['id'] == can_id].copy()
                merged = pd.merge_asof(
                    can_subset.sort_values('time_sec'),
                    obd_df[['time_sec', pid_name]].sort_values('time_sec'),
                    on='time_sec',
                    tolerance=0.1,
                    direction='nearest'
                )
                merged = merged.dropna(subset=[pid_name])
                
                if len(merged) < 10:
                    continue
                
                pid_values = merged[pid_name].values
                
                # Test consecutive byte pairs
                for b1 in range(7):
                    b2 = b1 + 1
                    result = test_multibyte_encodings(merged, can_id, b1, b2, pid_values)
                    if result and abs(result['correlation']) >= threshold:
                        results.append({
                            'can_id': can_id,
                            'byte_pos': f'D{b1}:D{b2}',
                            'pid_name': pid_name,
                            'correlation': result['correlation'],
                            'formula': result['formula'],
                            'encoding': result['endian'],
                            'scale': result['scale'],
                            'offset': result['offset'],
                            'n_samples': len(merged)
                        })
                        print(f"  ✓ 0x{can_id:03X}[D{b1}:D{b2}] ↔ {pid_name}: r={result['correlation']:.3f} ({result['endian']})")
    
    if not results:
        print("\n❌ No significant correlations found")
        return pd.DataFrame()
    
    df = pd.DataFrame(results)
    df = df.sort_values('correlation', key=abs, ascending=False)
    
    print(f"\n✅ Found {len(df)} significant correlations\n")
    return df


# ============================================================================
# VISUALIZATION - OVERLAY PLOTS
# ============================================================================

def plot_dual_axis_comparison(can_df, can_id, byte_pos, obd_df, pid_name, corr_info=None):
    """
    Create dual-axis plot with RAW values.
    Shows both signals with independent Y-axes to see actual values.
    """
    
    # Get CAN data
    if ':' in str(byte_pos):  # Multi-byte
        b1, b2 = map(int, str(byte_pos).replace('D', '').split(':'))
        can_subset = can_df[can_df['id'] == can_id].copy()
        can_subset['value'] = (can_subset[f'D{b1}'] * 256 + can_subset[f'D{b2}'])
        label = f'D{b1}:D{b2}'
    else:
        can_subset = can_df[can_df['id'] == can_id].copy()
        can_subset['value'] = can_subset[f'D{byte_pos}']
        label = f'D{byte_pos}'
    
    # Merge with OBD
    merged = pd.merge_asof(
        can_subset.sort_values('time_sec'),
        obd_df[['time_sec', pid_name]].sort_values('time_sec'),
        on='time_sec',
        tolerance=0.1,
        direction='nearest'
    )
    merged = merged.dropna()
    
    if len(merged) < 2:
        return None
    
    # Create figure with secondary y-axis
    fig = make_subplots(specs=[[{"secondary_y": True}]])
    
    # CAN signal (blue, left axis)
    fig.add_trace(
        go.Scatter(
            x=merged['time_sec'],
            y=merged['value'],
            name=f'CAN 0x{can_id:03X} {label}',
            line=dict(color='#3498db', width=2),
            mode='lines',
            hovertemplate='CAN: %{y}<br>Time: %{x:.2f}s<extra></extra>'
        ),
        secondary_y=False
    )
    
    # PID signal (red, right axis)
    fig.add_trace(
        go.Scatter(
            x=merged['time_sec'],
            y=merged[pid_name],
            name=f'OBD {pid_name}',
            line=dict(color='#e74c3c', width=2),
            mode='lines',
            hovertemplate='PID: %{y:.2f}<br>Time: %{x:.2f}s<extra></extra>'
        ),
        secondary_y=True
    )
    
    # Title with correlation info
    title = f'CAN 0x{can_id:03X} {label} ↔ {pid_name}'
    if corr_info:
        title += f"<br><sub>r = {corr_info['correlation']:.3f} | {corr_info.get('formula', 'N/A')}</sub>"
    
    fig.update_layout(
        title=title,
        xaxis_title='Time (seconds)',
        height=500,
        hovermode='x unified',
        legend=dict(x=0.02, y=0.98, bgcolor='rgba(255,255,255,0.8)'),
        plot_bgcolor='white',
        xaxis=dict(gridcolor='#ecf0f1')
    )
    
    # Set y-axes titles
    fig.update_yaxes(title_text=f"CAN {label} (raw)", secondary_y=False, gridcolor='#ecf0f1')
    fig.update_yaxes(title_text=f"{pid_name}", secondary_y=True, gridcolor='#ecf0f1')
    
    return fig


def plot_scatter_correlation(can_df, can_id, byte_pos, obd_df, pid_name, corr_info):
    """Scatter plot with trend line"""
    
    # Get data
    if ':' in str(byte_pos):
        b1, b2 = map(int, str(byte_pos).replace('D', '').split(':'))
        can_subset = can_df[can_df['id'] == can_id].copy()
        can_subset['value'] = (can_subset[f'D{b1}'] * 256 + can_subset[f'D{b2}'])
        label = f'{b1}:{b2}'
    else:
        can_subset = can_df[can_df['id'] == can_id].copy()
        can_subset['value'] = can_subset[f'D{byte_pos}']
        label = str(byte_pos)
    
    merged = pd.merge_asof(
        can_subset.sort_values('time_sec'),
        obd_df[['time_sec', pid_name]].sort_values('time_sec'),
        on='time_sec',
        tolerance=0.1
    ).dropna()
    
    if len(merged) < 2:
        return None
    
    # Scatter
    fig = go.Figure()
    
    fig.add_trace(go.Scatter(
        x=merged['value'],
        y=merged[pid_name],
        mode='markers',
        marker=dict(size=6, color='#3498db', opacity=0.6),
        name='Data',
        hovertemplate='CAN: %{x}<br>PID: %{y:.2f}<extra></extra>'
    ))
    
    # Trend line
    x = merged['value'].values
    y = merged[pid_name].values
    z = np.polyfit(x, y, 1)
    p = np.poly1d(z)
    x_line = np.linspace(x.min(), x.max(), 100)
    
    fig.add_trace(go.Scatter(
        x=x_line,
        y=p(x_line),
        mode='lines',
        line=dict(color='#e74c3c', dash='dash', width=2),
        name='Trend',
        hoverinfo='skip'
    ))
    
    fig.update_layout(
        title=f"0x{can_id:03X}[{label}] vs {pid_name}<br><sub>r = {corr_info['correlation']:.3f}</sub>",
        xaxis_title=f"CAN Byte Value",
        yaxis_title=pid_name,
        height=450,
        plot_bgcolor='white',
        xaxis=dict(gridcolor='#ecf0f1'),
        yaxis=dict(gridcolor='#ecf0f1')
    )
    
    return fig


def plot_correlation_heatmap(correlations_df):
    """Heatmap of correlations"""
    
    if len(correlations_df) == 0:
        return None
    
    # Create pivot table
    pivot = correlations_df.pivot_table(
        index='can_id',
        columns='pid_name',
        values='correlation',
        aggfunc='first'
    ).fillna(0)
    
    fig = go.Figure(data=go.Heatmap(
        z=pivot.values,
        x=pivot.columns,
        y=[f"0x{int(cid):03X}" for cid in pivot.index],
        colorscale='RdBu',
        zmid=0,
        text=np.round(pivot.values, 2),
        texttemplate='%{text}',
        textfont={"size": 10},
        colorbar=dict(title="Pearson r")
    ))
    
    fig.update_layout(
        title="CAN ID × OBD PID Correlation Matrix",
        xaxis_title="OBD PID",
        yaxis_title="CAN ID",
        height=max(400, len(pivot) * 30),
        xaxis=dict(side='bottom')
    )
    
    return fig


# ============================================================================
# DBC VALIDATION
# ============================================================================

def validate_with_dbc(correlations_df, dbc):
    """Check if found correlations match known DBC signals"""
    
    if not dbc or len(correlations_df) == 0:
        return correlations_df
    
    print("\n🔍 Validating against DBC...\n")
    
    correlations_df['dbc_signal'] = ''
    correlations_df['dbc_match'] = ''
    
    for idx, row in correlations_df.iterrows():
        can_id = int(row['can_id'])
        try:
            msg = dbc.get_message_by_frame_id(can_id)
            byte_pos = str(row['byte_pos'])
            
            if ':' not in byte_pos:
                byte_num = int(byte_pos)
                for signal in msg.signals:
                    start_byte = signal.start // 8
                    if start_byte == byte_num:
                        correlations_df.at[idx, 'dbc_signal'] = signal.name
                        correlations_df.at[idx, 'dbc_match'] = '✓'
                        print(f"  ✓ 0x{can_id:03X}[D{byte_num}] matches DBC: {signal.name}")
                        break
        except:
            continue
    
    return correlations_df


# ============================================================================
# HTML DASHBOARD
# ============================================================================

def create_dashboard(can_df, obd_df, correlations_df, output_html='dashboard.html', dbc=None):
    """Create comprehensive HTML dashboard"""
    
    print("\n📊 Creating interactive dashboard...")
    
    # DBC validation
    if dbc:
        correlations_df = validate_with_dbc(correlations_df, dbc)
    
    # HTML header
    html = ['<!DOCTYPE html><html><head>']
    html.append('<meta charset="utf-8">')
    html.append('<title>CAN Reverse Engineering Dashboard</title>')
    html.append('<style>')
    html.append('body { font-family: "Segoe UI", Arial, sans-serif; margin: 20px; background: #f5f5f5; }')
    html.append('h1 { color: #2c3e50; border-bottom: 3px solid #3498db; padding-bottom: 10px; }')
    html.append('h2 { color: #34495e; border-bottom: 2px solid #95a5a6; padding-bottom: 5px; margin-top: 40px; }')
    html.append('table { border-collapse: collapse; width: 100%; background: white; margin: 20px 0; }')
    html.append('th, td { padding: 12px; text-align: left; border: 1px solid #ddd; }')
    html.append('th { background: #3498db; color: white; font-weight: bold; }')
    html.append('tr:nth-child(even) { background: #f9f9f9; }')
    html.append('.summary { background: white; padding: 20px; border-radius: 5px; margin: 20px 0; }')
    html.append('.summary p { margin: 5px 0; font-size: 16px; }')
    html.append('</style>')
    html.append('</head><body>')
    
    # Title
    html.append('<h1>🔬 CAN Reverse Engineering Dashboard</h1>')
    html.append(f'<div class="summary"><p><b>Generated:</b> {pd.Timestamp.now().strftime("%Y-%m-%d %H:%M:%S")}</p>')
    html.append(f'<p><b>CAN Messages:</b> {len(can_df):,} | <b>OBD Samples:</b> {len(obd_df):,}</p>')
    html.append(f'<p><b>Correlations Found:</b> {len(correlations_df)}</p></div>')
    
    if len(correlations_df) == 0:
        html.append('<p style="color: red; font-size: 20px;">❌ No significant correlations found. Try lowering the threshold.</p>')
    else:
        # Correlation table
        html.append('<h2>📋 Discovered Correlations</h2>')
        html.append(correlations_df.head(30).to_html(index=False, float_format=lambda x: f'{x:.4f}', escape=False))
        
        # Heatmap
        html.append('<h2>🔥 Correlation Heatmap</h2>')
        fig = plot_correlation_heatmap(correlations_df)
        if fig:
            html.append(fig.to_html(include_plotlyjs='cdn', full_html=False))
        
        # Dual-axis plots (top 20)
        html.append('<h2>📈 CAN ↔ OBD Time Series (Top 20)</h2>')
        html.append('<p><i>Dual-axis plots showing RAW values. Left axis = CAN bytes, Right axis = OBD PID.</i></p>')
        
        for idx, row in correlations_df.head(20).iterrows():
            fig = plot_dual_axis_comparison(
                can_df, int(row['can_id']), row['byte_pos'],
                obd_df, row['pid_name'], row.to_dict()
            )
            if fig:
                html.append(fig.to_html(include_plotlyjs=False, full_html=False))
        
        # Scatter plots (top 10)
        html.append('<h2>🎯 Scatter Plots (Top 10)</h2>')
        for idx, row in correlations_df.head(10).iterrows():
            fig = plot_scatter_correlation(
                can_df, int(row['can_id']), row['byte_pos'],
                obd_df, row['pid_name'], row.to_dict()
            )
            if fig:
                html.append(fig.to_html(include_plotlyjs=False, full_html=False))
    
    html.append('</body></html>')
    
    # Save
    with open(output_html, 'w', encoding='utf-8') as f:
        f.write('\n'.join(html))
    
    print(f"  ✓ Dashboard saved: {output_html}\n")
    return output_html


# ============================================================================
# MAIN
# ============================================================================

def main():
    parser = argparse.ArgumentParser(description='CAN-OBD Correlation Analysis for Reverse Engineering')
    parser.add_argument('canlog', help='GVRET CAN log CSV file')
    parser.add_argument('obdlog', help='OBD session CSV file')
    parser.add_argument('--dbc', help='DBC file for validation (optional)')
    parser.add_argument('--threshold', type=float, default=0.7, help='Correlation threshold (default: 0.7)')
    parser.add_argument('--pid', help='Analyze specific PID only')
    parser.add_argument('--html', default='dashboard.html', help='Output HTML file (default: dashboard.html)')
    parser.add_argument('--no-multibyte', action='store_true', help='Skip 16-bit analysis')
    parser.add_argument('--min-rpm', type=int, default=0, help='Minimum RPM to filter data (default: 0 = all data, 500 = engine running)')
    
    args = parser.parse_args()
    
    print("\n" + "="*70)
    print("  CAN-OBD Reverse Engineering Correlator v2.0")
    print("="*70 + "\n")
    
    # Load data
    can_df = load_gvret_can(args.canlog)
    obd_df = load_obd_session(args.obdlog)
    
    # Load DBC if provided
    dbc = None
    if args.dbc:
        dbc = load_dbc(args.dbc)
    
    # Filter by PID if specified
    if args.pid:
        if args.pid not in obd_df.columns:
            print(f"❌ PID '{args.pid}' not found in OBD data")
            return
        obd_df = obd_df[['timestamp_us', 'time_sec', args.pid]]
        print(f"🎯 Analyzing only PID: {args.pid}")
        if args.pid == 'rpm':
            print(f"   → RPM analysis: using ALL data (not filtering by min RPM)\n")
    
    # Find correlations
    correlations_df = find_all_correlations(
        can_df, obd_df,
        threshold=args.threshold,
        test_multibyte=not args.no_multibyte,
        min_rpm=args.min_rpm,
        focus_pid=args.pid
    )
    
    # Create dashboard
    if len(correlations_df) > 0:
        create_dashboard(can_df, obd_df, correlations_df, args.html, dbc)
        print(f"✅ Analysis complete! Open {args.html} in your browser.\n")
    else:
        print("💡 Try lowering --threshold or check if timestamps are synchronized.\n")


if __name__ == '__main__':
    main()
