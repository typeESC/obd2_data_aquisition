#!/usr/bin/env python3
"""
OBD2 Manager - Direct Run Script
Run this file directly: python run.py

If you get import errors, install dependencies first:
    pip install PyQt6 pyqtgraph pandas numpy scipy requests matplotlib
"""

import sys
import os

# Add package to path
script_dir = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, script_dir)

def check_dependencies():
    """Check if required packages are installed"""
    missing = []
    
    packages = [
        ('PyQt6', 'PyQt6'),
        ('pyqtgraph', 'pyqtgraph'),
        ('pandas', 'pandas'),
        ('numpy', 'numpy'),
        ('scipy', 'scipy'),
        ('requests', 'requests'),
    ]
    
    for import_name, pip_name in packages:
        try:
            __import__(import_name)
        except ImportError:
            missing.append(pip_name)
    
    if missing:
        print("=" * 50)
        print("ERROR: Missing dependencies!")
        print("=" * 50)
        print()
        print("Please install with:")
        print(f"  pip install {' '.join(missing)}")
        print()
        print("Or install all at once:")
        print("  pip install -r requirements.txt")
        print()
        return False
    
    return True


def main():
    if not check_dependencies():
        sys.exit(1)
    
    # Import after checking dependencies
    from obd2_manager.main import main as app_main
    app_main()


if __name__ == "__main__":
    main()
