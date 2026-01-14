"""
Log Analyzer Widget - OBD+CAN overlay analysis
"""

import os
from typing import Optional, List, Tuple
from pathlib import Path

from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QSplitter,
    QLabel, QPushButton, QFileDialog, QGroupBox,
    QComboBox, QCheckBox, QListWidget, QListWidgetItem,
    QTableWidget, QTableWidgetItem, QHeaderView,
    QProgressBar, QMessageBox, QSpinBox, QDoubleSpinBox,
    QTabWidget, QTextEdit
)
from PyQt6.QtCore import Qt, QThread, pyqtSignal

import numpy as np
import pandas as pd

import pyqtgraph as pg

from ..log_parser import LogParser
from ..correlation_engine import CorrelationEngine, CorrelationResult


class CorrelationWorker(QThread):
    """Background worker for correlation analysis"""
    
    progress = pyqtSignal(int)
    finished = pyqtSignal(list)
    error = pyqtSignal(str)
    
    def __init__(self, can_df, obd_df, min_correlation=0.8):
        super().__init__()
        self.can_df = can_df
        self.obd_df = obd_df
        self.min_correlation = min_correlation
    
    def run(self):
        try:
            engine = CorrelationEngine()
            results = engine.auto_correlate(
                self.can_df, self.obd_df, 
                min_correlation=self.min_correlation
            )
            self.finished.emit(results)
        except Exception as e:
            self.error.emit(str(e))


