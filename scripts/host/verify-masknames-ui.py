# SPDX-License-Identifier: GPL-3.0-or-later
"""Capture the actual ten-card shelf on the caller's private Xvfb display."""
import argparse
import ctypes
import re
import signal
import json
import os
from pathlib import Path
import subprocess
import time

from parity_test_environment import private_display

p = argparse.ArgumentParser()
p.add_argument('--binary', type=Path, required=True)
p.add_argument('--mesh', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
display = private_display()
assert Path('/run/.containerenv').exists(), 'Run in bts-build'
assert not os.environ.get('WAYLAND_DISPLAY')
import pyatspi
from PIL import ImageGrab

expected = ['Crease Dirt', 'Curvature edges', 'Edge Wear', 'Ground Dirt',
            'Leaks & Stains', 'Moss', 'Position height', 'Rust', 'Scratches', 'Surface Dust']
a.output.parent.mkdir(parents=True, exist_ok=True)
log = a.output.with_suffix('.log').open('w')
env = dict(os.environ, QT_LINUX_ACCESSIBILITY_ALWAYS_ON='1', QT_SCALE_FACTOR='1')
env.pop('NO_AT_BRIDGE', None)
command = ['vglrun', '-nodl', '-d', 'egl', '-c', 'proxy', str(a.binary.resolve()),
           '--no-splash', '--new-mesh', str(a.mesh.resolve())]
process = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)

def owned(candidate):
    pid = candidate.get_process_id()
    for _ in range(32):
        if pid == process.pid:
            return True
        try:
            status = Path(f'/proc/{pid}/status').read_text()
            pid = int(next(line.split()[1] for line in status.splitlines() if line.startswith('PPid:')))
        except (OSError, StopIteration):
            return False
        if pid <= 1:
            return False
    return False

def wait_for(probe, seconds=120):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f'Application exited early: {process.returncode}')
        result = probe()
        if result:
            return result
        time.sleep(.25)
    raise TimeoutError('Private UI did not reach the expected state')

def walk(node):
    yield node
    for child in node:
        yield from walk(child)

try:
    app = wait_for(lambda: next((n for n in pyatspi.Registry.getDesktop(0)
                                if n.name == 'mpainter' and owned(n)), None))
    def shown(name):
        return next((n for n in walk(app) if n.name == name
                     and n.getState().contains(pyatspi.STATE_SHOWING)), None)
    category = wait_for(lambda: shown('Mask generators'))
    bounds=category.queryComponent().getExtents(pyatspi.DESKTOP_COORDS)
    subprocess.run(['xdotool', 'mousemove', str(bounds.x+bounds.width//2),
                    str(bounds.y+bounds.height//2), 'click', '1'], check=True)
    card = wait_for(lambda: shown('Crease Dirt'))
    shelf = card.parent
    names = wait_for(lambda: ([n.name for n in shelf] if [n.name for n in shelf] == expected else None))
    assert names == expected, names
    # Allow the asynchronous GPU thumbnail jobs to finish; the dedicated GPU
    # test checks their completion and pixels independently of accessibility.
    time.sleep(12)
    frame = next(n for n in app if n.getRoleName() == 'frame')
    bounds = frame.queryComponent().getExtents(pyatspi.DESKTOP_COORDS)
    ImageGrab.grab(bbox=(bounds.x, bounds.y, bounds.x+bounds.width, bounds.y+bounds.height),
                   xdisplay=display).save(a.output)
    window_id = subprocess.check_output(['xdotool', 'search', '--pid',
                                         str(app.get_process_id()), '--name', '^'+re.escape(frame.name)+'$'], text=True).splitlines()[0]
    # Send WM_DELETE_WINDOW directly: Xvfb has no window manager to process
    # Alt+F4, and destroying the X window would bypass application shutdown.
    class ClientMessage(ctypes.Structure):
        _fields_ = [('type', ctypes.c_int), ('serial', ctypes.c_ulong),
                    ('send_event', ctypes.c_int), ('display', ctypes.c_void_p),
                    ('window', ctypes.c_ulong), ('message_type', ctypes.c_ulong),
                    ('format', ctypes.c_int), ('data', ctypes.c_long * 5)]
    class Event(ctypes.Union):
        _fields_ = [('client', ClientMessage), ('padding', ctypes.c_long * 24)]
    x11 = ctypes.CDLL('libX11.so.6')
    x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
    x11.XOpenDisplay.restype = ctypes.c_void_p
    x11.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    x11.XInternAtom.restype = ctypes.c_ulong
    x11.XSendEvent.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_long, ctypes.POINTER(Event)]
    x11.XFlush.argtypes = [ctypes.c_void_p]
    x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
    connection = x11.XOpenDisplay(display.encode())
    assert connection
    event = Event()
    event.client = ClientMessage(33, 0, 1, connection, int(window_id),
                                x11.XInternAtom(connection, b'WM_PROTOCOLS', 0), 32,
                                (ctypes.c_long * 5)(x11.XInternAtom(connection, b'WM_DELETE_WINDOW', 0), 0, 0, 0, 0))
    assert x11.XSendEvent(connection, int(window_id), 0, 0, ctypes.byref(event))
    x11.XFlush(connection)
    x11.XCloseDisplay(connection)
    deadline = time.monotonic() + 20
    while process.poll() is None and time.monotonic() < deadline:
        discard = shown('Close without Saving') or shown('Discard')
        if discard:
            assert discard.queryAction().doAction(0)
        time.sleep(.25)
    assert process.wait(timeout=10) == 0
    a.output.with_suffix('.json').write_text(json.dumps({
        'display': display, 'names': names, 'actual_shelf_screenshot': str(a.output),
        'exit_code': process.returncode, 'command': command}, indent=2) + '\n')
finally:
    if process.poll() is None:
        os.killpg(process.pid, signal.SIGTERM)
        try:
            process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
    log.close()
