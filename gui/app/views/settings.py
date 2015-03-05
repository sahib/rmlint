#!/usr/bin/env python
# encoding: utf-8

# Stdlib:
import re
from operator import itemgetter
from functools import partial

# Internal:
from app.util import View

# External:
from gi.repository import Gtk
from gi.repository import Gio
from gi.repository import GLib
from gi.repository import GObject

# TODO: GtkRange, GtkPopdown(or likewise), Gtk

def boolean_widget(settings, key_name, summary, description):
    switch = Gtk.Switch()
    settings.bind(key_name, switch, 'active', 0)
    switch.set_active(settings.get_boolean(key_name))
    return switch


def numeric_widget(
    settings, key_name, summary, description,
    floating_point=False, draw_percent=False, draw_size=True
):
    schema = settings.get_property('settings-schema')
    key = schema.get_key(key_name)
    range_type, range_variant = key.get_range()

    step = 0.1 if floating_point else 1
    range_wdgt = Gtk.SpinButton.new_with_range(0, 10 ** 10, step)

    def _format_range_value(spin):
        adj = spin.get_adjustment()
        value = adj.get_value()
        print(value, draw_size, draw_percent)

        if draw_size:
            spin.set_text('{}'.format(value))
            return True
        elif draw_percent:
            spin.set_text('{}'.format(value * 100))
            return True
        return False

    range_wdgt.connect('value-changed', _format_range_value)

    if range_type == 'range':
        min_val, max_val = range_variant
        range_wdgt.set_range(min_val, max_val)

    settings.bind(key_name, range_wdgt, 'value', 0)

    if floating_point:
        value = settings.get_double(key_name)
    else:
        value = settings.get_int(key_name)

    range_wdgt.set_value(value)
    return range_wdgt


def numeric_range_widget(settings, key_name, summary, description, floating_point=False):
    low = numeric_widget(settings, key_name, summary, description, floating_point, draw_size=True)
    top = numeric_widget(settings, key_name, summary, description, floating_point, draw_size=True)
    box = Gtk.Box()
    box.pack_start(low, True, True, 2)
    box.pack_start(Gtk.Label(' to '), False, False, 0)
    box.pack_start(top, True, True, 2)
    return box


class _ChoiceRow(Gtk.ListBoxRow):
    def __init__(self, value):
        Gtk.ListBoxRow.__init__(self)
        self.value = value
        self.set_can_focus(False)


class _CurrentChoiceLabel(Gtk.Label):
    def __init__(self, text):
        Gtk.Label.__init__(self)

        self.set_use_markup(True)
        self.pretty_text = text

    @GObject.Property(type=str, default='')
    def choice(self):
        self._choice

    @choice.setter
    def set_choice(self, new_value):
        self._choice = new_value
        self.set_markup(
            '<u>{v}</u>'.format(
                v=self._choice.capitalize()
            )
        )

        print(new_value)
        self.notify('choice')


def choice_widget(settings, key_name, summary, description):
    schema = settings.get_property('settings-schema')
    key = schema.get_key(key_name)
    range_type, range_variant = key.get_range()

    value = settings.get_string(key_name)
    value_label = _CurrentChoiceLabel(value)
    value_label.set_use_markup(True)

    button = Gtk.Button()
    button.add(value_label)
    button.set_relief(Gtk.ReliefStyle.NONE)
    button.set_can_focus(False)

    popover = Gtk.Popover.new(button)
    popover.set_modal(True)

    listbox = Gtk.ListBox()
    listbox.set_border_width(10)

    def _update_value(_, row):
        settings.set_string(key_name, row.value)
        popover.hide()

    listbox.connect('row-activated', _update_value)
    listbox_header = Gtk.Label(
        '<small><b><span fgcolor="grey">{txt}</span></b></small>'.format(
            txt=summary
        )
    )
    listbox_header.set_use_markup(True)
    listbox_header.set_size_request(75, -1)

    for widget in Gtk.Separator(), listbox_header:
        row = Gtk.ListBoxRow()
        row.set_selectable(False)
        row.set_activatable(False)
        row.add(widget)
        listbox.prepend(row)

    frame = Gtk.Frame()
    frame.add(listbox)
    frame.set_border_width(5)
    popover.add(frame)

    if range_type == 'enum':
        for choice in range_variant:
            label = Gtk.Label(choice.capitalize())
            label.set_property('xalign', 0)

            row = _ChoiceRow(choice)
            row.add(label)
            row.set_margin_left(3)
            row.set_margin_right(3)
            listbox.add(row)

            if choice == value:
                listbox.select_row(row)

    button.connect('clicked', lambda *_: popover.show_all())
    settings.bind(key_name, value_label, 'choice', 0)

    return button


