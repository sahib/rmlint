#!/usr/bin/env python
# encoding: utf-8

# Internal:
from app.util import View

# External:
from gi.repository import Gtk, GLib


class ProgressView(View):
    def __init__(self, win):
        View.__init__(self, win)
        self._spinner = Gtk.Spinner()
        self._spinner.start()
        self._progress_source = None
        self.add(self._spinner)
        self.set_opacity(0.5)

    def on_view_enter(self):
        self._spinner.start()
        self._progress_source = GLib.timeout_add(
            50, lambda: self.app_window.show_progress(None) or 1
        )

    def on_view_leave(self):
        self._spinner.stop()
        if self._progress_source is not None:
            GLib.source_remove(self._progress_source)
            self.app_window.hide_progress()
