"""
PID Configuration Widget - Configure polling tiers and PID selection
"""

from typing import Optional, List, Dict
from dataclasses import dataclass

from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,
    QGroupBox, QPushButton, QLabel, QTreeWidget,
    QTreeWidgetItem, QHeaderView, QMessageBox,
    QComboBox, QCheckBox, QSpinBox, QDialog,
    QDialogButtonBox, QFormLayout, QLineEdit
)
from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor, QBrush

from ..api_client import APIClient
from ..models import PIDConfig, PollTier, TIER_INTERVALS


# Standard OBD-II PIDs with descriptions
STANDARD_PIDS = {
    0x04: ("Calculated Engine Load", "%", "ENGINE_LOAD"),
    0x05: ("Coolant Temperature", "°C", "COOLANT_TEMP"),
    0x06: ("Short Term Fuel Trim B1", "%", "STFT_B1"),
    0x07: ("Long Term Fuel Trim B1", "%", "LTFT_B1"),
    0x0A: ("Fuel Pressure", "kPa", "FUEL_PRESSURE"),
    0x0B: ("Intake Manifold Pressure", "kPa", "INTAKE_PRESSURE"),
    0x0C: ("Engine RPM", "rpm", "RPM"),
    0x0D: ("Vehicle Speed", "km/h", "SPEED"),
    0x0E: ("Timing Advance", "°", "TIMING_ADVANCE"),
    0x0F: ("Intake Air Temperature", "°C", "INTAKE_TEMP"),
    0x10: ("MAF Air Flow Rate", "g/s", "MAF"),
    0x11: ("Throttle Position", "%", "THROTTLE_POS"),
    0x1C: ("OBD Standard", "", "OBD_STANDARD"),
    0x1F: ("Run Time Since Start", "s", "RUN_TIME"),
    0x21: ("Distance with MIL", "km", "DIST_MIL"),
    0x2C: ("Commanded EGR", "%", "EGR"),
    0x2F: ("Fuel Tank Level", "%", "FUEL_LEVEL"),
    0x30: ("Warm-ups Since Clear", "", "WARMUPS"),
    0x31: ("Distance Since Clear", "km", "DIST_CLR"),
    0x33: ("Barometric Pressure", "kPa", "BARO"),
    0x42: ("Control Module Voltage", "V", "CONTROL_VOLTAGE"),
    0x43: ("Absolute Load Value", "%", "ABS_LOAD"),
    0x45: ("Relative Throttle Position", "%", "REL_THROTTLE"),
    0x46: ("Ambient Air Temperature", "°C", "AMBIENT_TEMP"),
    0x49: ("Accelerator Pedal Position D", "%", "ACCEL_POS_D"),
    0x4A: ("Accelerator Pedal Position E", "%", "ACCEL_POS_E"),
    0x4C: ("Commanded Throttle Actuator", "%", "CMD_THROTTLE"),
    0x5C: ("Engine Oil Temperature", "°C", "OIL_TEMP"),
    0x5E: ("Engine Fuel Rate", "L/h", "FUEL_RATE"),
}

TIER_COLORS = {
    PollTier.CRITICAL: QColor(255, 100, 100),  # Red
    PollTier.HIGH: QColor(255, 180, 100),       # Orange
    PollTier.MEDIUM: QColor(255, 255, 100),     # Yellow
    PollTier.LOW: QColor(100, 255, 100),        # Green
}


class AddPIDDialog(QDialog):
    """Dialog to add custom PID"""
    
    def __init__(self, parent=None):
        super().__init__(parent)
        
        self.setWindowTitle("Add Custom PID")
        self.setMinimumWidth(300)
        
        layout = QFormLayout(self)
        
        self.pid_input = QLineEdit()
        self.pid_input.setPlaceholderText("e.g., 2F or 0x2F")
        layout.addRow("PID (hex):", self.pid_input)
        
        self.name_input = QLineEdit()
        layout.addRow("Name:", self.name_input)
        
        self.unit_input = QLineEdit()
        layout.addRow("Unit:", self.unit_input)
        
        self.tier_combo = QComboBox()
        self.tier_combo.addItems(["CRITICAL", "HIGH", "MEDIUM", "LOW"])
        self.tier_combo.setCurrentIndex(2)  # Default to MEDIUM
        layout.addRow("Tier:", self.tier_combo)
        
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel
        )
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout.addRow(buttons)
    
    def get_pid_config(self) -> Optional[PIDConfig]:
        """Get PID config from dialog"""
        try:
            pid_text = self.pid_input.text().strip()
            if pid_text.startswith("0x"):
                pid = int(pid_text, 16)
            else:
                pid = int(pid_text, 16)
            
            name = self.name_input.text().strip() or f"PID_{pid:02X}"
            unit = self.unit_input.text().strip()
            tier = PollTier[self.tier_combo.currentText()]
            
            return PIDConfig(
                pid=pid,
                name=name,
                unit=unit,
                tier=tier,
                enabled=True
            )
        except ValueError:
            return None


