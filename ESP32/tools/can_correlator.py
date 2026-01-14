#!/usr/bin/env python3
"""
CAN-OBD Correlator for Reverse Engineering
===========================================

Correlates CAN bus messages with OBD PIDs to identify signal mappings.
Features:
- Byte and multi-byte (16-bit) analysis
- Big-endian and little-endian detection
- DBC file loading and validation
- Overlay plots for visual correlation
- Automatic formula detection (scale + offset)

Usage:
    python can_correlator.py canlog.csv session.csv
    python can_correlator.py canlog.csv session.csv --dbc mycar.dbc
    python can_correlator.py canlog.csv session.csv --pid rpm --html rpm_analysis.html

Author: OBD2 Data Acquisition Project
Date: 2025
"""

import argparse
import pandas as pd
import numpy as np
from scipy import stats
from scipy.signal import correlate
from pathlib import Path
import plotly.graph_objects as go
from plotly.subplots import make_subplots
import plotly.express as px
from datetime import datetime
import warnings
warnings.filterwarnings('ignore')

try:
    import cantools
    DBC_SUPPORT = True
except ImportError:
    DBC_SUPPORT = False
    print("⚠️  cantools not installed - DBC support disabled")
    print("   Install with: pip install cantools")

# ============================================================================
# DATA LOADING
# ============================================================================

def load_gvret_can(filepath):
    """Load GVRET format CAN log"""
    print(f"📂 Loading CAN log: {filepath}")
    
    df = pd.read_csv(filepath, skiprows=1, names=[
        'timestamp_us', 'id', 'extended', 'dir', 'bus', 'len',
        'D0', 'D1', 'D2', 'D3', 'D4', 'D5', 'D6', 'D7'
    ])
    
    # Convert ID to int
    df['id'] = df['id'].apply(lambda x: int(x, 16) if isinstance(x, str) else x)
    
    # Convert timestamp to seconds for easier plotting
    df['time_sec'] = df['timestamp_us'] / 1_000_000
    
    print(f"  ✓ Loaded {len(df)} messages ({df['id'].nunique()} unique IDs)")
    return df


def load_obd_session(filepath):
    """Load OBD session CSV"""
    print(f"📂 Loading OBD session: {filepath}")
    
    df = pd.read_csv(filepath)
    
    # Convert timestamp to seconds
    if 'timestamp_us' in df.columns:
        df['time_sec'] = df['timestamp_us'] / 1_000_000
    elif 'timestamp_ms' in df.columns:
        df['time_sec'] = df['timestamp_ms'] / 1_000
    else:
        print("  ⚠️  No timestamp column found, using index")
        df['time_sec'] = df.index * 0.05  # Assume 20Hz
    
    print(f"  ✓ Loaded {len(df)} OBD records")
    print(f"  📊 Available PIDs: {', '.join(df.columns[1:])}")
    return df


# ============================================================================
# DBC SUPPORT
# ============================================================================

# ============================================================================
# CORRELATION ANALYSIS - ENHANCED
# ============================================================================

