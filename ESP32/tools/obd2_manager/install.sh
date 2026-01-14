#!/bin/bash
# OBD2 Manager - Installation Script for Linux/Mac
echo "===================================="
echo "  OBD2 Manager Installer"
echo "===================================="
echo

# Check Python
if ! command -v python3 &> /dev/null; then
    echo "ERROR: Python3 not found! Please install Python 3.9+"
    exit 1
fi

echo "Installing dependencies..."
pip3 install PyQt6 pyqtgraph pandas numpy scipy requests matplotlib

if [ $? -ne 0 ]; then
    echo
    echo "ERROR: Failed to install some dependencies."
    echo "Try running: pip3 install -r requirements.txt"
    exit 1
fi

echo
echo "===================================="
echo "  Installation Complete!"
echo "===================================="
echo
echo "To run the application:"
echo "  python3 -m obd2_manager"
echo
echo "Or install as package:"
echo "  pip3 install -e ."
echo "  obd2-manager"
echo