VARIANT_TO_WIDGET = {
    'b': boolean_widget,
    'i': partial(numeric_widget, floating_point=False),
    'd': partial(numeric_widget, floating_point=True),
    's': choice_widget,
    #'(ii)': numeric_range_widget
}


class SettingsView(View):
    def __init__(self, app):
        View.__init__(self, app)

        self._grid = Gtk.Grid()
        self._grid.set_margin_left(30)
        self._grid.set_margin_right(40)
        self._grid.set_margin_top(5)
        self._grid.set_margin_bottom(15)
        self._grid.set_hexpand(True)
        self._grid.set_vexpand(True)
        self._grid.set_halign(Gtk.Align.FILL)
        self._grid.set_valign(Gtk.Align.FILL)
        self.add(self._grid)

        self.save_settings = False
        self.sections = {}

    def append_section(self, heading):
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

    def append_entry(self, section, key_name, val_widget, summary, desc=None):
        desc_label = Gtk.Label(desc or '')
        summ_label = Gtk.Label(summary or '')

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

        if len(listbox) is not 0:
            sep_row = Gtk.ListBoxRow()
            sep_row.set_activatable(False)
            sep_row.add(Gtk.Separator())
            listbox.insert(sep_row, -1)

        row = Gtk.ListBoxRow()
        row.add(sub_grid)
        row.set_can_focus(False)
        listbox.insert(row, -1)

    def reset_to_defaults(self):
        for key_name in self.app.settings.list_keys():
            self.app.settings.reset(key_name)

    def fill_from_settings(self):
        gst = self.app.settings
        schema = gst.get_property('settings-schema')

        entry_rows = []

        for key_name in gst.list_keys():
            key = schema.get_key(key_name)
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

            order, order_match = 0, re.match('\[(\d+)]\s(.*)', summary)
            if order_match is not None:
                order, summary = int(order_match.group(1)), order_match.group(2)

            description = key.get_description()
            if description:
                description = '<small><span font_weight=\'ultralight\' color=\'grey\'>{}</span></small>'.format(
                    key.get_description()
                )

            # Get a fitting, readily prepared configure widget
            val_widget = constructor(gst, key_name, summary, description)

            # Try to find the section name by the keyname.
            # This is an extension made by this code and not part of GSettings.
            section = ''
            if '-' in key_name:
                section, _ = key_name.split('-', maxsplit=1)

            entry_rows.append(
                (order, section, key_name, val_widget, summary, description)
            )

        for section in sorted(set(map(itemgetter(1), entry_rows))):
            self.append_section(section.capitalize())

        for entry in sorted(entry_rows, key=itemgetter(0, 2)):
            self.append_entry(*entry[1:])

    def on_view_enter(self):
        self.save_settings = False
        self.app_window.show_action_buttons('Apply settings', 'Reset to defaults')
        self.app.settings.delay()

        # Give the buttons a specific context meaning:
        self.app_window.suggested_action.connect(
            'clicked', self.on_apply_settings
        )
        self.app_window.destructive_action.connect(
            'clicked', self.on_reset_to_defaults
        )

    def on_view_leave(self):
        self.app_window.hide_action_buttons()

        print(self.save_settings)
        if self.save_settings:
            self.app.settings.apply()
        else:
            self.app.settings.revert()

    def on_apply_settings(self, *_):
        self.save_settings = True
        self.app_window.views.switch_to_previous()

    def on_reset_to_defaults(self, *_):
        print('reset')
        self.app.settings.revert()

        GLib.timeout_add(100, lambda: self.reset_to_defaults() or self.app.settings.delay())
        # self.app.settings.delay()
        self.save_settings = False
