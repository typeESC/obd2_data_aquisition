import pandas as pd

# Load files
can = pd.read_csv('canlog_20251223_181435.csv')
obd = pd.read_csv('session_2025-12-23_18-13.csv')

# Get timestamp ranges
can_start = can.iloc[0, 0]
can_end = can.iloc[-1, 0]
obd_start = obd['timestamp_us'].iloc[0]
obd_end = obd['timestamp_us'].iloc[-1]

print("="*60)
print("ANALISE DE OVERLAP - CAN vs OBD")
print("="*60)

print(f"\nCAN timestamps:")
print(f"  Start: {can_start} ({can_start/1e6:.1f}s desde boot)")
print(f"  End:   {can_end} ({can_end/1e6:.1f}s desde boot)")
print(f"  Duration: {(can_end - can_start)/1e6:.1f}s")

print(f"\nOBD timestamps:")
print(f"  Start: {obd_start} ({obd_start/1e6:.1f}s desde boot)")
print(f"  End:   {obd_end} ({obd_end/1e6:.1f}s desde boot)")
print(f"  Duration: {(obd_end - obd_start)/1e6:.1f}s")

# Calculate overlap
overlap_start = max(can_start, obd_start)
overlap_end = min(can_end, obd_end)

print(f"\nOVERLAP:")
print(f"  Start: {overlap_start} ({overlap_start/1e6:.1f}s)")
print(f"  End:   {overlap_end} ({overlap_end/1e6:.1f}s)")
print(f"  Duration: {(overlap_end - overlap_start)/1e6:.1f}s")

# Check data in overlap
can_overlap = can[(can.iloc[:, 0] >= overlap_start) & (can.iloc[:, 0] <= overlap_end)]
obd_overlap = obd[(obd['timestamp_us'] >= overlap_start) & (obd['timestamp_us'] <= overlap_end)]

print(f"\nDADOS NO OVERLAP:")
print(f"  CAN messages: {len(can_overlap)}")
print(f"  OBD samples: {len(obd_overlap)}")

# Check RPM in overlap
rpm_data = obd_overlap[obd_overlap['rpm'] != -1]
print(f"\n  RPM válido: {len(rpm_data)} samples")
print(f"  RPM min: {rpm_data['rpm'].min():.0f}")
print(f"  RPM max: {rpm_data['rpm'].max():.0f}")
print(f"  RPM unique: {rpm_data['rpm'].nunique()}")
print(f"  RPM values: {sorted(rpm_data['rpm'].unique())}")

# Check when RPM changes
if rpm_data['rpm'].nunique() > 1:
    print(f"\n✓ RPM VARIA no overlap! Correlação é possível.")
else:
    print(f"\n❌ RPM CONSTANTE ({rpm_data['rpm'].iloc[0]:.0f}) no overlap!")
    
    # Find where RPM changes
    rpm_nonzero = obd[obd['rpm'] > 0]
    if len(rpm_nonzero) > 0:
        first_rpm_time = rpm_nonzero.iloc[0]['timestamp_us']
        print(f"\n  Primeiro RPM > 0 em: {first_rpm_time} ({first_rpm_time/1e6:.1f}s)")
        if first_rpm_time > overlap_end:
            print(f"  ⚠️  Isso é {(first_rpm_time - overlap_end)/1e6:.1f}s DEPOIS do CAN terminar!")
