#!/usr/bin/env python
# encoding: utf-8

# External:
from gi.repository import Gtk
from gi.repository import GObject


class ShredderChartStack(Gtk.Stack):
    STATE_LOADING, STATE_GROUP, STATE_DIRECTORY = range(3)

    def __init__(self):
        Gtk.Stack.__init__(self)

        # Initial state is alawys loading.
        self._stack_state = ShredderChartStack.STATE_LOADING

        self.spinner = Gtk.Spinner()
        self.spinner.start()

    @property
    def _state(self):
        return self._stack_state

    @_state.setter
    def _state(self, new_state):
        self._stack_state = new_state
        self.set_visible_child(self.widget)
        self.notify('widget')

    @GObject.Property(type=Gtk.Widget, default=None)
    def widget(self):
        return {
            ShredderChartStack.STATE_LOADING: self.spinner,
            ShredderChartStack.STATE_GROUP: None,
            ShredderChartStack.STATE_DIRECTORY: None
        }.get(self._state)
