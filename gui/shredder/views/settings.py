#!/usr/bin/env python
# encoding: utf-8

"""
This is a rather generic interface for building a settings view
from a GSettingsSchema, which is (usually) defined as XML File.

For shredder this file is `org.gnome.Shredder.gschema.xml` and contains a
description of all keys used by Shredder.  Keys are typed in GSettings, so
depending on the type a suitable widget is constructed.  Changes from outside
are displayed by the widget and changes of the widget cause a change of the
key's value.

For reference, keys can be changed on the commandline too:
$ gsettings --schemadir ~/.glib-schemas set org.gnome.Shredder traverse-depth 2
"""

# Stdlib:
import re
import logging

from operator import itemgetter
from functools import partial

# External:
from gi.repository import Gtk
from gi.repository import GLib

# Internal:
from shredder.util import View, SuggestedButton, DestructiveButton
from shredder.util import FileSizeRange, MultipleChoiceButton


LOGGER = logging.getLogger('settings')


################
# TYPE WIDGETS #
################


def boolean_widget(settings, key_name, *_):
    """Provide a simple widget for a boolean option key"""
    switch = Gtk.Switch()
    settings.bind(key_name, switch, 'active', 0)
    switch.set_active(settings.get_boolean(key_name))
    return switch


def numeric_widget(settings, key_name, *_, step=1):
    """Provide a single widget to change a numeric key"""
    # Use GSetting's "reflection" to get key metadata:
    schema = settings.get_property('settings-schema')
    key = schema.get_key(key_name)
    range_type, range_variant = key.get_range()

    range_wdgt = Gtk.SpinButton.new_with_range(0, 10 ** 10, step)

    # Configure the range of the values if given:
    if range_type == 'range':
        min_val, max_val = range_variant
        range_wdgt.set_range(min_val, max_val)

    settings.bind(key_name, range_wdgt, 'value', 0)
    func = settings.get_int if step is 1 else settings.get_double
    range_wdgt.set_value(func(key_name))
    return range_wdgt


def range_widget(settings, key_name, *_):
    """Create a range widget for key types like (tt)"""
    min_val, max_val = settings.get_value(key_name)
    widget = FileSizeRange(min_val, max_val)

    def setting_changed(*_):
        """GSettings -> Widget callback."""
        min_val, max_val = settings.get_value(key_name)
        widget.min_value = min_val
        widget.max_value = max_val

    def widget_changed(*_):
        """Widget -> GSettings callback."""
        min_val, max_val = widget.min_value, widget.max_value
        settings.set_value(key_name, GLib.Variant('(tt)', (min_val, max_val)))

    # Bind manually:
    settings.connect('changed::' + key_name, setting_changed)
    widget.connect('value-changed', widget_changed)

    return widget


def choice_widget(settings, key_name, summary, _):
    """Create a widget for choosing between a list of selections"""
    schema = settings.props.settings_schema
    key = schema.get_key(key_name)

    selected = settings.get_string(key_name)
    default = key.get_default_value().get_string()

    range_type, range_variant = key.get_range()
    if range_type != 'enum':
        LOGGER.warning('%s is not an enum.', key_name)
        return

    choices = list(range_variant)
    button = MultipleChoiceButton(choices, default, selected, summary)
    button.connect(
        'row-selected',
        lambda _: settings.set_string(key_name, button.get_selected_choice())
    )

    settings.bind(key_name, button.value_label, 'choice', 0)
    return button


VARIANT_TO_WIDGET = {
    'b': boolean_widget,
    'i': partial(numeric_widget),
    'd': partial(numeric_widget, step=0.1),
    's': choice_widget,
    '(ii)': range_widget,
    '(tt)': range_widget
}


#####################
#    ACTUAL VIEW    #
#####################