def test_byte_encoding(can_values, pid_values):
    """
    Test different byte interpretations:
    - Direct (uint8)
    - Signed (int8) 
    - Scaled (with common factors)
    """
    results = []
    
    # Direct uint8
    corr_direct, _ = stats.pearsonr(can_values, pid_values)
    if abs(corr_direct) > 0.1:
        slope, intercept = np.polyfit(can_values, pid_values, 1)
        results.append({
            'encoding': 'uint8',
            'correlation': corr_direct,
            'formula': f"PID = {slope:.4f} * byte + {intercept:.2f}",
            'scale': slope,
            'offset': intercept
        })
    
    # Signed int8
    can_signed = np.array([x if x < 128 else x - 256 for x in can_values])
    corr_signed, _ = stats.pearsonr(can_signed, pid_values)
    if abs(corr_signed) > 0.1:
        slope, intercept = np.polyfit(can_signed, pid_values, 1)
        results.append({
            'encoding': 'int8',
            'correlation': corr_signed,
            'formula': f"PID = {slope:.4f} * signed_byte + {intercept:.2f}",
            'scale': slope,
            'offset': intercept
        })
    
    # Common scaling factors
    for scale in [0.25, 0.5, 2, 4, 10, 32, 64]:
        scaled = can_values * scale
    Tests multiple encodings and returns best match.
    """
    # Filter CAN messages for this ID
    can_subset = can_df[can_df['id'] == can_id].copy()
    
    if len(can_subset) < 10 or pid_name not in obd_df.columns:
        return None
    
    # Merge by time (tolerance 100ms)
    merged = pd.merge_asof(
        can_subset.sort_values('time_sec'),
        obd_df[['time_sec', pid_name]].sort_values('time_sec'),
        on='time_sec',
        tolerance=0.1,
        direction='nearest'
    )
    
    # Remove NaN
    merged = merged.dropna(subset=[f'D{byte_pos}', pid_name])
    
    if len(merged) < 10:
        return None
    
    can_values = merged[f'D{byte_pos}'].values
    pid_values = merged[pid_name].values
    
    # Test different encodings
    best_encoding = test_byte_encoding(can_values, pid_values)
    
    if not best_encoding or abs(best_encoding['correlation']) < 0.5:
        return None
    
    return {
        'can_id': can_id,
        'byte_pos': byte_pos,
        'pid_name': pid_name,
        'correlation': best_encoding['correlation'],
        'formula': best_encoding['formula'],
        'encoding': best_encoding['encoding'],
        'scale': best_encoding['scale'],
        'offset': best_encoding['offset'],
        'n_samples': len(merged)
            'bytes': f'[{byte1}:{byte2}]',
            'endian': 'little-endian',
            'correlation': corr_le,
            'formula': f"PID = (D{byte2}<<8|D{byte1}) * {slope:.4f} + {intercept:.2f}",
            'scale': slope,
            'offset': intercept,
            'values': le_values
        }
    """Load DBC file and extract signals"""
    if not DBC_SUPPORT:
        print("⚠️  DBC support not available")
        return None
    
    print(f"📂 Loading DBC: {filepath}")
    try:
        db = cantools.database.load_file(filepath)
        print(f"  ✓ Loaded {len(db.messages)} messages, {sum(len(m.signals) for m in db.messages)} signals")
        return db
    except Exception as e:
        print(f"  ❌ Failed to load DBC: {e}")
        return None


def extract_signal_from_dbc(can_df, dbc, can_id, signal_name):
    """Extract decoded signal from CAN data using DBC"""
    if not dbc:
        return None
    
    try:
        msg = dbc.get_message_by_frame_id(can_id)
        signal = msg.get_signal_by_name(signal_name)
        
        can_subset = can_df[can_df['id'] == can_id].copy()
        values = []
        
        for _, row in can_subset.iterrows():
            data = bytes([int(row[f'D{i}']) for i in range(8)])
            decoded = msg.decode(data)
            values.append(decoded.get(signal_name, np.nan))
        
        can_subset['signal_value'] = values
        return can_subset
    except:
        return None

def correlate_byte_with_pid(can_df, can_id, byte_pos, obd_df, pid_name):
    """
    Calculate correlation between a CAN byte and an OBD PID.
    Returns (correlation, p_value, formula_hint)
    """
    # Filter CAN messages for this ID
    can_subset = can_df[can_df['id'] == can_id].copy()
    
    if len(can_subset) < 10 or pid_name not in obd_df.columns:
        return None
    
    # Merge by time (tolerance 100ms)
    can_subset['time_sec_rounded'] = (can_subset['time_sec'] * 10).round() / 10
    obd_subset = obd_df[['time_sec', pid_name]].copy()
    obd_subset['time_sec_rounded'] = (obd_subset['time_sec'] * 10).round() / 10
    
    merged = pd.merge_asof(
        can_subset.sort_values('time_sec'),
        obd_subset.sort_values('time_sec'),
        on='time_sec',
        tolerance=0.1,
        direction='nearest'
    )
    
    # Remove NaN
    merged = merged.dropna(subset=[f'D{byte_pos}', pid_name])
    
    if len(merged) < 10:
        return None
    
    can_values = merged[f'D{byte_pos}'].values
    pid_values = merged[pid_name].values
    
    # Calculate Pearson correlation
    corr, p_value = stats.pearsonr(can_values, pid_values)
    
    # Try to find formula (scaling factor)
    if abs(corr) > 0.7:
        # Linear regression
        slope, intercept = np.polyfit(can_values, pid_values, 1)
        formula = f"PID = {slope:.3f} * byte + {intercept:.2f}"
    else:
        formula = "N/A"
    
    return {
        'correlation': corr,
        'p_value': p_value,
        'n_samples': len(merged),
        'formula': formula,
        'can_id': can_id,
        'byte_pos': byte_pos,
        'pid_name': pid_name
    }


def find_all_correlations(can_df, obd_df, min_correlation=0.7):
    """
    Find all strong correlations between CAN bytes and OBD PIDs.
    """
    print(f"\n🔍 Searching for correlations (threshold: {min_correlation})...")
    
    results = []
    can_ids = can_df['id'].unique()
    obd_pids = [col for col in obd_df.columns if col not in ['time_sec', 'timestamp_us', 'timestamp_ms']]
    
    total = len(can_ids) * 8 * len(obd_pids)
    print(f"  Testing {len(can_ids)} IDs × 8 bytes × {len(obd_pids)} PIDs = {total} combinations")
    
    for can_id in can_ids:
        for byte_pos in range(8):
            for pid_name in obd_pids:
                result = correlate_byte_with_pid(can_df, can_id, byte_pos, obd_df, pid_name)
                
                if result and abs(result['correlation']) >= min_correlation:
                    results.append(result)
    
    # Sort by correlation strength
    results.sort(key=lambda x: abs(x['correlation']), reverse=True)
    
    print(f"  ✓ Found {len(results)} strong correlations")
    return results


# ============================================================================
# VISUALIZATION
# ============================================================================

def plot_correlation_heatmap(correlations, obd_pids):
    """
    Create heatmap of correlations CAN ID × PID.
    """
    # Create matrix
    can_ids = sorted(set(r['can_id'] for r in correlations))
    matrix = np.zeros((len(can_ids), len(obd_pids)))
    
    for r in correlations:
        i = can_ids.index(r['can_id'])
        j = obd_pids.index(r['pid_name'])
        matrix[i, j] = r['correlation']
    
    fig = go.Figure(data=go.Heatmap(
        z=matrix,
        x=obd_pids,
        y=[f"0x{cid:03X}" for cid in can_ids],
        colorscale='RdBu',
        zmid=0,
        text=np.round(matrix, 2),
        texttemplate='%{text}',
        textfont={"size": 10},
        colorbar=dict(title="Correlation")
    ))
    
    fig.update_layout(
        title="CAN ID vs OBD PID Correlation Matrix",
        xaxis_title="OBD PID",
        yaxis_title="CAN ID",
        height=600
    )
    
    return fig


def plot_byte_vs_pid(can_df, can_id, byte_pos, obd_df, pid_name, result):
    """
    Plot CAN byte value vs OBD PID value over time (dual axis).
    """
    # Get data
    can_subset = can_df[can_df['id'] == can_id].copy()
    
    fig = make_subplots(specs=[[{"secondary_y": True}]])
    
    # CAN byte
    fig.add_trace(
        go.Scatter(
            x=can_subset['time_sec'],
            y=can_subset[f'D{byte_pos}'],
            name=f"CAN 0x{can_id:03X} byte {byte_pos}",
            mode='lines',
            line=dict(color='blue', width=1)
        ),
        secondary_y=False
    )
    
    # OBD PID
    fig.add_trace(
        go.Scatter(
            x=obd_df['time_sec'],
            y=obd_df[pid_name],
            name=f"OBD {pid_name}",
            mode='lines',
            line=dict(color='red', width=2)
        ),
        secondary_y=True
    )
    
    fig.update_layout(
        title=f"Correlation: {result['correlation']:.3f} | {result['formula']}",
        hovermode='x unified',
        height=400
    )
    
    fig.update_xaxes(title_text="Time (seconds)")
    fig.update_yaxes(title_text="CAN Byte Value", secondary_y=False)
    fig.update_yaxes(title_text=f"{pid_name} Value", secondary_y=True)
    
    return fig


def plot_scatter_correlation(can_df, can_id, byte_pos, obd_df, pid_name, result):
    """
    Scatter plot: CAN byte vs PID value.
    """
    # Merge data
    can_subset = can_df[can_df['id'] == can_id][['time_sec', f'D{byte_pos}']].copy()
    merged = pd.merge_asof(
        can_subset.sort_values('time_sec'),
        obd_df[['time_sec', pid_name]].sort_values('time_sec'),
        on='time_sec',
        tolerance=0.1
    ).dropna()
    
    fig = go.Figure()
    
    fig.add_trace(go.Scatter(
        x=merged[f'D{byte_pos}'],
        y=merged[pid_name],
        mode='markers',
        marker=dict(
            size=5,
            color=merged['time_sec'],
            colorscale='Viridis',
            showscale=True,
            colorbar=dict(title="Time (s)")
        ),
        text=[f"t={t:.1f}s" for t in merged['time_sec']],
        hovertemplate='Byte: %{x}<br>PID: %{y}<br>%{text}<extra></extra>'
    ))
    
    # Add trend line
    x = merged[f'D{byte_pos}'].values
    y = merged[pid_name].values
    z = np.polyfit(x, y, 1)
    p = np.poly1d(z)
    x_line = np.linspace(x.min(), x.max(), 100)
    
    fig.add_trace(go.Scatter(
        x=x_line,
        y=p(x_line),
        mode='lines',
        name='Trend',
        line=dict(color='red', dash='dash')
    ))
    
    fig.update_layout(
        title=f"0x{can_id:03X}[{byte_pos}] vs {pid_name} | r={result['correlation']:.3f}",
        xaxis_title=f"CAN Byte {byte_pos}",
        yaxis_title=pid_name,
        showlegend=True,
        height=500
    )
    
    return fig


def create_dashboard(can_df, obd_df, correlations):
    """
    Create interactive HTML dashboard with all visualizations.
    """
    print("\n📊 Creating interactive dashboard...")
    
    # Get PIDs
    obd_pids = [col for col in obd_df.columns if col not in ['time_sec', 'timestamp_us', 'timestamp_ms']]
    
    # Create HTML
    html_parts = []
    html_parts.append("""
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>CAN-OBD Correlation Dashboard</title>
    <script src="https://cdn.plot.ly/plotly-latest.min.js"></script>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #f5f5f5; }
        h1 { color: #333; }
        .section { background: white; padding: 20px; margin: 20px 0; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
        .plot { margin: 20px 0; }
        table { width: 100%; border-collapse: collapse; margin: 20px 0; }
        th, td { padding: 12px; text-align: left; border-bottom: 1px solid #ddd; }
        th { background-color: #4CAF50; color: white; }
        tr:hover { background-color: #f5f5f5; }
        .strong-corr { font-weight: bold; color: #4CAF50; }
    </style>
</head>
<body>
    <h1>🚗 CAN-OBD Correlation Analysis Dashboard</h1>
    <div class="section">
        <h2>📋 Summary</h2>
        <p><strong>CAN Messages:</strong> """ + f"{len(can_df):,}" + """</p>
        <p><strong>OBD Records:</strong> """ + f"{len(obd_df):,}" + """</p>
        <p><strong>Strong Correlations Found:</strong> """ + f"{len(correlations)}" + """</p>
        <p><strong>Time Range:</strong> """ + f"{can_df['time_sec'].min():.1f}s - {can_df['time_sec'].max():.1f}s" + """</p>
    </div>
    """)
    
    # Correlation matrix
    if correlations:
        fig = plot_correlation_heatmap(correlations, obd_pids)
        html_parts.append('<div class="section"><h2>🔥 Correlation Heatmap</h2><div class="plot">')
        html_parts.append(fig.to_html(full_html=False, include_plotlyjs=False))
        html_parts.append('</div></div>')
    
    # Top correlations table
    html_parts.append("""
    <div class="section">
        <h2>🏆 Top Correlations</h2>
        <table>
            <tr>
                <th>Rank</th>
                <th>CAN ID</th>
                <th>Byte</th>
                <th>OBD PID</th>
                <th>Correlation</th>
                <th>Samples</th>
                <th>Formula</th>
            </tr>
    """)
    
    for i, r in enumerate(correlations[:20], 1):
        corr_class = 'strong-corr' if abs(r['correlation']) > 0.9 else ''
        html_parts.append(f"""
            <tr>
                <td>{i}</td>
                <td>0x{r['can_id']:03X}</td>
                <td>{r['byte_pos']}</td>
                <td>{r['pid_name']}</td>
                <td class="{corr_class}">{r['correlation']:.4f}</td>
                <td>{r['n_samples']}</td>
                <td><code>{r['formula']}</code></td>
            </tr>
        """)
    
    html_parts.append("</table></div>")
    
    # Individual plots for top 5
    print("  📈 Generating plots for top correlations...")
    for i, r in enumerate(correlations[:5], 1):
        print(f"    Plot {i}/5: 0x{r['can_id']:03X}[{r['byte_pos']}] vs {r['pid_name']}")
        
        html_parts.append(f'<div class="section"><h2>#{i}: 0x{r["can_id"]:03X}[{r["byte_pos"]}] vs {r["pid_name"]}</h2>')
        
        # Time series
        fig1 = plot_byte_vs_pid(can_df, r['can_id'], r['byte_pos'], obd_df, r['pid_name'], r)
        html_parts.append('<div class="plot">')
        html_parts.append(fig1.to_html(full_html=False, include_plotlyjs=False))
        html_parts.append('</div>')
        
        # Scatter
        fig2 = plot_scatter_correlation(can_df, r['can_id'], r['byte_pos'], obd_df, r['pid_name'], r)
        html_parts.append('<div class="plot">')
        html_parts.append(fig2.to_html(full_html=False, include_plotlyjs=False))
        html_parts.append('</div></div>')
    
    html_parts.append("</body></html>")
    
    return '\n'.join(html_parts)


# ============================================================================
# MAIN
# ============================================================================

def main():
    parser = argparse.ArgumentParser(
        description='CAN-OBD Correlator with Interactive Visualization',
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    
    parser.add_argument('canlog', help='GVRET format CAN log (CSV)')
    parser.add_argument('obd', help='OBD session log (CSV)')
    parser.add_argument('--html', default='correlation_dashboard.html',
                       help='Output HTML dashboard (default: correlation_dashboard.html)')
    parser.add_argument('--min-corr', type=float, default=0.7,
                       help='Minimum correlation threshold (default: 0.7)')
    
    args = parser.parse_args()
    
    # Load data
    can_df = load_gvret_can(args.canlog)
    obd_df = load_obd_session(args.obd)
    
    # Find correlations
    correlations = find_all_correlations(can_df, obd_df, args.min_corr)
    
    if not correlations:
        print("\n⚠️  No strong correlations found!")
        print("  Try lowering --min-corr threshold or check data quality")
        return
    
    # Print summary
    print("\n" + "="*60)
    print("🏆 TOP 10 CORRELATIONS")
    print("="*60)
    print(f"{'Rank':<6} {'CAN ID':<10} {'Byte':<6} {'PID':<12} {'Corr':<10} {'Formula'}")
    print("-"*60)
    
    for i, r in enumerate(correlations[:10], 1):
        print(f"{i:<6} 0x{r['can_id']:03X}      {r['byte_pos']:<6} {r['pid_name']:<12} {r['correlation']:>6.4f}    {r['formula']}")
    
    # Create dashboard
    html_content = create_dashboard(can_df, obd_df, correlations)
    
    with open(args.html, 'w', encoding='utf-8') as f:
        f.write(html_content)
    
    print(f"\n✅ Dashboard saved to: {args.html}")
    print(f"   Open in browser to explore correlations interactively!")


if __name__ == '__main__':
    main()
