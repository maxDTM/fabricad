# Passed to the FreeCAD GUI on the command line: runs once the main window is up and the
# event loop is running, writes a marker file ($GUI_READY_FILE) and exits immediately.
import os
import FreeCADGui
from PySide import QtCore


def _ready():
    mw = FreeCADGui.getMainWindow()
    with open(os.environ.get("GUI_READY_FILE", "/dev/null"), "w") as f:
        f.write(f"GUI_READY visible={mw.isVisible()} wb={FreeCADGui.activeWorkbench().name()} "
                f"docks={len(mw.findChildren(QtCore.QObject))}\n")
    os._exit(0)


QtCore.QTimer.singleShot(0, _ready)
