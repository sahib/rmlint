#!/usr/bin/env python
# encoding: utf-8

# Stdlib:
import math
from operator import itemgetter

# External:
from gi.repository import Gtk
from gi.repository import Gio
from gi.repository import GObject


###############################
# Multiple Choice List Widget #
###############################


class ChoiceRow(Gtk.ListBoxRow):
    """Row representing a single choice"""
    def __init__(self, value, is_default):
        Gtk.ListBoxRow.__init__(self)

        self.value, self.is_default = value, is_default

        self.set_can_focus(False)
        self.set_margin_left(3)
        self.set_margin_right(3)

        self.symbol = Gtk.Image.new_from_gicon(
            Gio.ThemedIcon(name='emblem-ok-symbolic'),
            Gtk.IconSize.BUTTON
        )
        self.symbol.props.margin_start = 10
        self.symbol.set_no_show_all(True)

        label = Gtk.Label(value.capitalize())
        label.props.xalign = 0

        box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        box.pack_start(label, True, True, 0)
        box.pack_start(self.symbol, False, False, 0)
        self.add(box)

    def set_show_checkmark(self, state):
        self.symbol.set_visible(state or self.is_default)

        CHECKMARK_TABLE = {
            (False, False): (None, False),
            (False, True): ('emblem-ok-symbolic', False),
            (True, False): ('non-starred-symbolic', True),
            (True, True): ('starred-symbolic', False)
        }

        icon_name, dim_down = CHECKMARK_TABLE[(self.is_default, bool(state))]
        if icon_name is not None:
            self.symbol.set_from_gicon(
                Gio.ThemedIcon(name=icon_name), Gtk.IconSize.BUTTON
            )

        ctx = self.symbol.get_style_context()
        if dim_down:
            ctx.add_class(Gtk.STYLE_CLASS_DIM_LABEL)
            self.symbol.set_opacity(0.5)
        else:
            ctx.remove_class(Gtk.STYLE_CLASS_DIM_LABEL)
            self.symbol.set_opacity(1.0)


class CurrentChoiceLabel(Gtk.Label):
    """Helper class for displaying the current choice as label"""
    def __init__(self, text):
        Gtk.Label.__init__(self)
        self.set_use_markup(True)
        self.set_choice(text)

    @GObject.Property(type=str, default='')
    def choice(self):
        self._choice

    def set_choice(self, new_value):
        self._choice = new_value
        self.set_markup(
            '<u>{v}</u>'.format(
                v=self._choice.capitalize()
            )
        )

        self.notify('choice')


class MultipleChoiceButton(Gtk.Button):
    __gsignals__ = {
        'row-selected': (GObject.SIGNAL_RUN_FIRST, None, ()),
    }

    def __init__(self, values, default, selected, summary):
        Gtk.Button.__init__(self)
        self._selected_row = None

        self.set_relief(Gtk.ReliefStyle.NONE)
        self.set_can_focus(False)

        # Visible part when not selected:
        self.value_label = CurrentChoiceLabel(default)
        self.value_label.set_choice(selected)
        self.add(self.value_label)

        popover = Gtk.Popover.new(self)
        popover.set_modal(True)
        self.connect('clicked', lambda *_: popover.show_all())

        self.listbox = Gtk.ListBox()
        self.listbox.set_border_width(10)
        self.listbox.set_selection_mode(Gtk.SelectionMode.NONE)
        self.listbox.set_activate_on_single_click(True)
        self.listbox.connect('row-activated', self.on_update_value, popover)

        # Createh the header:
        listbox_header = Gtk.Label(
            '<small><b>{txt}?</b></small>'.format(txt=summary)
        )
        listbox_header.get_style_context().add_class(
            Gtk.STYLE_CLASS_DIM_LABEL
        )
        listbox_header.set_use_markup(True)
        listbox_header.set_size_request(90, -1)

        for widget in Gtk.Separator(), listbox_header:
            row = Gtk.ListBoxRow()
            row.set_selectable(False)
            row.set_activatable(False)
            row.add(widget)
            self.listbox.prepend(row)

        # Add a decorative frame:
        frame = Gtk.Frame()
        frame.add(self.listbox)
        frame.set_border_width(5)
        popover.add(frame)

        # Populate the rows:
        for choice in values:
            row = ChoiceRow(choice, default == choice)
            self.listbox.add(row)

            if choice == selected:
                self.listbox.select_row(row)
                row.set_show_checkmark(True)

        # Show the popup once the button was clicked:
        self.connect('clicked', lambda *_: popover.show_all())

    def _set_current_row(self, row):
        for other_row in self.listbox:
            # Might be a different type:
            if isinstance(other_row, ChoiceRow):
                other_row.set_show_checkmark(row is other_row)

        self.value_label.set_choice(row.value)
        self._selected_row = row

    def get_selected_choice(self):
        """Return the currently selcted label text"""
        return self._selected_row.value

    def set_selected_choice(self, value):
        """Set the choice of the widget by name"""
        for row in self.listbox:
            if isinstance(row, ChoiceRow):
                if row.value == value:
                    self._set_current_row(row)

    def on_update_value(self, listbox, row, popover):
        self._set_current_row(row)
        popover.hide()

        # Notify users:
        self.emit('row-selected')


