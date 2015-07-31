# Stdlib:
import math
from operator import itemgetter

# External:
from gi.repository import Gtk
from gi.repository import GLib
from gi.repository import GObject


EXPONENTS = {
    'Byte': 0,
    'KB': 1,
    'MB': 2,
    'GB': 3,
    'TB': 4,
    'PB': 5
}

MAX_EXPONENT =  max(EXPONENTS.values())



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
        self._units = Gtk.ComboBoxText.new()
        self._entry = Gtk.SpinButton.new_with_range(1, 1023, 1)
        self._entry.set_wrap(True)

        # Make the combobox appear joined together:
        self.get_style_context().add_class(
            Gtk.STYLE_CLASS_LINKED
        )

        self._entry.set_hexpand(True)
        self.pack_start(self._entry, True, True, 0)
        self.pack_start(self._units, False, False, 0)

        # Fill in the usual units:
        sorted_exps = sorted(EXPONENTS.items(), key=itemgetter(1))
        for key, value in sorted_exps:
            self._units.append(str(value), key)

        self._units.set_active(1)
        self._units.set_property('has-frame', False)

        self._entry.connect('value-changed', self.on_value_changed)
        self._units.connect('changed', self.on_unit_changed)

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

        self._units.set_active_id(str(value))
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

    def on_unit_changed(self, cbox):
        """Called upon a change in the combobox"""
        label = self._units.get_active_text()
        exponent = EXPONENTS.get(label, MAX_EXPONENT)
        self._set_exponent(exponent)
        self.emit('value-changed', self.get_bytes())


class FileSizeRange(Gtk.Grid):
    """Display a range of file sizes.

    The minimum may not be higher or eqal than than the maximum.
    """
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

    @GObject.Property(type=int, default=1)
    def min_value(self):
        return self._min_wdgt.get_bytes()

    @GObject.Property(type=int, default=1024 ** 3)
    def max_value(self):
        return self._max_wdgt.get_bytes()

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


if __name__ == '__main__':
    win = Gtk.Window()
    win.connect('destroy', Gtk.main_quit)
    win.add(FileSizeRange(1, 1024 ** MAX_EXPONENT))
    win.show_all()
    Gtk.main()
