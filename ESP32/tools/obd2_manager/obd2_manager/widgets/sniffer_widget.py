"""
Sniffer Widget - CAN bus sniffer control
"""

from typing import Optional, Dict
from collections import defaultdict

from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,
    QGroupBox, QPushButton, QLabel, QLineEdit,
    QTableWidget, QTableWidgetItem, QHeaderView,
    QSpinBox, QCheckBox, QMessageBox, QComboBox,
    QProgressBar
)
from PyQt6.QtCore import Qt, QTimer

from ..api_client import APIClient
from ..models import SnifferStats


class SnifferWidget(QWidget):
    """CAN sniffer control widget"""
    
    def __init__(self, parent=None):
        super().__init__(parent)
        
        self.api_client: Optional[APIClient] = None
        self.stats: Optional[SnifferStats] = None
        self.can_id_stats: Dict[int, Dict] = defaultdict(lambda: {
            'count': 0, 'last_data': [0]*8, 'delta': 0
        })
        
        self._setup_ui()
        
        # Update timer
        self.update_timer = QTimer()
        self.update_timer.timeout.connect(self._update_stats)
        self.update_timer.setInterval(500)
    
    def _setup_ui(self):
        layout = QVBoxLayout(self)
        
        # Control section
        control_group = QGroupBox("Sniffer Control")
        control_layout = QHBoxLayout(control_group)
        
        self.start_btn = QPushButton("▶️ Start Sniffing")
        self.start_btn.clicked.connect(self._start_sniffing)
        control_layout.addWidget(self.start_btn)
        
        self.stop_btn = QPushButton("⏹️ Stop")
        self.stop_btn.clicked.connect(self._stop_sniffing)
        self.stop_btn.setEnabled(False)
        control_layout.addWidget(self.stop_btn)
        
        control_layout.addSpacing(20)
        
        control_layout.addWidget(QLabel("Duration (s):"))
        self.duration_spin = QSpinBox()
        self.duration_spin.setRange(10, 300)
        self.duration_spin.setValue(60)
        control_layout.addWidget(self.duration_spin)
        
        control_layout.addStretch()
        
        self.status_label = QLabel("Idle")
        self.status_label.setStyleSheet("font-weight: bold;")
        control_layout.addWidget(self.status_label)
        
        layout.addWidget(control_group)
        
        # Filter section
        filter_group = QGroupBox("Filters")
        filter_layout = QGridLayout(filter_group)
        
        filter_layout.addWidget(QLabel("CAN ID Filter:"), 0, 0)
        self.filter_id = QLineEdit()
        self.filter_id.setPlaceholderText("e.g., 7E8 or 7E0-7EF")
        filter_layout.addWidget(self.filter_id, 0, 1)
        
        filter_layout.addWidget(QLabel("Protocol:"), 0, 2)
        self.protocol_combo = QComboBox()
        self.protocol_combo.addItems(["ISO 15765-4 (CAN)", "ISO 14230 (KWP)", "Auto"])
        filter_layout.addWidget(self.protocol_combo, 0, 3)
        
        self.filter_obd = QCheckBox("OBD-II IDs only (7E0-7EF)")
        filter_layout.addWidget(self.filter_obd, 1, 0, 1, 2)
        
        self.filter_unique = QCheckBox("Show unique messages only")
        filter_layout.addWidget(self.filter_unique, 1, 2, 1, 2)
        
        layout.addWidget(filter_group)
        
        # Statistics section
        stats_group = QGroupBox("Statistics")
        stats_layout = QGridLayout(stats_group)
        
        stats_layout.addWidget(QLabel("Messages:"), 0, 0)
        self.msg_count_label = QLabel("0")
        self.msg_count_label.setStyleSheet("font-weight: bold; font-size: 16px;")
        stats_layout.addWidget(self.msg_count_label, 0, 1)
        
        stats_layout.addWidget(QLabel("Unique IDs:"), 0, 2)
        self.unique_ids_label = QLabel("0")
        self.unique_ids_label.setStyleSheet("font-weight: bold; font-size: 16px;")
        stats_layout.addWidget(self.unique_ids_label, 0, 3)
        
        stats_layout.addWidget(QLabel("Msg/sec:"), 0, 4)
        self.rate_label = QLabel("0")
        self.rate_label.setStyleSheet("font-weight: bold; font-size: 16px;")
        stats_layout.addWidget(self.rate_label, 0, 5)
        
        stats_layout.addWidget(QLabel("Errors:"), 0, 6)
        self.errors_label = QLabel("0")
        stats_layout.addWidget(self.errors_label, 0, 7)
        
        # Progress bar for duration
        stats_layout.addWidget(QLabel("Progress:"), 1, 0)
        self.progress = QProgressBar()
        self.progress.setRange(0, 100)
        stats_layout.addWidget(self.progress, 1, 1, 1, 7)
        
        layout.addWidget(stats_group)
        
        # Live message table
        msg_group = QGroupBox("Live Messages")
        msg_layout = QVBoxLayout(msg_group)
        
        self.msg_table = QTableWidget()
        self.msg_table.setColumnCount(11)
        self.msg_table.setHorizontalHeaderLabels([
            "CAN ID", "Count", "D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7", "Delta"
        ])
        
        for i in range(11):
            if i == 0:
                self.msg_table.horizontalHeader().setSectionResizeMode(i, QHeaderView.ResizeMode.ResizeToContents)
            elif i == 1:
                self.msg_table.horizontalHeader().setSectionResizeMode(i, QHeaderView.ResizeMode.ResizeToContents)
            elif i == 10:
                self.msg_table.horizontalHeader().setSectionResizeMode(i, QHeaderView.ResizeMode.Stretch)
            else:
                self.msg_table.horizontalHeader().setSectionResizeMode(i, QHeaderView.ResizeMode.Fixed)
                self.msg_table.setColumnWidth(i, 35)
        
        self.msg_table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        msg_layout.addWidget(self.msg_table)
        
        # Message controls
        msg_controls = QHBoxLayout()
        
        self.clear_btn = QPushButton("Clear")
        self.clear_btn.clicked.connect(self._clear_messages)
        msg_controls.addWidget(self.clear_btn)
        
        self.pause_btn = QPushButton("Pause")
        self.pause_btn.setCheckable(True)
        msg_controls.addWidget(self.pause_btn)
        
        msg_controls.addStretch()
        
        self.export_btn = QPushButton("Export Log")
        self.export_btn.clicked.connect(self._export_log)
        msg_controls.addWidget(self.export_btn)
        
        msg_layout.addLayout(msg_controls)
        layout.addWidget(msg_group)
    
    def set_client(self, client: Optional[APIClient]):
        """Set API client"""
        self.api_client = client
    
    def _start_sniffing(self):
        """Start CAN sniffing"""
        if not self.api_client:
            QMessageBox.warning(self, "Not Connected", "Connect to device first")
            return
        
        try:
            duration = self.duration_spin.value()
            
            # Parse filter
            filter_id = None
            filter_mask = None
            
            if self.filter_obd.isChecked():
                filter_id = 0x7E0
                filter_mask = 0x7F0  # Match 7E0-7EF
            elif self.filter_id.text():
                try:
                    filter_text = self.filter_id.text().strip()
                    if '-' in filter_text:
                        # Range filter
                        start, end = filter_text.split('-')
                        filter_id = int(start, 16)
                        # Simplified mask calculation
                        filter_mask = 0x7FF
                    else:
                        filter_id = int(filter_text, 16)
                        filter_mask = 0x7FF  # Exact match
                except ValueError:
                    QMessageBox.warning(self, "Invalid Filter", "Enter valid hex CAN ID")
                    return
            
            success = self.api_client.start_sniffer(
                duration=duration,
                filter_id=filter_id,
                filter_mask=filter_mask
            )
            
            if success:
                self.start_btn.setEnabled(False)
                self.stop_btn.setEnabled(True)
                self.status_label.setText("🔴 Sniffing...")
                self.status_label.setStyleSheet("color: #f44; font-weight: bold;")
                
                self.can_id_stats.clear()
                self.update_timer.start()
                
        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to start sniffer: {e}")
    
    def _stop_sniffing(self):
        """Stop CAN sniffing"""
        if not self.api_client:
            return
        
        try:
            self.api_client.stop_sniffer()
        except:
            pass
        
        self._on_sniff_stopped()
    
    def _on_sniff_stopped(self):
        """Handle sniff stop"""
        self.update_timer.stop()
        self.start_btn.setEnabled(True)
        self.stop_btn.setEnabled(False)
        self.status_label.setText("Stopped")
        self.status_label.setStyleSheet("font-weight: bold;")
    
    def _update_stats(self):
        """Update sniffer statistics"""
        if not self.api_client:
            return
        
        try:
            self.stats = self.api_client.get_sniffer_stats()
            
            if self.stats:
                self.msg_count_label.setText(str(self.stats.messages_captured))
                self.unique_ids_label.setText(str(self.stats.unique_ids))
                
                # Estimate rate from messages logged
                rate = self.stats.messages_logged / max(1, self.duration_spin.value())
                self.rate_label.setText(f"{rate:.1f}")
                self.errors_label.setText(str(self.stats.buffer_overflows))
                
                # Update progress (estimated from storage)
                self.progress.setValue(int(self.stats.storage_percent_used))
                
                # Check if complete (no more active sniffing)
                if self.stats.state != "ACTIVE":
                    self._on_sniff_stopped()
                
                # Update message table (if not paused)
                if not self.pause_btn.isChecked():
                    self._update_message_table()
                    
        except Exception as e:
            print(f"Stats update error: {e}")
    
    def _update_message_table(self):
        """Update message display table"""
        if not self.stats:
            return
        
        # Get latest messages (simplified - in real impl would get from device)
        # For now, use the ID count from stats
        
        sorted_ids = sorted(self.can_id_stats.keys())
        self.msg_table.setRowCount(len(sorted_ids))
        
        for i, can_id in enumerate(sorted_ids):
            stats = self.can_id_stats[can_id]
            
            # CAN ID
            id_item = QTableWidgetItem(f"0x{can_id:03X}")
            id_item.setForeground(Qt.GlobalColor.cyan)
            self.msg_table.setItem(i, 0, id_item)
            
            # Count
            self.msg_table.setItem(i, 1, QTableWidgetItem(str(stats['count'])))
            
            # Data bytes
            for j, b in enumerate(stats['last_data']):
                byte_item = QTableWidgetItem(f"{b:02X}")
                self.msg_table.setItem(i, 2 + j, byte_item)
            
            # Delta
            self.msg_table.setItem(i, 10, QTableWidgetItem(f"{stats['delta']} ms"))
    
    def update_message(self, can_id: int, data: list, delta_ms: int):
        """Update with new CAN message"""
        self.can_id_stats[can_id]['count'] += 1
        self.can_id_stats[can_id]['last_data'] = data
        self.can_id_stats[can_id]['delta'] = delta_ms
    
    def _clear_messages(self):
        """Clear message display"""
        self.can_id_stats.clear()
        self.msg_table.setRowCount(0)
    
    def _export_log(self):
        """Export sniffer log"""
        if not self.api_client:
            return
        
        QMessageBox.information(
            self, "Export",
            "Log files are saved on the ESP32.\n"
            "Use the Files tab to download them."
        )
    
    def update_data(self, stats: SnifferStats):
        """Update with new stats"""
        self.stats = stats
        self.msg_count_label.setText(str(stats.messages_captured))
        self.unique_ids_label.setText(str(stats.unique_ids))
        self.errors_label.setText(str(stats.buffer_overflows))
