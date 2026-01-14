"""
Main Window for OBD2 Manager
"""

import os
from pathlib import Path
from typing import Optional

from PyQt6.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QTabWidget,
    QLabel, QLineEdit, QPushButton, QStatusBar, QMessageBox,
    QFileDialog, QSplitter, QGroupBox, QFormLayout, QSpinBox,
    QDoubleSpinBox, QCheckBox, QComboBox, QProgressBar,
    QToolBar, QFrame
)
from PyQt6.QtCore import Qt, QTimer, QSettings, pyqtSignal
from PyQt6.QtGui import QAction, QIcon, QFont

from .api_client import APIClient
from .models import Telemetry, SystemStatus, SystemState
from .widgets.dashboard_widget import DashboardWidget
from .widgets.log_analyzer_widget import LogAnalyzerWidget
from .widgets.files_widget import FilesWidget
from .widgets.dtc_widget import DTCWidget
from .widgets.sniffer_widget import SnifferWidget
from .widgets.pid_config_widget import PIDConfigWidget


class ConnectionBar(QFrame):
    """Connection status and control bar"""
    
    connected = pyqtSignal(bool)
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.api_client: Optional[APIClient] = None
        self._setup_ui()
    
    def _setup_ui(self):
        layout = QHBoxLayout(self)
        layout.setContentsMargins(5, 5, 5, 5)
        
        # Host input
        layout.addWidget(QLabel("ESP32 Address:"))
        self.host_input = QLineEdit()
        self.host_input.setPlaceholderText("192.168.1.100 or obd2logger.local")
        self.host_input.setMinimumWidth(200)
        layout.addWidget(self.host_input)
        
        # Connect button
        self.connect_btn = QPushButton("Connect")
        self.connect_btn.clicked.connect(self._on_connect)
        layout.addWidget(self.connect_btn)
        
        # Status indicator
        self.status_label = QLabel("● Disconnected")
        self.status_label.setStyleSheet("color: #888;")
        layout.addWidget(self.status_label)
        
        layout.addStretch()
        
        # Device info
        self.device_info = QLabel("")
        layout.addWidget(self.device_info)
        
        # Load saved host
        settings = QSettings()
        saved_host = settings.value("esp32_host", "192.168.1.100")
        self.host_input.setText(saved_host)
    
    def _on_connect(self):
        host = self.host_input.text().strip()
        if not host:
            return
        
        # Save host
        settings = QSettings()
        settings.setValue("esp32_host", host)
        
        # Create client and test connection
        self.api_client = APIClient(host)
        self.connect_btn.setEnabled(False)
        self.status_label.setText("● Connecting...")
        self.status_label.setStyleSheet("color: #f90;")
        
        try:
            status = self.api_client.get_status()
            self.status_label.setText("● Connected")
            self.status_label.setStyleSheet("color: #0a0;")
            self.device_info.setText(
                f"State: {status.state} | "
                f"Storage: {status.storage_used//1024}KB / {status.storage_total//1024}KB"
            )
            self.connect_btn.setText("Disconnect")
            self.connected.emit(True)
        except Exception as e:
            self.status_label.setText(f"● Failed: {e}")
            self.status_label.setStyleSheet("color: #f00;")
            self.api_client = None
            self.connected.emit(False)
        
        self.connect_btn.setEnabled(True)
    
    def get_client(self) -> Optional[APIClient]:
        return self.api_client


