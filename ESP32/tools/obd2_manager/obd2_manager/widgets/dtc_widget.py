"""
DTC Widget - Diagnostic Trouble Codes viewer
"""

from typing import Optional, List

from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout,
    QTableWidget, QTableWidgetItem, QHeaderView,
    QPushButton, QLabel, QMessageBox, QGroupBox,
    QTextEdit
)
from PyQt6.QtCore import Qt

from ..api_client import APIClient
from ..models import DTCData


# Common DTC descriptions
DTC_DESCRIPTIONS = {
    "P0100": "Mass Air Flow Sensor Circuit",
    "P0101": "Mass Air Flow Sensor Range/Performance",
    "P0102": "Mass Air Flow Sensor Low Input",
    "P0103": "Mass Air Flow Sensor High Input",
    "P0110": "Intake Air Temperature Sensor Circuit",
    "P0115": "Engine Coolant Temperature Sensor Circuit",
    "P0120": "Throttle Position Sensor Circuit",
    "P0130": "O2 Sensor Circuit (Bank 1, Sensor 1)",
    "P0135": "O2 Sensor Heater Circuit (Bank 1, Sensor 1)",
    "P0171": "System Too Lean (Bank 1)",
    "P0172": "System Too Rich (Bank 1)",
    "P0300": "Random/Multiple Cylinder Misfire Detected",
    "P0301": "Cylinder 1 Misfire Detected",
    "P0302": "Cylinder 2 Misfire Detected",
    "P0303": "Cylinder 3 Misfire Detected",
    "P0304": "Cylinder 4 Misfire Detected",
    "P0400": "EGR Flow Malfunction",
    "P0420": "Catalyst System Efficiency Below Threshold (Bank 1)",
    "P0440": "EVAP Emission Control System Malfunction",
    "P0500": "Vehicle Speed Sensor Malfunction",
    "P0505": "Idle Control System Malfunction",
    "P0600": "Serial Communication Link Malfunction",
    "P0700": "Transmission Control System Malfunction",
}


