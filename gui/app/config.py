#!/usr/bin/env python
# encoding: utf-8

# Stdlib:
import os

# External:
from gi.repository import Gio


class Config:
    SCHEMA_ID = 'org.gnome.Rmlint'

    def __init__(self):
        schema_source = Gio.SettingsSchemaSource.new_from_directory(
            os.path.expanduser("~/glib-schemas"),
            Gio.SettingsSchemaSource.get_default(),
            False
        )

        schema = schema_source.lookup(Config.SCHEMA_ID, False)
        self._gst = Gio.Settings.new_full(schema, None, None)