class PIDConfigWidget(QWidget):
    """PID configuration widget"""
    
    def __init__(self, parent=None):
        super().__init__(parent)
        
        self.api_client: Optional[APIClient] = None
        self.pid_configs: List[PIDConfig] = []
        
        self._setup_ui()
        self._load_default_pids()
    
    def _setup_ui(self):
        layout = QVBoxLayout(self)
        
        # Tier info
        info_group = QGroupBox("Polling Tiers")
        info_layout = QGridLayout(info_group)
        
        for i, tier in enumerate(PollTier):
            label = QLabel(tier.name)
            label.setStyleSheet(f"background-color: {TIER_COLORS[tier].name()}; "
                              f"padding: 4px; border-radius: 3px; color: black;")
            info_layout.addWidget(label, 0, i)
            
            interval_label = QLabel(f"{TIER_INTERVALS[tier]}ms")
            interval_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
            info_layout.addWidget(interval_label, 1, i)
        
        layout.addWidget(info_group)
        
        # PID tree
        tree_group = QGroupBox("PID Configuration")
        tree_layout = QVBoxLayout(tree_group)
        
        # Toolbar
        toolbar = QHBoxLayout()
        
        self.add_btn = QPushButton("➕ Add PID")
        self.add_btn.clicked.connect(self._add_pid)
        toolbar.addWidget(self.add_btn)
        
        self.remove_btn = QPushButton("➖ Remove")
        self.remove_btn.clicked.connect(self._remove_pid)
        toolbar.addWidget(self.remove_btn)
        
        toolbar.addSpacing(20)
        
        toolbar.addWidget(QLabel("Move to:"))
        self.move_combo = QComboBox()
        self.move_combo.addItems(["CRITICAL", "HIGH", "MEDIUM", "LOW"])
        toolbar.addWidget(self.move_combo)
        
        self.move_btn = QPushButton("Move")
        self.move_btn.clicked.connect(self._move_to_tier)
        toolbar.addWidget(self.move_btn)
        
        toolbar.addStretch()
        
        self.refresh_btn = QPushButton("🔄 Refresh")
        self.refresh_btn.clicked.connect(self._refresh_from_device)
        toolbar.addWidget(self.refresh_btn)
        
        self.apply_btn = QPushButton("📤 Apply to Device")
        self.apply_btn.clicked.connect(self._apply_to_device)
        toolbar.addWidget(self.apply_btn)
        
        tree_layout.addLayout(toolbar)
        
        # Tree widget
        self.tree = QTreeWidget()
        self.tree.setColumnCount(5)
        self.tree.setHeaderLabels(["PID", "Name", "Unit", "Tier", "Enabled"])
        self.tree.header().setSectionResizeMode(0, QHeaderView.ResizeMode.ResizeToContents)
        self.tree.header().setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
        self.tree.header().setSectionResizeMode(2, QHeaderView.ResizeMode.ResizeToContents)
        self.tree.header().setSectionResizeMode(3, QHeaderView.ResizeMode.ResizeToContents)
        self.tree.header().setSectionResizeMode(4, QHeaderView.ResizeMode.ResizeToContents)
        self.tree.setSelectionMode(QTreeWidget.SelectionMode.ExtendedSelection)
        self.tree.setDragDropMode(QTreeWidget.DragDropMode.InternalMove)
        self.tree.itemChanged.connect(self._on_item_changed)
        tree_layout.addWidget(self.tree)
        
        layout.addWidget(tree_group)
        
        # Summary
        summary_group = QGroupBox("Summary")
        summary_layout = QHBoxLayout(summary_group)
        
        self.summary_labels = {}
        for tier in PollTier:
            vbox = QVBoxLayout()
            tier_label = QLabel(tier.name)
            tier_label.setStyleSheet(f"background-color: {TIER_COLORS[tier].name()}; "
                                   f"padding: 2px; color: black;")
            vbox.addWidget(tier_label)
            
            count_label = QLabel("0 PIDs")
            count_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
            self.summary_labels[tier] = count_label
            vbox.addWidget(count_label)
            
            summary_layout.addLayout(vbox)
        
        layout.addWidget(summary_group)
    
    def _load_default_pids(self):
        """Load default PID configuration"""
        # Default tier assignments
        tier_assignments = {
            PollTier.CRITICAL: [0x0C, 0x0D, 0x11, 0x05],  # RPM, Speed, Throttle, Coolant
            PollTier.HIGH: [0x04, 0x0F, 0x10, 0x33],      # Load, IAT, MAF, Baro
            PollTier.MEDIUM: [0x06, 0x07, 0x0B, 0x42, 0x43, 0x45, 0x49],
            PollTier.LOW: [0x1F, 0x2F, 0x30, 0x31, 0x46, 0x5C, 0x5E]
        }
        
        self.pid_configs.clear()
        
        for tier, pids in tier_assignments.items():
            for pid in pids:
                if pid in STANDARD_PIDS:
                    name, unit, _ = STANDARD_PIDS[pid]
                    self.pid_configs.append(PIDConfig(
                        pid=pid,
                        name=name,
                        unit=unit,
                        tier=tier,
                        enabled=True
                    ))
        
        self._populate_tree()
    
    def _populate_tree(self):
        """Populate tree with PID configs"""
        self.tree.clear()
        
        # Group by tier
        tier_items = {}
        for tier in PollTier:
            tier_item = QTreeWidgetItem([tier.name, "", "", "", ""])
            tier_item.setBackground(0, QBrush(TIER_COLORS[tier]))
            tier_item.setForeground(0, QBrush(QColor(0, 0, 0)))
            tier_item.setExpanded(True)
            tier_items[tier] = tier_item
            self.tree.addTopLevelItem(tier_item)
        
        # Add PIDs
        for config in sorted(self.pid_configs, key=lambda x: x.pid):
            item = QTreeWidgetItem()
            item.setText(0, f"0x{config.pid:02X}")
            item.setText(1, config.name)
            item.setText(2, config.unit)
            item.setText(3, config.tier.name)
            item.setCheckState(4, Qt.CheckState.Checked if config.enabled else Qt.CheckState.Unchecked)
            item.setData(0, Qt.ItemDataRole.UserRole, config)
            item.setFlags(item.flags() | Qt.ItemFlag.ItemIsUserCheckable)
            
            tier_items[config.tier].addChild(item)
        
        self._update_summary()
    
    def _update_summary(self):
        """Update tier summary"""
        for tier in PollTier:
            count = sum(1 for c in self.pid_configs if c.tier == tier and c.enabled)
            self.summary_labels[tier].setText(f"{count} PIDs")
    
    def _on_item_changed(self, item, column):
        """Handle item change"""
        if column == 4:  # Enabled checkbox
            config = item.data(0, Qt.ItemDataRole.UserRole)
            if config:
                config.enabled = item.checkState(4) == Qt.CheckState.Checked
                self._update_summary()
    
    def _add_pid(self):
        """Add new PID"""
        dialog = AddPIDDialog(self)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            config = dialog.get_pid_config()
            if config:
                # Check for duplicate
                if any(c.pid == config.pid for c in self.pid_configs):
                    QMessageBox.warning(self, "Duplicate", f"PID 0x{config.pid:02X} already exists")
                    return
                
                self.pid_configs.append(config)
                self._populate_tree()
    
    def _remove_pid(self):
        """Remove selected PID"""
        selected = self.tree.selectedItems()
        
        for item in selected:
            config = item.data(0, Qt.ItemDataRole.UserRole)
            if config:
                self.pid_configs.remove(config)
        
        self._populate_tree()
    
    def _move_to_tier(self):
        """Move selected PIDs to tier"""
        tier_name = self.move_combo.currentText()
        tier = PollTier[tier_name]
        
        selected = self.tree.selectedItems()
        
        for item in selected:
            config = item.data(0, Qt.ItemDataRole.UserRole)
            if config:
                config.tier = tier
        
        self._populate_tree()
    
    def set_client(self, client: Optional[APIClient]):
        """Set API client"""
        self.api_client = client
    
    def _refresh_from_device(self):
        """Refresh config from device"""
        if not self.api_client:
            QMessageBox.warning(self, "Not Connected", "Connect to device first")
            return
        
        try:
            configs = self.api_client.get_pid_config()
            if configs:
                self.pid_configs = configs
                self._populate_tree()
                QMessageBox.information(self, "Success", "Configuration loaded from device")
            else:
                QMessageBox.warning(self, "Failed", "Could not load configuration")
        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to load: {e}")
    
    def _apply_to_device(self):
        """Apply config to device"""
        if not self.api_client:
            QMessageBox.warning(self, "Not Connected", "Connect to device first")
            return
        
        # Validate
        critical_count = sum(1 for c in self.pid_configs if c.tier == PollTier.CRITICAL and c.enabled)
        if critical_count > 6:
            QMessageBox.warning(
                self, "Too Many PIDs",
                f"CRITICAL tier has {critical_count} PIDs.\n"
                "Maximum recommended is 6 for stable timing."
            )
            return
        
        try:
            success = self.api_client.set_pid_config(self.pid_configs)
            if success:
                QMessageBox.information(self, "Success", "Configuration applied to device")
            else:
                QMessageBox.warning(self, "Failed", "Could not apply configuration")
        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to apply: {e}")
    
    def get_configs(self) -> List[PIDConfig]:
        """Get current configurations"""
        return self.pid_configs
