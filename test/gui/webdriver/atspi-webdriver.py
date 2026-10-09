import importlib.util
import json
import sys
import pyatspi
from pathlib import Path

BASE_DRIVER_PATH = Path(__file__).parent.parent / "__webdriver" / "atspi-webdriver.py"

sys.path.insert(0, str(BASE_DRIVER_PATH.parent))
spec = importlib.util.spec_from_file_location("atspi-webdriver", str(BASE_DRIVER_PATH))
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)

app = base.app
sessions = base.sessions

# TODO:
# 1. Implement: /session/{sessionId}/window/rect
# 2. Allow gtk in _createNode2 toolkitName filtering

def session_element_rect(session_id, element_id):
    session = sessions[session_id]
    element = session.elements[element_id]
    x, y = element.queryComponent().getPosition(pyatspi.XY_SCREEN)
    if x == 0 and y == 0:
        x, y = element.queryComponent().getPosition(pyatspi.XY_WINDOW)
    w, h = element.queryComponent().getSize()

    return json.dumps({'value': {'x': x, 'y': y, 'width': w, 'height': h} }), 200, {'content-type': 'application/json'}

app.view_functions['session_element_rect'] = session_element_rect