class DTCWidget(QWidget):
    """DTC viewer and management"""
    
    def __init__(self, parent=None):
        super().__init__(parent)
        
        self.api_client: Optional[APIClient] = None
        self.dtc_data: Optional[DTCData] = None
        
        self._setup_ui()
    
    def _setup_ui(self):
        layout = QVBoxLayout(self)
        
        # Header
        header = QHBoxLayout()
        
        self.status_label = QLabel("No DTCs loaded")
        self.status_label.setStyleSheet("font-size: 14px; font-weight: bold;")
        header.addWidget(self.status_label)
        
        header.addStretch()
        
        self.read_btn = QPushButton("🔍 Read DTCs")
        self.read_btn.clicked.connect(self.read_dtcs)
        header.addWidget(self.read_btn)
        
        self.clear_btn = QPushButton("🗑️ Clear DTCs")
        self.clear_btn.clicked.connect(self.clear_dtcs)
        self.clear_btn.setEnabled(False)
        header.addWidget(self.clear_btn)
        
        layout.addLayout(header)
        
        # Status info
        info_group = QGroupBox("ECU Information")
        info_layout = QHBoxLayout(info_group)
        
        self.mil_label = QLabel("MIL: --")
        info_layout.addWidget(self.mil_label)
        
        self.dtc_count_label = QLabel("DTC Count: --")
        info_layout.addWidget(self.dtc_count_label)
        
        self.tests_label = QLabel("Tests Available: --")
        info_layout.addWidget(self.tests_label)
        
        info_layout.addStretch()
        
        layout.addWidget(info_group)
        
        # DTC Table
        dtc_group = QGroupBox("Diagnostic Trouble Codes")
        dtc_layout = QVBoxLayout(dtc_group)
        
        self.table = QTableWidget()
        self.table.setColumnCount(4)
        self.table.setHorizontalHeaderLabels(["Code", "Type", "Status", "Description"])
        self.table.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.ResizeToContents)
        self.table.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeMode.ResizeToContents)
        self.table.horizontalHeader().setSectionResizeMode(2, QHeaderView.ResizeMode.ResizeToContents)
        self.table.horizontalHeader().setSectionResizeMode(3, QHeaderView.ResizeMode.Stretch)
        self.table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self.table.itemClicked.connect(self._on_dtc_clicked)
        dtc_layout.addWidget(self.table)
        
        layout.addWidget(dtc_group)
        
        # Detail view
        detail_group = QGroupBox("DTC Details")
        detail_layout = QVBoxLayout(detail_group)
        
        self.detail_text = QTextEdit()
        self.detail_text.setReadOnly(True)
        self.detail_text.setMaximumHeight(150)
        self.detail_text.setStyleSheet("background-color: #2d2d2d; color: #ddd; font-family: monospace;")
        detail_layout.addWidget(self.detail_text)
        
        layout.addWidget(detail_group)
    
    def set_client(self, client: Optional[APIClient]):
        """Set API client"""
        self.api_client = client
    
    def read_dtcs(self):
        """Read DTCs from vehicle"""
        if not self.api_client:
            QMessageBox.warning(self, "Not Connected", "Connect to device first")
            return
        
        try:
            self.dtc_data = self.api_client.read_dtc()
            
            if self.dtc_data:
                self._update_display()
                self.clear_btn.setEnabled(True)
            else:
                self.status_label.setText("Failed to read DTCs")
                
        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to read DTCs: {e}")
    
    def _update_display(self):
        """Update display with DTC data"""
        if not self.dtc_data:
            return
        
        # Get all codes (confirmed + pending)
        all_codes = self.dtc_data.confirmed + self.dtc_data.pending
        dtc_count = len(all_codes)
        
        if dtc_count == 0:
            self.status_label.setText("✅ No DTCs found")
            self.status_label.setStyleSheet("font-size: 14px; font-weight: bold; color: #0a0;")
        else:
            self.status_label.setText(f"⚠️ {dtc_count} DTC(s) found")
            self.status_label.setStyleSheet("font-size: 14px; font-weight: bold; color: #fa0;")
        
        # MIL status
        if self.dtc_data.mil_on:
            self.mil_label.setText("MIL: 🔴 ON")
            self.mil_label.setStyleSheet("color: #f44;")
        else:
            self.mil_label.setText("MIL: ⚫ OFF")
            self.mil_label.setStyleSheet("color: #888;")
        
        self.dtc_count_label.setText(f"DTC Count: {dtc_count}")
        self.tests_label.setText(f"Confirmed: {self.dtc_data.confirmed_count}, Pending: {self.dtc_data.pending_count}")
        
        # Populate table
        self._populate_table()
    
    def _populate_table(self):
        """Populate DTC table"""
        if not self.dtc_data:
            return
        
        # Combine confirmed and pending codes
        all_codes = []
        for code in self.dtc_data.confirmed:
            all_codes.append((code, "Confirmed"))
        for code in self.dtc_data.pending:
            all_codes.append((code, "Pending"))
        
        self.table.setRowCount(len(all_codes))
        
        for i, (code, status) in enumerate(all_codes):
            # Parse DTC code
            dtc_type = self._get_dtc_type(code)
            description = DTC_DESCRIPTIONS.get(code, "Unknown code")
            
            # Code
            code_item = QTableWidgetItem(code)
            code_item.setForeground(Qt.GlobalColor.yellow)
            self.table.setItem(i, 0, code_item)
            
            # Type
            self.table.setItem(i, 1, QTableWidgetItem(dtc_type))
            
            # Status
            status_item = QTableWidgetItem(status)
            if status == "Confirmed":
                status_item.setForeground(Qt.GlobalColor.red)
            else:
                status_item.setForeground(Qt.GlobalColor.gray)
            self.table.setItem(i, 2, status_item)
            
            # Description
            self.table.setItem(i, 3, QTableWidgetItem(description))
    
    def _get_dtc_type(self, code: str) -> str:
        """Get DTC type from code prefix"""
        if not code:
            return "Unknown"
        
        prefix = code[0].upper()
        types = {
            'P': "Powertrain",
            'B': "Body",
            'C': "Chassis",
            'U': "Network"
        }
        return types.get(prefix, "Unknown")
    
    def _on_dtc_clicked(self, item):
        """Handle DTC row click"""
        row = item.row()
        
        # Get all codes list
        all_codes = self.dtc_data.confirmed + self.dtc_data.pending
        
        if row >= len(all_codes):
            return
        
        code = all_codes[row]
        status = "Confirmed" if row < len(self.dtc_data.confirmed) else "Pending"
        description = DTC_DESCRIPTIONS.get(code, "Unknown code - no description available")
        
        detail = f"""
DTC Code: {code}
Type: {self._get_dtc_type(code)}
Status: {status}

Description:
{description}

Possible Causes:
- Sensor malfunction
- Wiring issue
- ECU software problem
- Related component failure

Recommended Actions:
1. Verify the fault with a diagnostic tool
2. Check related sensors and wiring
3. Clear code and monitor for recurrence
"""
        
        self.detail_text.setPlainText(detail.strip())
    
    def clear_dtcs(self):
        """Clear all DTCs"""
        if not self.api_client:
            return
        
        reply = QMessageBox.question(
            self, "Confirm Clear",
            "Clear all Diagnostic Trouble Codes?\n\n"
            "This will also turn off the MIL (Check Engine Light).",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        
        if reply != QMessageBox.StandardButton.Yes:
            return
        
        try:
            success = self.api_client.clear_dtc()
            
            if success:
                QMessageBox.information(self, "Success", "DTCs cleared successfully")
                self.read_dtcs()  # Re-read to verify
            else:
                QMessageBox.warning(self, "Failed", "Failed to clear DTCs")
                
        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to clear DTCs: {e}")
    
    def update_data(self, dtc_data: DTCData):
        """Update with new DTC data"""
        self.dtc_data = dtc_data
        self._update_display()