class SettingsView(View):
    """Generic GSettingsView in a modern Gnome like appearance."""
    def __init__(self, app):
        View.__init__(
            self, app, sub_title='Configure how duplicates are searched'
        )

        self._grid = Gtk.Grid()
        self._grid.set_margin_start(30)
        self._grid.set_margin_end(40)
        self._grid.set_margin_top(5)
        self._grid.set_margin_bottom(15)
        self._grid.set_hexpand(True)
        self._grid.set_vexpand(True)
        self._grid.set_halign(Gtk.Align.FILL)
        self._grid.set_valign(Gtk.Align.FILL)
        self.add(self._grid)

        self.save_settings = False
        self.sections = {}

        self.appy_btn = SuggestedButton()
        self.deny_btn = DestructiveButton('Reset to defaults')

        self.appy_btn.connect('clicked', self.on_apply_settings)
        self.deny_btn.connect('clicked', self.on_reset_to_defaults)

        # Initialize from current settings:
        self.build()

    def append_section(self, heading):
        """Append a new named section for multiple entries with `heading`"""
        box = Gtk.ListBox()
        box.set_selection_mode(Gtk.SelectionMode.NONE)
        box.set_size_request(350, -1)
        box.set_hexpand(True)

        frame = Gtk.Frame()
        frame.set_halign(Gtk.Align.FILL)
        frame.add(box)

        label = Gtk.Label()
        label.set_margin_top(30)
        label.set_markup(
            '<b>{}:</b>'.format(GLib.markup_escape_text(heading, -1))
        )
        label.set_halign(Gtk.Align.START)
        label.set_margin_bottom(2)

        self.sections[heading.lower()] = box
        self._grid.attach(label, 0, len(self._grid), 1, 1)
        self._grid.attach(frame, 0, len(self._grid), 1, 1)

    def append_entry(self, section, val_widget, summary, desc=None):
        """Append an entry to a named section.

        section: A previously inserted section name.
        val_widget: The widget to show the key in.
        summary: A short summary to show.
        desc: A longer description.
        """
        desc_label = Gtk.Label(desc or '')
        summ_label = Gtk.Label(summary or '')

        desc_label.get_style_context().add_class(
            Gtk.STYLE_CLASS_DIM_LABEL
        )

        for label in desc_label, summ_label:
            label.set_use_markup(True)
            label.set_halign(Gtk.Align.FILL)
            label.set_hexpand(True)
            label.set_halign(Gtk.Align.START)

        val_widget.set_halign(Gtk.Align.END)

        sub_grid = Gtk.Grid()
        sub_grid.attach(summ_label, 0, 0, 1, 1)
        sub_grid.attach(desc_label, 0, 1, 1, 1)
        sub_grid.attach(val_widget, 1, 0, 1, 2)
        sub_grid.set_border_width(3)

        listbox = self.sections[section.lower()]
        if len(listbox):
            sep_row = Gtk.ListBoxRow()
            sep_row.set_activatable(False)
            sep_row.add(Gtk.Separator())
            listbox.insert(sep_row, -1)

        row = Gtk.ListBoxRow()
        row.add(sub_grid)
        row.set_can_focus(False)
        listbox.insert(row, -1)

    def reset_to_defaults(self):
        """Reset whole view and keys to their defaults"""
        for key_name in self.app.settings.list_keys():
            self.app.settings.reset(key_name)

    def build(self):
        """Built all entries and sections"""
        gst = self.app.settings
        entry_rows = []

        for key_name in gst.list_keys():
            key = gst.get_property('settings-schema').get_key(key_name)
            variant_key = gst.get_value(key_name)

            # Try to find a way to render this option:
            constructor = VARIANT_TO_WIDGET.get(variant_key.get_type_string())
            if constructor is None:
                continue

            # Get the key summary and description:
            summary = '{}'.format(key.get_summary())

            # This is an extension of this code:
            if summary.startswith('[hidden]'):
                continue

            order, order_grep = 0, re.match(r'\[(\d+)]\s(.*)', summary)
            if order_grep is not None:
                order, summary = int(order_grep.group(1)), order_grep.group(2)

            description = key.get_description()
            if description:
                description = '<small>{desc}</small>'.format(
                    desc=key.get_description()
                )

            # Get a fitting, readily prepared configure widget
            val_widget = constructor(gst, key_name, summary, description)

            # Try to find the section name by the keyname.
            # This is an extension made by this code and not part of GSettings.
            section = ''
            if '-' in key_name:
                section, _ = key_name.split('-', maxsplit=1)

            entry_rows.append(
                (order, section, val_widget, summary, description)
            )

        for section in sorted(set([entry[1] for entry in entry_rows])):
            self.append_section(section.capitalize())

        for entry in sorted(entry_rows, key=itemgetter(0, 2)):
            self.append_entry(*entry[1:])

    ####################
    # SIGNAL CALLBACKS #
    ####################

    def on_view_enter(self):
        """Called once the view is visible. Delay save of settings."""
        self.save_settings = False
        self.app.settings.delay()

        # Give the buttons a specific context meaning:
        self.on_key_changed(self.app.settings, None)
        self.app.settings.connect('changed', self.on_key_changed)

        self.app_window.add_header_widget(self.appy_btn)
        self.app_window.add_header_widget(self.deny_btn, Gtk.Align.START)

    def on_view_leave(self):
        """Called once the view gets out of sight. Revert or apply."""
        self.app_window.remove_header_widget(self.appy_btn)
        self.app_window.remove_header_widget(self.deny_btn)
        if self.save_settings:
            self.app.settings.apply()
        else:
            self.app.settings.revert()

    def on_apply_settings(self, *_):
        """Callback for the apply button."""
        self.save_settings = True
        self.app_window.views.switch_to_previous()

    def on_reset_to_defaults(self, *_):
        """Callback for the reset button."""
        self.app.settings.revert()

        GLib.timeout_add(
            100, lambda: self.reset_to_defaults() or self.app.settings.delay()
        )

        self.save_settings = False

    def on_key_changed(self, settings, _):
        """Called when a key in GSettings changes."""
        is_sensitive = settings.get_has_unapplied()
        self.appy_btn.set_sensitive(is_sensitive)
        self.deny_btn.set_sensitive(is_sensitive)
