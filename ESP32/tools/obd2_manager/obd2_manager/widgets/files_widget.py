"""
Files Widget - File management from ESP32
"""

from typing import Optional, List
from pathlib import Path
import os

from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout,
    QTableWidget, QTableWidgetItem, QHeaderView,
    QPushButton, QProgressBar, QLabel, QMessageBox,
    QFileDialog, QMenu
)
from PyQt6.QtCore import Qt, QThread, pyqtSignal
from PyQt6.QtGui import QAction

from ..api_client import APIClient
from ..models import FileInfo


class DownloadWorker(QThread):
    """Background worker for file download"""
    
    progress = pyqtSignal(int)
    finished = pyqtSignal(bytes)
    error = pyqtSignal(str)
    
    def __init__(self, client: APIClient, filename: str):
        super().__init__()
        self.client = client
        self.filename = filename
    
    def run(self):
        try:
            data = self.client.download_file(self.filename)
            if data:
                self.finished.emit(data)
            else:
                self.error.emit("Download failed")
        except Exception as e:
            self.error.emit(str(e))


class FilesWidget(QWidget):
    """File management widget"""
    
    file_selected = pyqtSignal(str, bytes)  # filename, data
    
    def __init__(self, parent=None):
        super().__init__(parent)
        
        self.api_client: Optional[APIClient] = None
        self.files: List[FileInfo] = []
        
        self._setup_ui()
    
    def _setup_ui(self):
        layout = QVBoxLayout(self)
        
        # Toolbar
        toolbar = QHBoxLayout()
        
        self.refresh_btn = QPushButton("🔄 Refresh")
        self.refresh_btn.clicked.connect(self.refresh_files)
        toolbar.addWidget(self.refresh_btn)
        
        self.download_btn = QPushButton("⬇️ Download")
        self.download_btn.clicked.connect(self._download_selected)
        self.download_btn.setEnabled(False)
        toolbar.addWidget(self.download_btn)
        
        self.delete_btn = QPushButton("🗑️ Delete")
        self.delete_btn.clicked.connect(self._delete_selected)
        self.delete_btn.setEnabled(False)
        toolbar.addWidget(self.delete_btn)
        
        toolbar.addStretch()
        
        self.storage_label = QLabel("")
        toolbar.addWidget(self.storage_label)
        
        layout.addLayout(toolbar)
        
        # Files table
        self.table = QTableWidget()
        self.table.setColumnCount(4)
        self.table.setHorizontalHeaderLabels(["Filename", "Size", "Type", "Actions"])
        self.table.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
        self.table.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeMode.ResizeToContents)
        self.table.horizontalHeader().setSectionResizeMode(2, QHeaderView.ResizeMode.ResizeToContents)
        self.table.horizontalHeader().setSectionResizeMode(3, QHeaderView.ResizeMode.ResizeToContents)
        self.table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self.table.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.table.customContextMenuRequested.connect(self._show_context_menu)
        self.table.itemSelectionChanged.connect(self._on_selection_changed)
        self.table.itemDoubleClicked.connect(self._on_double_click)
        layout.addWidget(self.table)
        
        # Progress bar
        self.progress = QProgressBar()
        self.progress.setVisible(False)
        layout.addWidget(self.progress)
        
        # Status
        self.status_label = QLabel("Connect to device to view files")
        self.status_label.setStyleSheet("color: #888;")
        layout.addWidget(self.status_label)
    
    def set_client(self, client: Optional[APIClient]):
        """Set API client"""
        self.api_client = client
        if client:
            self.refresh_files()
        else:
            self.table.setRowCount(0)
            self.status_label.setText("Disconnected")
    
    def refresh_files(self):
        """Refresh file list from device"""
        if not self.api_client:
            return
        
        try:
            self.files = self.api_client.list_files()
            self._populate_table()
            
            # Update storage info
            status = self.api_client.get_status()
            if status:
                used_kb = status.storage_used // 1024
                total_kb = status.storage_total // 1024
                pct = (status.storage_used / status.storage_total * 100) if status.storage_total > 0 else 0
                self.storage_label.setText(f"Storage: {used_kb} / {total_kb} KB ({pct:.1f}%)")
            
            self.status_label.setText(f"Found {len(self.files)} files")
            
        except Exception as e:
            self.status_label.setText(f"Error: {e}")
    
    def _populate_table(self):
        """Populate file table"""
        self.table.setRowCount(len(self.files))
        
        for i, f in enumerate(self.files):
            # Filename
            self.table.setItem(i, 0, QTableWidgetItem(f.name))
            
            # Size
            size_str = self._format_size(f.size)
            size_item = QTableWidgetItem(size_str)
            size_item.setTextAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
            self.table.setItem(i, 1, size_item)
            
            # Type
            file_type = self._get_file_type(f.name)
            self.table.setItem(i, 2, QTableWidgetItem(file_type))
            
            # Actions button
            actions_btn = QPushButton("...")
            actions_btn.setFixedWidth(30)
            actions_btn.clicked.connect(lambda checked, row=i: self._show_actions_menu(row))
            self.table.setCellWidget(i, 3, actions_btn)
    
    def _format_size(self, size: int) -> str:
        """Format file size"""
        if size < 1024:
            return f"{size} B"
        elif size < 1024 * 1024:
            return f"{size / 1024:.1f} KB"
        else:
            return f"{size / (1024 * 1024):.2f} MB"
    
    def _get_file_type(self, filename: str) -> str:
        """Determine file type"""
        if 'session' in filename.lower():
            return "📊 OBD Session"
        elif 'can' in filename.lower() or 'sniff' in filename.lower():
            return "📡 CAN Log"
        elif 'dtc' in filename.lower():
            return "⚠️ DTC Data"
        elif filename.endswith('.csv'):
            return "📄 CSV"
        else:
            return "📁 File"
    
    def _on_selection_changed(self):
        """Handle selection change"""
        has_selection = len(self.table.selectedItems()) > 0
        self.download_btn.setEnabled(has_selection)
        self.delete_btn.setEnabled(has_selection)
    
    def _on_double_click(self, item):
        """Handle double-click to download and open"""
        row = item.row()
        if row < len(self.files):
            self._download_and_open(row)
    
    def _show_context_menu(self, pos):
        """Show context menu"""
        item = self.table.itemAt(pos)
        if not item:
            return
        
        row = item.row()
        menu = QMenu(self)
        
        download_action = QAction("Download", self)
        download_action.triggered.connect(lambda: self._download_file(row))
        menu.addAction(download_action)
        
        open_action = QAction("Open in Analyzer", self)
        open_action.triggered.connect(lambda: self._download_and_open(row))
        menu.addAction(open_action)
        
        menu.addSeparator()
        
        delete_action = QAction("Delete", self)
        delete_action.triggered.connect(lambda: self._delete_file(row))
        menu.addAction(delete_action)
        
        menu.exec(self.table.viewport().mapToGlobal(pos))
    
    def _show_actions_menu(self, row: int):
        """Show actions menu for row"""
        btn = self.table.cellWidget(row, 3)
        if btn:
            pos = btn.mapToGlobal(btn.rect().bottomLeft())
            
            menu = QMenu(self)
            
            download_action = QAction("Download", self)
            download_action.triggered.connect(lambda: self._download_file(row))
            menu.addAction(download_action)
            
            open_action = QAction("Open in Analyzer", self)
            open_action.triggered.connect(lambda: self._download_and_open(row))
            menu.addAction(open_action)
            
            menu.addSeparator()
            
            delete_action = QAction("Delete", self)
            delete_action.triggered.connect(lambda: self._delete_file(row))
            menu.addAction(delete_action)
            
            menu.exec(pos)
    
    def _download_selected(self):
        """Download selected file"""
        rows = set(item.row() for item in self.table.selectedItems())
        for row in rows:
            self._download_file(row)
    
    def _download_file(self, row: int):
        """Download file to local disk"""
        if row >= len(self.files):
            return
        
        f = self.files[row]
        
        save_path, _ = QFileDialog.getSaveFileName(
            self, "Save File", f.name, "All Files (*)"
        )
        
        if not save_path:
            return
        
        self.progress.setVisible(True)
        self.progress.setRange(0, 0)
        
        self.worker = DownloadWorker(self.api_client, f.name)
        self.worker.finished.connect(
            lambda data: self._on_download_complete(data, save_path)
        )
        self.worker.error.connect(self._on_download_error)
        self.worker.start()
    
    def _download_and_open(self, row: int):
        """Download file and open in analyzer"""
        if row >= len(self.files):
            return
        
        f = self.files[row]
        
        self.progress.setVisible(True)
        self.progress.setRange(0, 0)
        
        self.worker = DownloadWorker(self.api_client, f.name)
        self.worker.finished.connect(
            lambda data: self._on_download_open(f.name, data)
        )
        self.worker.error.connect(self._on_download_error)
        self.worker.start()
    
    def _on_download_complete(self, data: bytes, path: str):
        """Handle download completion"""
        self.progress.setVisible(False)
        
        try:
            with open(path, 'wb') as f:
                f.write(data)
            
            self.status_label.setText(f"Downloaded: {path}")
            QMessageBox.information(self, "Download Complete", f"Saved to {path}")
            
        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to save: {e}")
    
    def _on_download_open(self, filename: str, data: bytes):
        """Handle download for opening"""
        self.progress.setVisible(False)
        self.file_selected.emit(filename, data)
    
    def _on_download_error(self, error: str):
        """Handle download error"""
        self.progress.setVisible(False)
        QMessageBox.critical(self, "Download Error", error)
    
    def _delete_selected(self):
        """Delete selected files"""
        rows = set(item.row() for item in self.table.selectedItems())
        
        if not rows:
            return
        
        files_to_delete = [self.files[r].name for r in rows if r < len(self.files)]
        
        reply = QMessageBox.question(
            self, "Confirm Delete",
            f"Delete {len(files_to_delete)} file(s)?\n" + "\n".join(files_to_delete),
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        
        if reply == QMessageBox.StandardButton.Yes:
            for filename in files_to_delete:
                self._do_delete(filename)
            self.refresh_files()
    
    def _delete_file(self, row: int):
        """Delete single file"""
        if row >= len(self.files):
            return
        
        f = self.files[row]
        
        reply = QMessageBox.question(
            self, "Confirm Delete",
            f"Delete file '{f.name}'?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        
        if reply == QMessageBox.StandardButton.Yes:
            self._do_delete(f.name)
            self.refresh_files()
    
    def _do_delete(self, filename: str):
        """Perform file deletion"""
        try:
            if self.api_client.delete_file(filename):
                self.status_label.setText(f"Deleted: {filename}")
            else:
                self.status_label.setText(f"Failed to delete: {filename}")
        except Exception as e:
            self.status_label.setText(f"Error: {e}")
