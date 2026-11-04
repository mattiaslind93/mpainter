"""Controlled Linux kernel tablet injection for verify-tablet-xi2.py.
Run on the graphical host (evdev required). A temporary virtual device is removed
on close. This does not read, grab or change the physical Wacom device.
"""
from session_guard import require_gui_allowed
require_gui_allowed()
import argparse,json,time
from pathlib import Path
from evdev import UInput,AbsInfo,ecodes as E
p=argparse.ArgumentParser();p.add_argument('ready',type=Path);a=p.parse_args();data=json.loads(a.ready.read_text())
screen=data['screen'];start=data['start'];end=data['end']
caps={E.EV_KEY:[E.BTN_TOOL_PEN,E.BTN_TOUCH,E.BTN_STYLUS,E.BTN_STYLUS2],E.EV_ABS:[(E.ABS_X,AbsInfo(0,0,32767,0,0,100)),(E.ABS_Y,AbsInfo(0,0,32767,0,0,100)),(E.ABS_PRESSURE,AbsInfo(0,0,8191,0,0,0)),(E.ABS_TILT_X,AbsInfo(0,-64,63,0,0,1)),(E.ABS_TILT_Y,AbsInfo(0,-64,63,0,0,1)),(E.ABS_DISTANCE,AbsInfo(0,0,63,0,0,0))]}
with UInput(caps,name='BTS Virtual Pressure Tablet',vendor=0x056a,product=0x0357,version=0x100,input_props=[E.INPUT_PROP_POINTER]) as ui:
 time.sleep(1)
 def sample(t,pressure):
  for code,index,axis in ((E.ABS_X,0,0),(E.ABS_Y,1,1)):
   value=start[index]+(end[index]-start[index])*t
   ui.write(E.EV_ABS,code,round((value-screen[axis])*32767/screen[axis+2]))
  ui.write(E.EV_ABS,E.ABS_PRESSURE,round(pressure*8191));ui.syn()
 ui.write(E.EV_KEY,E.BTN_TOOL_PEN,1);sample(0,0);time.sleep(.15)
 ui.write(E.EV_KEY,E.BTN_TOUCH,1);sample(0,.1)
 for i in range(1,81):sample(i/80,.1+.9*i/80);time.sleep(.0125)
 ui.write(E.EV_KEY,E.BTN_TOUCH,0);ui.write(E.EV_ABS,E.ABS_PRESSURE,0);ui.syn();time.sleep(.1);ui.write(E.EV_KEY,E.BTN_TOOL_PEN,0);ui.syn();time.sleep(.15)