############################
# File size ranger chooser #
############################


EXPONENTS = {
    'Byte': 0,
    'Kilobyte': 1,
    'Megabyte': 2,
    'Gigabyte': 3,
    'Terrabye': 4,
    'Petabyte': 5
}


MAX_EXPONENT =  max(EXPONENTS.values())
SORTED_KEYS = sorted(EXPONENTS.items(), key=itemgetter(1))


class FileSizeSpinButton(Gtk.Box):
    """Widget to choose a file size in the usual units.

    Works mostly like a GtkSpinButon (and consists of one).
    """
    __gsignals__ = {
        'value-changed': (GObject.SIGNAL_RUN_FIRST, None, (int, ))
    }

    def __init__(self):
        Gtk.Box.__init__(self, orientation=Gtk.Orientation.HORIZONTAL)

        self._last_val, self._last_exp = 1, 1
        self._units = MultipleChoiceButton(
            [label for label, _ in SORTED_KEYS],
            'MB', 'MB', 'Unit'
        )
        self._entry = Gtk.SpinButton.new_with_range(1, 1023, 1)
        self._entry.set_wrap(True)

        # Make the buttons appear to be joined together:
        self.get_style_context().add_class(
            Gtk.STYLE_CLASS_LINKED
        )

        self._entry.set_hexpand(True)
        self.pack_start(self._entry, True, True, 0)
        self.pack_start(self._units, False, False, 0)

        self._entry.connect('value-changed', self.on_value_changed)
        self._units.connect('row-selected', self.on_unit_changed)

    def get_bytes(self):
        """Get the current number of displayed bytes"""
        return self._last_val * (1024 ** self._curr_exp)

    def set_bytes(self, size):
        """Set the current number of displayed bytes"""
        # Find out what unit to use:
        if size is 0:
           exponent = 1
        else:
           exponent = math.floor(math.log(size, 1024))

        # Convert raw size to a factor
        display_size = size / (1024 ** exponent)

        # Update state:
        self._entry.set_value(display_size)
        self._set_exponent(exponent)
        self._last_val = display_size

    def _set_exponent(self, exponent):
        # linear search for the correct label
        for key, value in EXPONENTS.items():
            if value == exponent:
                break

        self._units.set_selected_choice(key)
        self._curr_exp = value

    def on_value_changed(self, spbtn):
        """Called upon a change in the spinbutton"""
        curr = spbtn.get_value_as_int()

        if curr == 1023 and self._last_val == 1:
            # Wrap from max to min
            self._set_exponent(self._curr_exp - 1)

        if curr == 1 and self._last_val == 1023:
            # Wrap from min to max
            self._set_exponent(self._curr_exp + 1)

        self._last_val = curr
        self.emit('value-changed', curr)

    def on_unit_changed(self, _):
        """Called upon a change in the combobox"""
        label = self._units.get_selected_choice()
        exponent = EXPONENTS.get(label, MAX_EXPONENT)
        self._set_exponent(exponent)
        self.emit('value-changed', self.get_bytes())


class FileSizeRange(Gtk.Grid):
    """Display a range of file sizes.

    The minimum may not be higher or eqal than than the maximum.
    """
    __gsignals__ = {
        'value-changed': (GObject.SIGNAL_RUN_FIRST, None, ())
    }

    def __init__(self, min_val, max_val):
        Gtk.Grid.__init__(self)
        self._min_wdgt = FileSizeSpinButton()
        self._max_wdgt = FileSizeSpinButton()
        self._min_wdgt.set_bytes(min_val)
        self._max_wdgt.set_bytes(max_val)

        self._min_wdgt.set_hexpand(True)
        self._max_wdgt.set_hexpand(True)

        self.attach(self._min_wdgt, 0, 0, 1, 1)
        self.attach(Gtk.Label(' to '), 1, 0, 1, 1)
        self.attach(self._max_wdgt, 2, 0, 1, 1)

        self._min_wdgt.connect('value-changed', self.on_value_changed)
        self._max_wdgt.connect('value-changed', self.on_value_changed)

    @property
    def min_value(self):
        return self._min_wdgt.get_bytes()

    @min_value.setter
    def min_value(self, val):
        self._min_wdgt.set_bytes(val)

    @property
    def max_value(self):
        return self._max_wdgt.get_bytes()

    @max_value.setter
    def max_value(self, val):
        self._max_wdgt.set_bytes(val)

    def on_value_changed(self, wdgt, _):
        min_val = self._min_wdgt.get_bytes()
        max_val = self._max_wdgt.get_bytes()

        # Add constraint to ensure min < max
        # by at least one byte (not always visible)
        if min_val >= max_val:
            if wdgt is self._max_wdgt:
                self._max_wdgt.set_bytes(min_val + 1)
            else:
                self._min_wdgt.set_bytes(max_val - 1)

        self.emit('value-changed')


if __name__ == '__main__':
    win = Gtk.Window()
    win.connect('destroy', Gtk.main_quit)
    win.add(FileSizeRange(1, 1024 ** MAX_EXPONENT))
    win.show_all()
    Gtk.main()
