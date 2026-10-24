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

# FINDINGS:
# 1. nautilus:
#   - no window rect available (XY_SCREEN)
#   - no position available for elements (XY_SCREEN, XY_WINDOW)
#   - click won't work
#   - native click won't work (no screen position)

def get_window_rect(accessible):
    frameName = None
    for window in accessible:
        print("-------------")
        if window.name:
            frameName = window.name
        print(window.getRole())
        print("rect:", window.queryComponent().getPosition(pyatspi.XY_SCREEN))
        print(window.queryComponent().getExtents(pyatspi.DESKTOP_COORDS))
        print(window.queryComponent().getExtents(pyatspi.WINDOW_COORDS))
        if window.getRole() == pyatspi.ROLE_FRAME:
            x, y = window.queryComponent().getPosition(pyatspi.XY_SCREEN)
            w, h = window.queryComponent().getSize()
            # Skip windows with any dimension or position equal to 0
            if 0 in (x, y):
                continue
    if x == 0 and y == 0:
        # NOTE: even with this, nautilus won't work
        from dogtail.tree import root
        app = root.application("org.gnome.Nautilus")
        node = app.child(roleName="frame", name=frameName)
        x, y = node.position
        print(node.position, node.size)
        
    return {'x': x, 'y': y, 'width': w, 'height': h}
    # return {'x': 0, 'y': 0, 'width': 0, 'height': 0}

def session_element_rect(session_id, element_id):
    session = sessions[session_id]
    element = session.elements[element_id]
    x, y = element.queryComponent().getPosition(pyatspi.XY_SCREEN)
    if x == 0 and y == 0:
        x, y = element.queryComponent().getPosition(pyatspi.XY_WINDOW)
    w, h = element.queryComponent().getSize()

    return json.dumps({'value': {'x': x, 'y': y, 'width': w, 'height': h} }), 200, {'content-type': 'application/json'}

app.view_functions['session_element_rect'] = session_element_rect

@app.route('/session/<session_id>/window/rect', methods=['GET'])
def session_window_rect(session_id):
    session = sessions[session_id]
    window_rect = get_window_rect(session.browsing_context)

    return json.dumps({'value': window_rect}), 200, {'content-type': 'application/json'}

