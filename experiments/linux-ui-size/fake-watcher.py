#!/usr/bin/env python3
"""Minimal StatusNotifierWatcher for isolated protocol tests, not a tray UI."""
import os
import sys
from gi.repository import Gio, GLib

xml = '''<node><interface name="org.kde.StatusNotifierWatcher">
<method name="RegisterStatusNotifierItem"><arg type="s" direction="in"/></method>
<property name="IsStatusNotifierHostRegistered" type="b" access="read"/>
</interface></node>'''
connection = Gio.bus_get_sync(Gio.BusType.SESSION, None)
info = Gio.DBusNodeInfo.new_for_xml(xml).interfaces[0]

def method(conn, sender, path, iface, name, params, invocation):
    with open(sys.argv[1], 'w', encoding='utf-8') as f:
        f.write(params.unpack()[0])
    invocation.return_value(None)

def prop(conn, sender, path, iface, name):
    return GLib.Variant('b', True) if name == 'IsStatusNotifierHostRegistered' else None

connection.register_object('/StatusNotifierWatcher', info, method, prop, None)
Gio.bus_own_name_on_connection(connection, 'org.kde.StatusNotifierWatcher', Gio.BusNameOwnerFlags.NONE, None, None)
GLib.MainLoop().run()
