#!/usr/bin/env python3
"""Check that the tray icon reflects synthetic quota values, not fixed data."""
import sys
import time
from gi.repository import Gio, GLib

bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
for _ in range(50):
    result = bus.call_sync(sys.argv[1], '/StatusNotifierItem', 'org.freedesktop.DBus.Properties',
                           'Get', GLib.Variant('(ss)', ('org.kde.StatusNotifierItem', 'IconPixmap')),
                           GLib.VariantType.new('(v)'), Gio.DBusCallFlags.NONE, 2000, None)
    width, height, pixels = result.unpack()[0][0]
    def color(x, y):
        return tuple(pixels[(y * width + x) * 4:(y * width + x) * 4 + 4])
    # Claude short 60%, Codex short 75%, no weekly values.
    if (width, height) == (24, 24) and color(4, 10) == (255, 107, 215, 155) and color(4, 9) == (255, 38, 52, 69) and color(14, 8) == (255, 118, 182, 255) and color(14, 7) == (255, 38, 52, 69) and color(9, 18) == (255, 130, 130, 130):
        print('PASS: tray icon reflects reported quota values')
        break
    time.sleep(.1)
else:
    raise SystemExit('FAIL: tray icon does not reflect fixture quota')