class LogAnalyzerWidget(QWidget):
    """Log analysis with CAN+OBD overlay and correlation"""
    
    def __init__(self, parent=None):
        super().__init__(parent)
        
        self.obd_df: Optional[pd.DataFrame] = None
        self.can_df: Optional[pd.DataFrame] = None
        self.correlations: List[CorrelationResult] = []
        
        self._setup_ui()
    
    def _setup_ui(self):
        layout = QVBoxLayout(self)
        
        # File loading section
        files_group = QGroupBox("Load Log Files")
        files_layout = QHBoxLayout(files_group)
        
        # OBD file
        files_layout.addWidget(QLabel("OBD Session:"))
        self.obd_path_label = QLabel("Not loaded")
        self.obd_path_label.setStyleSheet("color: #888;")
        files_layout.addWidget(self.obd_path_label)
        
        obd_btn = QPushButton("Browse...")
        obd_btn.clicked.connect(lambda: self._load_file('obd'))
        files_layout.addWidget(obd_btn)
        
        files_layout.addSpacing(20)
        
        # CAN file
        files_layout.addWidget(QLabel("CAN Log:"))
        self.can_path_label = QLabel("Not loaded")
        self.can_path_label.setStyleSheet("color: #888;")
        files_layout.addWidget(self.can_path_label)
        
        can_btn = QPushButton("Browse...")
        can_btn.clicked.connect(lambda: self._load_file('can'))
        files_layout.addWidget(can_btn)
        
        files_layout.addStretch()
        
        layout.addWidget(files_group)
        
        # Main splitter
        splitter = QSplitter(Qt.Orientation.Horizontal)
        
        # Left panel: controls
        left_panel = QWidget()
        left_layout = QVBoxLayout(left_panel)
        
        # OBD signal selection
        obd_group = QGroupBox("OBD Signals")
        obd_layout = QVBoxLayout(obd_group)
        
        self.obd_signals_list = QListWidget()
        self.obd_signals_list.setSelectionMode(QListWidget.SelectionMode.MultiSelection)
        self.obd_signals_list.itemSelectionChanged.connect(self._update_plot)
        obd_layout.addWidget(self.obd_signals_list)
        
        left_layout.addWidget(obd_group)
        
        # CAN ID selection
        can_group = QGroupBox("CAN Signals")
        can_layout = QVBoxLayout(can_group)
        
        # CAN ID filter
        id_layout = QHBoxLayout()
        id_layout.addWidget(QLabel("CAN ID:"))
        self.can_id_combo = QComboBox()
        self.can_id_combo.currentIndexChanged.connect(self._on_can_id_changed)
        id_layout.addWidget(self.can_id_combo)
        can_layout.addLayout(id_layout)
        
        # Byte selection
        self.byte_checks = []
        byte_layout = QHBoxLayout()
        for i in range(8):
            cb = QCheckBox(f"B{i}")
            cb.stateChanged.connect(self._update_plot)
            self.byte_checks.append(cb)
            byte_layout.addWidget(cb)
        can_layout.addLayout(byte_layout)
        
        # 16-bit mode
        self.word_mode = QCheckBox("16-bit mode (combine bytes)")
        self.word_mode.stateChanged.connect(self._update_plot)
        can_layout.addWidget(self.word_mode)
        
        # Endianness
        endian_layout = QHBoxLayout()
        endian_layout.addWidget(QLabel("Endian:"))
        self.endian_combo = QComboBox()
        self.endian_combo.addItems(["Big", "Little"])
        self.endian_combo.currentIndexChanged.connect(self._update_plot)
        endian_layout.addWidget(self.endian_combo)
        endian_layout.addStretch()
        can_layout.addLayout(endian_layout)
        
        left_layout.addWidget(can_group)
        
        # Correlation section
        corr_group = QGroupBox("Auto Correlation")
        corr_layout = QVBoxLayout(corr_group)
        
        thresh_layout = QHBoxLayout()
        thresh_layout.addWidget(QLabel("Min correlation:"))
        self.corr_threshold = QDoubleSpinBox()
        self.corr_threshold.setRange(0.5, 1.0)
        self.corr_threshold.setValue(0.8)
        self.corr_threshold.setSingleStep(0.05)
        thresh_layout.addWidget(self.corr_threshold)
        corr_layout.addLayout(thresh_layout)
        
        self.correlate_btn = QPushButton("🔍 Find Correlations")
        self.correlate_btn.clicked.connect(self._run_correlation)
        self.correlate_btn.setEnabled(False)
        corr_layout.addWidget(self.correlate_btn)
        
        self.corr_progress = QProgressBar()
        self.corr_progress.setVisible(False)
        corr_layout.addWidget(self.corr_progress)
        
        left_layout.addWidget(corr_group)
        
        splitter.addWidget(left_panel)
        
        # Right panel: charts and results
        right_panel = QWidget()
        right_layout = QVBoxLayout(right_panel)
        
        # Chart tabs
        chart_tabs = QTabWidget()
        
        # Overlay chart
        overlay_tab = QWidget()
        overlay_layout = QVBoxLayout(overlay_tab)
        
        self.overlay_chart = pg.PlotWidget(title="OBD + CAN Overlay")
        self.overlay_chart.setBackground('#1e1e1e')
        self.overlay_chart.showGrid(x=True, y=True, alpha=0.3)
        self.overlay_chart.setLabel('left', 'OBD Value')
        self.overlay_chart.setLabel('bottom', 'Time', units='s')
        self.overlay_chart.addLegend()
        
        # Secondary Y axis for CAN
        self.can_viewbox = pg.ViewBox()
        self.overlay_chart.scene().addItem(self.can_viewbox)
        self.overlay_chart.getAxis('right').linkToView(self.can_viewbox)
        self.can_viewbox.setXLink(self.overlay_chart)
        self.overlay_chart.getAxis('right').setLabel('CAN Value')
        self.overlay_chart.showAxis('right')
        
        overlay_layout.addWidget(self.overlay_chart)
        chart_tabs.addTab(overlay_tab, "📊 Overlay")
        
        # Correlation results
        results_tab = QWidget()
        results_layout = QVBoxLayout(results_tab)
        
        self.results_table = QTableWidget()
        self.results_table.setColumnCount(7)
        self.results_table.setHorizontalHeaderLabels([
            "CAN ID", "Bytes", "PID", "Correlation", "Scale", "Offset", "Formula"
        ])
        self.results_table.horizontalHeader().setSectionResizeMode(
            6, QHeaderView.ResizeMode.Stretch
        )
        self.results_table.itemDoubleClicked.connect(self._on_result_clicked)
        results_layout.addWidget(self.results_table)
        
        export_btn = QPushButton("📤 Export to DBC")
        export_btn.clicked.connect(self._export_dbc)
        results_layout.addWidget(export_btn)
        
        chart_tabs.addTab(results_tab, "📋 Correlations")
        
        right_layout.addWidget(chart_tabs)
        
        splitter.addWidget(right_panel)
        splitter.setSizes([300, 700])
        
        layout.addWidget(splitter)
        
        # Store plot items
        self.obd_curves = {}
        self.can_curves = {}
    
    def _load_file(self, file_type: str):
        """Load OBD or CAN file"""
        filepath, _ = QFileDialog.getOpenFileName(
            self, f"Open {file_type.upper()} File", "",
            "CSV Files (*.csv);;All Files (*)"
        )
        
        if not filepath:
            return
        
        try:
            if file_type == 'obd':
                self.obd_df = LogParser.load_obd_session(filepath)
                self.obd_path_label.setText(os.path.basename(filepath))
                self.obd_path_label.setStyleSheet("color: #0a0;")
                self._populate_obd_signals()
            else:
                self.can_df = LogParser.load_can_log(filepath)
                self.can_path_label.setText(os.path.basename(filepath))
                self.can_path_label.setStyleSheet("color: #0a0;")
                self._populate_can_ids()
            
            # Enable correlation if both loaded
            if self.obd_df is not None and self.can_df is not None:
                self.correlate_btn.setEnabled(True)
                # Synchronize timestamps
                self.obd_df, self.can_df = LogParser.load_paired_logs(
                    self.obd_path_label.text(),  # Note: need full path
                    self.can_path_label.text()
                ) if False else (self.obd_df, self.can_df)  # Skip re-sync
            
        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to load file: {e}")
    
    def load_file(self, filepath: str):
        """Load file (called from main window)"""
        file_type = LogParser.detect_file_type(filepath)
        
        if file_type == 'obd':
            self.obd_df = LogParser.load_obd_session(filepath)
            self.obd_path_label.setText(os.path.basename(filepath))
            self.obd_path_label.setStyleSheet("color: #0a0;")
            self._populate_obd_signals()
        elif file_type == 'can':
            self.can_df = LogParser.load_can_log(filepath)
            self.can_path_label.setText(os.path.basename(filepath))
            self.can_path_label.setStyleSheet("color: #0a0;")
            self._populate_can_ids()
    
    def _populate_obd_signals(self):
        """Populate OBD signals list"""
        self.obd_signals_list.clear()
        
        if self.obd_df is None:
            return
        
        # Available numeric columns
        exclude = ['timestamp_us', 'time_s', 'valid_mask']
        for col in self.obd_df.columns:
            if col not in exclude and self.obd_df[col].dtype in ['float64', 'int64', 'float32', 'int32']:
                item = QListWidgetItem(col)
                self.obd_signals_list.addItem(item)
    
    def _populate_can_ids(self):
        """Populate CAN ID dropdown"""
        self.can_id_combo.clear()
        
        if self.can_df is None:
            return
        
        # Get unique CAN IDs sorted
        can_ids = sorted(self.can_df['id'].unique())
        
        for cid in can_ids:
            # Count messages for this ID
            count = len(self.can_df[self.can_df['id'] == cid])
            self.can_id_combo.addItem(f"0x{cid:03X} ({count} msgs)", cid)
    
    def _on_can_id_changed(self, index):
        """Handle CAN ID selection change"""
        self._update_plot()
    
    def _update_plot(self):
        """Update overlay plot with selected signals"""
        # Clear existing curves
        self.overlay_chart.clear()
        self.can_viewbox.clear()
        
        colors = ['#ff6b6b', '#4ecdc4', '#ffe66d', '#95e1d3', '#a8e6cf', '#dcedc1']
        color_idx = 0
        
        # Plot OBD signals
        if self.obd_df is not None:
            selected_obd = [item.text() for item in self.obd_signals_list.selectedItems()]
            
            for col in selected_obd:
                if col in self.obd_df.columns:
                    curve = self.overlay_chart.plot(
                        self.obd_df['time_s'].values,
                        self.obd_df[col].values,
                        pen=pg.mkPen(colors[color_idx % len(colors)], width=2),
                        name=col
                    )
                    color_idx += 1
        
        # Plot CAN signals
        if self.can_df is not None and self.can_id_combo.currentIndex() >= 0:
            can_id = self.can_id_combo.currentData()
            filtered = self.can_df[self.can_df['id'] == can_id]
            
            if len(filtered) > 0:
                selected_bytes = [i for i, cb in enumerate(self.byte_checks) if cb.isChecked()]
                
                if self.word_mode.isChecked() and len(selected_bytes) >= 2:
                    # 16-bit mode
                    hi, lo = selected_bytes[0], selected_bytes[1]
                    endian = self.endian_combo.currentText().lower()
                    
                    if endian == 'big':
                        values = filtered[f'd{hi}'] * 256 + filtered[f'd{lo}']
                    else:
                        values = filtered[f'd{lo}'] * 256 + filtered[f'd{hi}']
                    
                    curve = pg.PlotCurveItem(
                        filtered['time_s'].values,
                        values.values,
                        pen=pg.mkPen('#ff9f43', width=2, style=Qt.PenStyle.DashLine),
                        name=f"CAN 0x{can_id:03X} [{hi},{lo}]"
                    )
                    self.can_viewbox.addItem(curve)
                else:
                    # Individual bytes
                    for byte_idx in selected_bytes:
                        col = f'd{byte_idx}'
                        if col in filtered.columns:
                            curve = pg.PlotCurveItem(
                                filtered['time_s'].values,
                                filtered[col].values,
                                pen=pg.mkPen(colors[(color_idx + byte_idx) % len(colors)], 
                                           width=2, style=Qt.PenStyle.DashLine),
                                name=f"CAN B{byte_idx}"
                            )
                            self.can_viewbox.addItem(curve)
        
        # Sync viewbox
        self.can_viewbox.setGeometry(self.overlay_chart.getViewBox().sceneBoundingRect())
    
    def _run_correlation(self):
        """Run auto-correlation analysis"""
        if self.obd_df is None or self.can_df is None:
            return
        
        self.correlate_btn.setEnabled(False)
        self.corr_progress.setVisible(True)
        self.corr_progress.setRange(0, 0)  # Indeterminate
        
        self.worker = CorrelationWorker(
            self.can_df, self.obd_df,
            min_correlation=self.corr_threshold.value()
        )
        self.worker.finished.connect(self._on_correlation_done)
        self.worker.error.connect(self._on_correlation_error)
        self.worker.start()
    
    def _on_correlation_done(self, results: List[CorrelationResult]):
        """Handle correlation completion"""
        self.correlations = results
        self.correlate_btn.setEnabled(True)
        self.corr_progress.setVisible(False)
        
        # Populate results table
        self.results_table.setRowCount(len(results))
        
        for i, r in enumerate(results):
            self.results_table.setItem(i, 0, QTableWidgetItem(f"0x{r.can_id:03X}"))
            self.results_table.setItem(i, 1, QTableWidgetItem(
                f"[{r.byte_pos}]" if r.length == 1 else f"[{r.byte_pos},{r.byte_pos+1}] {r.endian}"
            ))
            self.results_table.setItem(i, 2, QTableWidgetItem(r.pid_name))
            self.results_table.setItem(i, 3, QTableWidgetItem(f"{r.correlation:.3f}"))
            self.results_table.setItem(i, 4, QTableWidgetItem(f"{r.scale:.6f}"))
            self.results_table.setItem(i, 5, QTableWidgetItem(f"{r.offset:.2f}"))
            self.results_table.setItem(i, 6, QTableWidgetItem(r.formula))
        
        QMessageBox.information(
            self, "Correlation Complete",
            f"Found {len(results)} signal correlations"
        )
    
    def _on_correlation_error(self, error: str):
        """Handle correlation error"""
        self.correlate_btn.setEnabled(True)
        self.corr_progress.setVisible(False)
        QMessageBox.critical(self, "Error", f"Correlation failed: {error}")
    
    def _on_result_clicked(self, item):
        """Handle double-click on result to show in chart"""
        row = item.row()
        if row < len(self.correlations):
            r = self.correlations[row]
            
            # Set CAN ID
            for i in range(self.can_id_combo.count()):
                if self.can_id_combo.itemData(i) == r.can_id:
                    self.can_id_combo.setCurrentIndex(i)
                    break
            
            # Set bytes
            for cb in self.byte_checks:
                cb.setChecked(False)
            
            if r.length == 2:
                self.word_mode.setChecked(True)
                self.byte_checks[r.byte_pos].setChecked(True)
                if r.byte_pos + 1 < 8:
                    self.byte_checks[r.byte_pos + 1].setChecked(True)
                self.endian_combo.setCurrentText(r.endian.capitalize())
            else:
                self.word_mode.setChecked(False)
                self.byte_checks[r.byte_pos].setChecked(True)
            
            # Select OBD signal
            for i in range(self.obd_signals_list.count()):
                item = self.obd_signals_list.item(i)
                item.setSelected(item.text() == r.pid_name)
            
            self._update_plot()
    
    def _export_dbc(self):
        """Export correlations to DBC file"""
        if not self.correlations:
            QMessageBox.warning(self, "No Data", "Run correlation first")
            return
        
        filepath, _ = QFileDialog.getSaveFileName(
            self, "Export DBC", "discovered_signals.dbc",
            "DBC Files (*.dbc)"
        )
        
        if filepath:
            engine = CorrelationEngine()
            engine.export_to_dbc(self.correlations, filepath)
            QMessageBox.information(self, "Export Complete", f"Saved to {filepath}")
