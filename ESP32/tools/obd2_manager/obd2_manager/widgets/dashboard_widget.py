"""
Dashboard Widget - Real-time gauges and charts
"""

from typing import Optional, Deque
from collections import deque
import numpy as np

from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,
    QLabel, QFrame, QGroupBox, QSizePolicy
)
from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtGui import QPainter, QColor, QPen, QFont, QBrush, QConicalGradient

import pyqtgraph as pg

from ..api_client import APIClient
from ..models import Telemetry


class GaugeWidget(QWidget):
    """Circular gauge widget for displaying values"""
    
    def __init__(self, title: str, min_val: float, max_val: float, 
                 unit: str = "", warning_val: float = None, parent=None):
        super().__init__(parent)
        self.title = title
        self.min_val = min_val
        self.max_val = max_val
        self.unit = unit
        self.warning_val = warning_val
        self.value = min_val
        
        self.setMinimumSize(150, 150)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
    
    def set_value(self, value: float):
        """Update gauge value"""
        self.value = max(self.min_val, min(self.max_val, value))
        self.update()
    
    def paintEvent(self, event):
        """Draw the gauge"""
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        
        # Calculate dimensions
        side = min(self.width(), self.height())
        margin = 10
        rect_size = side - 2 * margin
        
        center_x = self.width() // 2
        center_y = self.height() // 2
        
        # Draw background arc
        painter.setPen(QPen(QColor(60, 60, 60), 15, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        painter.drawArc(
            center_x - rect_size // 2,
            center_y - rect_size // 2,
            rect_size, rect_size,
            225 * 16, -270 * 16
        )
        
        # Calculate value angle
        value_range = self.max_val - self.min_val
        if value_range > 0:
            value_ratio = (self.value - self.min_val) / value_range
        else:
            value_ratio = 0
        value_angle = 270 * value_ratio
        
        # Determine color based on warning
        if self.warning_val and self.value >= self.warning_val:
            color = QColor(255, 80, 80)
        else:
            # Gradient from green to yellow to red
            if value_ratio < 0.5:
                color = QColor(80, 200, 80)
            elif value_ratio < 0.75:
                color = QColor(220, 180, 50)
            else:
                color = QColor(255, 120, 80)
        
        # Draw value arc
        painter.setPen(QPen(color, 15, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        painter.drawArc(
            center_x - rect_size // 2,
            center_y - rect_size // 2,
            rect_size, rect_size,
            225 * 16, -int(value_angle * 16)
        )
        
        # Draw value text
        painter.setPen(QColor(255, 255, 255))
        font = QFont("Arial", 24, QFont.Weight.Bold)
        painter.setFont(font)
        
        value_text = f"{self.value:.0f}"
        painter.drawText(
            0, center_y - 10, self.width(), 40,
            Qt.AlignmentFlag.AlignCenter, value_text
        )
        
        # Draw unit
        font.setPointSize(12)
        font.setWeight(QFont.Weight.Normal)
        painter.setFont(font)
        painter.setPen(QColor(180, 180, 180))
        painter.drawText(
            0, center_y + 20, self.width(), 25,
            Qt.AlignmentFlag.AlignCenter, self.unit
        )
        
        # Draw title
        font.setPointSize(10)
        painter.setFont(font)
        painter.drawText(
            0, self.height() - 25, self.width(), 20,
            Qt.AlignmentFlag.AlignCenter, self.title
        )


class StatusIndicator(QFrame):
    """LED-style status indicator"""
    
    def __init__(self, label: str, parent=None):
        super().__init__(parent)
        layout = QHBoxLayout(self)
        layout.setContentsMargins(5, 2, 5, 2)
        
        self.led = QLabel("●")
        self.led.setStyleSheet("color: #666; font-size: 16px;")
        layout.addWidget(self.led)
        
        self.label = QLabel(label)
        layout.addWidget(self.label)
        layout.addStretch()
    
    def set_state(self, on: bool, color: str = "#0f0"):
        """Set indicator state"""
        if on:
            self.led.setStyleSheet(f"color: {color}; font-size: 16px;")
        else:
            self.led.setStyleSheet("color: #444; font-size: 16px;")


class DashboardWidget(QWidget):
    """Real-time dashboard with gauges and charts"""
    
    HISTORY_SIZE = 120  # 60 seconds at 2Hz
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.api_client: Optional[APIClient] = None
        
        # Data history for charts
        self.time_data: Deque[float] = deque(maxlen=self.HISTORY_SIZE)
        self.rpm_data: Deque[float] = deque(maxlen=self.HISTORY_SIZE)
        self.speed_data: Deque[float] = deque(maxlen=self.HISTORY_SIZE)
        self.throttle_data: Deque[float] = deque(maxlen=self.HISTORY_SIZE)
        self.coolant_data: Deque[float] = deque(maxlen=self.HISTORY_SIZE)
        
        self._setup_ui()
    
    def _setup_ui(self):
        layout = QVBoxLayout(self)
        
        # Top section: Gauges
        gauges_group = QGroupBox("Live Gauges")
        gauges_layout = QHBoxLayout(gauges_group)
        
        self.rpm_gauge = GaugeWidget("RPM", 0, 8000, "rpm", warning_val=6500)
        self.speed_gauge = GaugeWidget("Speed", 0, 200, "km/h")
        self.coolant_gauge = GaugeWidget("Coolant", -40, 120, "°C", warning_val=100)
        self.throttle_gauge = GaugeWidget("Throttle", 0, 100, "%")
        
        gauges_layout.addWidget(self.rpm_gauge)
        gauges_layout.addWidget(self.speed_gauge)
        gauges_layout.addWidget(self.coolant_gauge)
        gauges_layout.addWidget(self.throttle_gauge)
        
        layout.addWidget(gauges_group)
        
        # Middle section: Status indicators
        status_group = QGroupBox("Status")
        status_layout = QHBoxLayout(status_group)
        
        self.ignition_led = StatusIndicator("Ignition")
        self.logging_led = StatusIndicator("Logging")
        self.mil_led = StatusIndicator("MIL")
        self.sniffer_led = StatusIndicator("Sniffer")
        
        status_layout.addWidget(self.ignition_led)
        status_layout.addWidget(self.logging_led)
        status_layout.addWidget(self.mil_led)
        status_layout.addWidget(self.sniffer_led)
        status_layout.addStretch()
        
        # Additional values
        self.voltage_label = QLabel("Voltage: --")
        self.load_label = QLabel("Load: --")
        self.maf_label = QLabel("MAF: --")
        self.fuel_label = QLabel("Fuel: --")
        
        status_layout.addWidget(self.voltage_label)
        status_layout.addWidget(self.load_label)
        status_layout.addWidget(self.maf_label)
        status_layout.addWidget(self.fuel_label)
        
        layout.addWidget(status_group)
        
        # Bottom section: Charts
        charts_group = QGroupBox("History (60s)")
        charts_layout = QVBoxLayout(charts_group)
        
        # Configure pyqtgraph
        pg.setConfigOptions(antialias=True)
        
        # RPM/Speed chart
        self.chart1 = pg.PlotWidget(title="RPM & Speed")
        self.chart1.setBackground('#1e1e1e')
        self.chart1.showGrid(x=True, y=True, alpha=0.3)
        self.chart1.setLabel('left', 'RPM', color='#ff6b6b')
        self.chart1.setLabel('bottom', 'Time', units='s')
        
        self.rpm_curve = self.chart1.plot(pen=pg.mkPen('#ff6b6b', width=2), name='RPM')
        
        # Add second Y axis for speed
        self.chart1_speed = pg.ViewBox()
        self.chart1.scene().addItem(self.chart1_speed)
        self.chart1.getAxis('right').linkToView(self.chart1_speed)
        self.chart1_speed.setXLink(self.chart1)
        self.chart1.getAxis('right').setLabel('Speed', units='km/h', color='#4ecdc4')
        self.chart1.showAxis('right')
        
        self.speed_curve = pg.PlotCurveItem(pen=pg.mkPen('#4ecdc4', width=2), name='Speed')
        self.chart1_speed.addItem(self.speed_curve)
        
        charts_layout.addWidget(self.chart1)
        
        # Throttle/Coolant chart
        self.chart2 = pg.PlotWidget(title="Throttle & Coolant")
        self.chart2.setBackground('#1e1e1e')
        self.chart2.showGrid(x=True, y=True, alpha=0.3)
        self.chart2.setLabel('left', 'Throttle', units='%', color='#ffe66d')
        self.chart2.setLabel('bottom', 'Time', units='s')
        
        self.throttle_curve = self.chart2.plot(pen=pg.mkPen('#ffe66d', width=2), name='Throttle')
        
        # Second Y axis for coolant
        self.chart2_coolant = pg.ViewBox()
        self.chart2.scene().addItem(self.chart2_coolant)
        self.chart2.getAxis('right').linkToView(self.chart2_coolant)
        self.chart2_coolant.setXLink(self.chart2)
        self.chart2.getAxis('right').setLabel('Coolant', units='°C', color='#95e1d3')
        self.chart2.showAxis('right')
        
        self.coolant_curve = pg.PlotCurveItem(pen=pg.mkPen('#95e1d3', width=2), name='Coolant')
        self.chart2_coolant.addItem(self.coolant_curve)
        
        charts_layout.addWidget(self.chart2)
        
        layout.addWidget(charts_group)
        
        # Connect view resizing
        self.chart1.getViewBox().sigResized.connect(self._update_views)
        self.chart2.getViewBox().sigResized.connect(self._update_views)
    
    def _update_views(self):
        """Sync secondary axes with main view"""
        self.chart1_speed.setGeometry(self.chart1.getViewBox().sceneBoundingRect())
        self.chart2_coolant.setGeometry(self.chart2.getViewBox().sceneBoundingRect())
    
    def set_client(self, client: Optional[APIClient]):
        """Set API client"""
        self.api_client = client
        
        # Clear history on reconnect
        self.time_data.clear()
        self.rpm_data.clear()
        self.speed_data.clear()
        self.throttle_data.clear()
        self.coolant_data.clear()
    
    def update_telemetry(self, telemetry: Telemetry):
        """Update display with new telemetry data"""
        # Update gauges
        self.rpm_gauge.set_value(telemetry.rpm)
        self.speed_gauge.set_value(telemetry.speed)
        self.coolant_gauge.set_value(telemetry.coolant)
        self.throttle_gauge.set_value(telemetry.throttle)
        
        # Update status LEDs
        ignition_on = telemetry.rpm > 300 or telemetry.voltage > 12.5
        self.ignition_led.set_state(ignition_on, "#0f0")
        self.logging_led.set_state(ignition_on, "#0af")
        self.mil_led.set_state(telemetry.mil_status > 0, "#f00")
        
        # Update labels
        self.voltage_label.setText(f"Voltage: {telemetry.voltage:.1f}V")
        self.load_label.setText(f"Load: {telemetry.load:.1f}%")
        self.maf_label.setText(f"MAF: {telemetry.maf:.2f}g/s")
        self.fuel_label.setText(f"Fuel: {telemetry.fuel_level:.1f}%")
        
        # Update history
        if len(self.time_data) == 0:
            self.time_data.append(0)
        else:
            self.time_data.append(self.time_data[-1] + 0.5)
        
        self.rpm_data.append(telemetry.rpm)
        self.speed_data.append(telemetry.speed)
        self.throttle_data.append(telemetry.throttle)
        self.coolant_data.append(telemetry.coolant)
        
        # Update charts
        time_arr = np.array(self.time_data)
        
        self.rpm_curve.setData(time_arr, np.array(self.rpm_data))
        self.speed_curve.setData(time_arr, np.array(self.speed_data))
        self.throttle_curve.setData(time_arr, np.array(self.throttle_data))
        self.coolant_curve.setData(time_arr, np.array(self.coolant_data))
        
        # Auto-range secondary axes
        self.chart1_speed.setYRange(0, 200)
        self.chart2_coolant.setYRange(-40, 120)