class MainWindow(QMainWindow):
    """Main application window with tabbed interface"""
    
    def __init__(self):
        super().__init__()
        self.setWindowTitle("OBD2 Manager - ESP32 Smart Logger")
        self.setMinimumSize(1200, 800)
        
        self._setup_ui()
        self._setup_menu()
        self._setup_timers()
        
        # Load window geometry
        settings = QSettings()
        geometry = settings.value("window_geometry")
        if geometry:
            self.restoreGeometry(geometry)
    
    def _setup_ui(self):
        """Setup main UI layout"""
        central = QWidget()
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        
        # Connection bar at top
        self.connection_bar = ConnectionBar()
        self.connection_bar.connected.connect(self._on_connection_changed)
        layout.addWidget(self.connection_bar)
        
        # Tab widget
        self.tabs = QTabWidget()
        self.tabs.setTabPosition(QTabWidget.TabPosition.West)
        self.tabs.setDocumentMode(True)
        layout.addWidget(self.tabs)
        
        # Create tabs
        self.dashboard = DashboardWidget()
        self.log_analyzer = LogAnalyzerWidget()
        self.files_widget = FilesWidget()
        self.dtc_widget = DTCWidget()
        self.sniffer_widget = SnifferWidget()
        self.pid_config = PIDConfigWidget()
        
        self.tabs.addTab(self.dashboard, "📊 Dashboard")
        self.tabs.addTab(self.log_analyzer, "📈 Log Analyzer")
        self.tabs.addTab(self.files_widget, "📁 Files")
        self.tabs.addTab(self.dtc_widget, "🔧 DTCs")
        self.tabs.addTab(self.sniffer_widget, "🔍 Sniffer")
        self.tabs.addTab(self.pid_config, "⚙️ PID Config")
        
        # Status bar
        self.status_bar = QStatusBar()
        self.setStatusBar(self.status_bar)
        self.status_bar.showMessage("Ready - Enter ESP32 address and click Connect")
    
    def _setup_menu(self):
        """Setup menu bar"""
        menubar = self.menuBar()
        
        # File menu
        file_menu = menubar.addMenu("&File")
        
        open_action = QAction("&Open Log File...", self)
        open_action.setShortcut("Ctrl+O")
        open_action.triggered.connect(self._open_log_file)
        file_menu.addAction(open_action)
        
        file_menu.addSeparator()
        
        exit_action = QAction("E&xit", self)
        exit_action.setShortcut("Ctrl+Q")
        exit_action.triggered.connect(self.close)
        file_menu.addAction(exit_action)
        
        # View menu
        view_menu = menubar.addMenu("&View")
        
        refresh_action = QAction("&Refresh", self)
        refresh_action.setShortcut("F5")
        refresh_action.triggered.connect(self._refresh_all)
        view_menu.addAction(refresh_action)
        
        # Help menu
        help_menu = menubar.addMenu("&Help")
        
        about_action = QAction("&About", self)
        about_action.triggered.connect(self._show_about)
        help_menu.addAction(about_action)
    
    def _setup_timers(self):
        """Setup update timers"""
        # Dashboard update timer (500ms when connected)
        self.dashboard_timer = QTimer()
        self.dashboard_timer.timeout.connect(self._update_dashboard)
        
        # Status update timer (2s)
        self.status_timer = QTimer()
        self.status_timer.timeout.connect(self._update_status)
    
    def _on_connection_changed(self, connected: bool):
        """Handle connection state change"""
        client = self.connection_bar.get_client()
        
        # Pass client to all widgets
        self.dashboard.set_client(client)
        self.files_widget.set_client(client)
        self.dtc_widget.set_client(client)
        self.sniffer_widget.set_client(client)
        self.pid_config.set_client(client)
        
        if connected:
            self.dashboard_timer.start(500)
            self.status_timer.start(2000)
            self.status_bar.showMessage("Connected to ESP32")
            self._refresh_all()
        else:
            self.dashboard_timer.stop()
            self.status_timer.stop()
            self.status_bar.showMessage("Disconnected")
    
    def _update_dashboard(self):
        """Update dashboard with live data"""
        client = self.connection_bar.get_client()
        if not client:
            return
        
        try:
            telemetry = client.get_telemetry()
            self.dashboard.update_telemetry(telemetry)
        except Exception as e:
            self.status_bar.showMessage(f"Update error: {e}")
    
    def _update_status(self):
        """Update status bar and connection info"""
        client = self.connection_bar.get_client()
        if not client:
            return
        
        try:
            status = client.get_status()
            self.connection_bar.device_info.setText(
                f"State: {status.state} | "
                f"Records: {status.records} | "
                f"Storage: {status.storage_percent:.1f}% | "
                f"Heap: {status.heap_free//1024}KB"
            )
        except:
            pass
    
    def _refresh_all(self):
        """Refresh all widgets"""
        if hasattr(self.files_widget, 'refresh_files'):
            self.files_widget.refresh_files()
        if hasattr(self.dtc_widget, 'read_dtcs'):
            self.dtc_widget.read_dtcs()
        # Sniffer and PID config don't need auto-refresh
    
    def _open_log_file(self):
        """Open log file dialog"""
        filepath, _ = QFileDialog.getOpenFileName(
            self,
            "Open Log File",
            "",
            "CSV Files (*.csv);;All Files (*)"
        )
        
        if filepath:
            self.log_analyzer.load_file(filepath)
            self.tabs.setCurrentWidget(self.log_analyzer)
    
    def _show_about(self):
        """Show about dialog"""
        QMessageBox.about(
            self,
            "About OBD2 Manager",
            "<h2>OBD2 Manager</h2>"
            "<p>Version 1.0.0</p>"
            "<p>Desktop application for ESP32 OBD2 Smart Logger</p>"
            "<p>Features:</p>"
            "<ul>"
            "<li>Real-time dashboard with gauges</li>"
            "<li>Log analysis with CAN+OBD overlay</li>"
            "<li>Automatic CAN signal correlation</li>"
            "<li>File management and DTC viewer</li>"
            "</ul>"
        )
    
    def closeEvent(self, event):
        """Save settings on close"""
        settings = QSettings()
        settings.setValue("window_geometry", self.saveGeometry())
        super().closeEvent(event)
