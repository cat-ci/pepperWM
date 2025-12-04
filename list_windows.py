#!/usr/bin/env python3
"""
List all user-facing application windows (LXDE/Openbox/X11)
with Title, PID, Process, Position (x,y), and Size (width,height).

Outputs JSON.
Requires: python3-xlib
"""

import json
from Xlib import X, display, Xatom


def get_window_name(d, w):
    """Retrieve window name (title)."""
    name = None
    atom_net_wm_name = d.intern_atom("_NET_WM_NAME")
    atom_utf8_string = d.intern_atom("UTF8_STRING")

    prop = w.get_property(atom_net_wm_name, atom_utf8_string, 0, 1024)
    if prop and prop.value:
        name = prop.value.decode("utf-8", errors="ignore")

    if not name:
        atom_wm_name = d.intern_atom("WM_NAME")
        prop = w.get_property(atom_wm_name, X.AnyPropertyType, 0, 1024)
        if prop and prop.value:
            try:
                name = prop.value.decode("utf-8", errors="ignore")
            except UnicodeDecodeError:
                name = prop.value.decode("latin1", errors="ignore")

    return name or "(no title)"


def get_window_pid(d, w):
    """Retrieve window process ID (PID)."""
    atom_pid = d.intern_atom("_NET_WM_PID")
    prop = w.get_property(atom_pid, Xatom.CARDINAL, 0, 1024)
    if prop:
        return int(prop.value[0])
    return None


def get_proc_name(pid):
    """Return process name using /proc/PID/comm."""
    try:
        with open(f"/proc/{pid}/comm", "r", encoding="utf-8") as f:
            return f.read().strip()
    except (FileNotFoundError, PermissionError, TypeError):
        return "(unknown)"


def is_normal_window(d, w):
    """Check if the window type is _NET_WM_WINDOW_TYPE_NORMAL."""
    atom_window_type = d.intern_atom("_NET_WM_WINDOW_TYPE")
    atom_normal = d.intern_atom("_NET_WM_WINDOW_TYPE_NORMAL")

    prop = w.get_property(atom_window_type, Xatom.ATOM, 0, 1024)
    if prop:
        return any(t == atom_normal for t in prop.value)
    return False


def get_window_geometry(w):
    """
    Get window geometry — returns dict(x, y, width, height)
    Adjusts coordinates to screen coordinates including parent offsets.
    """
    try:
        geom = w.get_geometry()
        x, y = geom.x, geom.y
        width, height = geom.width, geom.height

        # Walk up through parents to get absolute position
        try:
            parent = w.query_tree().parent
            while parent:
                pg = parent.get_geometry()
                x += pg.x
                y += pg.y
                if parent.id == w.display.screen().root.id:
                    break
                parent = parent.query_tree().parent
        except Exception:
            pass

        return {"x": x, "y": y, "width": width, "height": height}
    except Exception:
        return {"x": 0, "y": 0, "width": 0, "height": 0}


def list_windows():
    """Get information for all visible 'normal' windows."""
    d = display.Display()
    root = d.screen().root

    atom_client_list = d.intern_atom("_NET_CLIENT_LIST")
    prop = root.get_property(atom_client_list, X.AnyPropertyType, 0, 1024)

    windows = []
    if prop:
        for window_id in prop.value:
            w = d.create_resource_object("window", window_id)

            if not is_normal_window(d, w):
                continue

            title = get_window_name(d, w)
            if not title.strip():
                continue

            pid = get_window_pid(d, w)
            proc = get_proc_name(pid) if pid else "(no pid)"

            geom = get_window_geometry(w)

            windows.append(
                {
                    "Title": title,
                    "PID": pid,
                    "Process": proc,
                    "X": geom["x"],
                    "Y": geom["y"],
                    "Width": geom["width"],
                    "Height": geom["height"],
                }
            )
    return windows


if __name__ == "__main__":
    windows = list_windows()
    print(json.dumps(windows, indent=2))
