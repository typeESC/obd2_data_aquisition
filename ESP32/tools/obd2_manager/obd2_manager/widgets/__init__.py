"""
Widget modules for OBD2 Manager
"""
from .dashboard_widget import DashboardWidget
from .log_analyzer_widget import LogAnalyzerWidget
from .files_widget import FilesWidget
from .dtc_widget import DTCWidget
from .sniffer_widget import SnifferWidget
from .pid_config_widget import PIDConfigWidget

__all__ = [
    'DashboardWidget',
    'LogAnalyzerWidget',
    'FilesWidget',
    'DTCWidget',
    'SnifferWidget',
    'PIDConfigWidget',
]