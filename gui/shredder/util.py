#!/usr/bin/env python
# encoding: utf-8

"""
Misc widgets used throughout the code.
This code here is supposed to be drop-in able
into other projects if necessary.

Also included: Various specialised GtkCellRenderer implementation.
Reason for special derivates are given in the respective class.
"""

# Stdlib:
import math

from datetime import datetime
from operator import itemgetter
from enum import Enum

# External:
from gi.repository import Gtk
from gi.repository import Gdk
from gi.repository import Gio
from gi.repository import GObject
from gi.repository import Pango, PangoCairo
from gi.repository import GLib


def size_to_human_readable(size):
    """Convert a size in bytes to a human readable string."""
    human_readable = ''

    if size > 0:
        for unit in ['', 'K', 'M', 'G', 'T', 'P', 'E', 'Z']:
            if abs(size) >= 1024.0:
                size /= 1024.0
                continue

            # Hack to split off the unneeded .0
            size = round(size, 1) if size % 1 else int(size)
            human_readable = "{s} {f}B".format(s=size, f=unit)
            break
    else:
        human_readable = '0 Byte'

    return human_readable


def load_css_from_data(css_data):
    """Load css customizations from a bytestring.
    """
    style_provider = Gtk.CssProvider()
    style_provider.load_from_data(css_data)
    Gtk.StyleContext.add_provider_for_screen(
        Gdk.Screen.get_default(),
        style_provider,
        Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
    )


def scrolled(widget):
    """Return a Gtk.ScrolledWindow with `widget` inside"""
    scw = Gtk.ScrolledWindow()
    scw.add(widget)
    return scw


def get_theme_color(widget, background=True, state=Gtk.StateFlags.SELECTED):
    """Get current theme's color for a certain widget being in `state`"""
    color = None
    sctx = widget.get_style_context()
    if background:
        color = sctx.get_background_color(state)
    else:
        color = sctx.get_color(state)
        return '#{r:0^2x}{g:0^2x}{b:0^2x}'.format(
            r=int(255 * color.red),
            g=int(255 * color.green),
            b=int(255 * color.blue)
        )


class IconButton(Gtk.Button):
    """Button with easy icon support."""
    def __init__(self, icon_name, label=None):
        Gtk.Button.__init__(self)

        box = Gtk.Box()
        box.add(
            Gtk.Image.new_from_gicon(
                Gio.ThemedIcon(name=icon_name),
                Gtk.IconSize.BUTTON
            ),
        )

        self.label = None
        if label is not None:
            self.label = Gtk.Label(label)
            self.label.set_margin_start(5)
            box.add(self.label)

        box.show_all()
        self.add(box)

    def set_markup(self, text):
        """Same function as Gtk.Label.set_markup."""
        if self.label is not None:
            self.label.set_markup(text)


class SuggestedButton(IconButton):
    """Gtk.Button with suggested-action style class pre-added."""
    def __init__(self, text=None):
        IconButton.__init__(self, 'object-select-symbolic', text or 'Apply')
        self.get_style_context().add_class(
            Gtk.STYLE_CLASS_SUGGESTED_ACTION
        )


class DestructiveButton(IconButton):
    """Gtk.Button with destructive style class pre-added."""
    def __init__(self, text=None):
        IconButton.__init__(self, 'user-trash-symbolic', text or 'Cancel')
        self.get_style_context().add_class(
            Gtk.STYLE_CLASS_DESTRUCTIVE_ACTION
        )


def create_searchbar(win):
    """Create a searchbar that takes the keyboard events of `win`"""
    search_bar = Gtk.SearchBar()
    search_entry = Gtk.SearchEntry()

    # Box that shows
    search_box = Gtk.Box(
        orientation=Gtk.Orientation.HORIZONTAL, spacing=6
    )
    search_box.pack_start(search_entry, True, True, 0)

    search_bar.add(search_box)
    search_bar.connect_entry(search_entry)
    search_bar.set_search_mode(False)
    search_bar.set_show_close_button(True)
    search_bar.set_no_show_all(True)
    search_bar.hide()
    search_box.show_all()

    def _hide_search_bar():
        """Called on timeout after pressing ESC"""
        if not search_bar.get_search_mode():
            search_bar.hide()
        return False

    def _key_press_event(_, event, search_bar):
        """Called on keyboard input with search entry in focus"""
        search_bar.handle_event(event)
        if event.keyval == Gdk.KEY_Escape:
            search_bar.set_search_mode(False)
            GLib.timeout_add(250, _hide_search_bar)

    win.connect('key-press-event', _key_press_event, search_bar)
    return search_bar, search_entry


class InfoBar(Gtk.InfoBar):
    """Easier to use version Gtk.InfoBar."""
    def __init__(self):
        Gtk.InfoBar.__init__(self)
        self._label = Gtk.Label()

        self.set_show_close_button(True)
        self.get_content_area().add(self._label)
        self.get_content_area().show_all()
        self.set_no_show_all(True)
        self.connect('response', self.on_response)

    def show(self, message, message_type):
        """Show with a certain message and severity level."""
        self.set_message_type(message_type)
        self._label.set_markup(GLib.markup_escape_text(message, -1))
        Gtk.InfoBar.show(self)

    def on_response(self, _, response_id):
        """Just hide once an action was done."""
        if response_id == Gtk.ResponseType.CLOSE:
            self.hide()


class View(Gtk.Grid):
    """Default View class that has some utility extras.
    """
    __gsignals__ = {
        'view-enter': (GObject.SIGNAL_RUN_FIRST, None, ()),
        'view-leave': (GObject.SIGNAL_RUN_FIRST, None, ())
    }

    def __init__(self, app, sub_title=None):
        Gtk.Grid.__init__(self)
        self.scw = Gtk.ScrolledWindow()
        self.scw.set_hexpand(True)

        self._app = app
        self._sub_title = sub_title or View.sub_title.default
        self._is_visible = False
        self._header_widgets = []

        self.progressbar = Gtk.ProgressBar()
        self.progressbar.set_name('ShredderProgress')
        self.progressbar.set_pulse_step(0.1)

        # This is a workaround for removing a small gap at the bottom
        # of the application. Set the widget to be a backdrop always.
        def on_state_cange(pgb, flags):
            """Hack: Make progressbar to be always appear as backdrop."""
            pgb.set_state_flags(flags | Gtk.StateFlags.BACKDROP, True)

        self.progressbar.connect('state-flags-changed', on_state_cange)
        self.progressbar_revealer = Gtk.Revealer()
        self.progressbar_revealer.add(self.progressbar)
        self.progressbar_revealer.show_all()
        self.progressbar_revealer.set_transition_type(
            Gtk.RevealerTransitionType.SLIDE_UP
        )
        self.progressbar_revealer.set_transition_duration(500)

        self.search_bar, self.search_entry = create_searchbar(self)
        self.infobar = InfoBar()

        self.attach(self.infobar, 0, 0, 1, 1)
        self.attach(self.search_bar, 0, 1, 1, 1)
        self.attach(self.progressbar_revealer, 0, 2, 1, 1)
        self.attach(self.scw, 0, 3, 1, 1)

        self.connect('view-enter', self._on_view_enter)
        self.connect('view-leave', self._on_view_leave)

    def add(self, widget):
        """Add the root widget to the view.
        It will be placed inside a scrolled window.
        """
        self.scw.add(widget)

    def _on_view_enter(self, _):
        """Hidden method that is called once a view change is detected.
        The change will be propagated to the underlying view.
        """
        self._is_visible = True
        if hasattr(self, 'on_view_enter'):
            self.on_view_enter()

        # Restore the sub_title.
        View.sub_title.fset(self, self._sub_title)

    def _on_view_leave(self, _):
        """Hidden method that is called once the view gets out of sight.
        If possible the change will be propagated down.
        """
        self.clear_header_widgets()

        self._is_visible = False
        if hasattr(self, 'on_view_leave'):
            self.on_view_leave()

    def show_progress(self, percent):
        """Set a percentage value to display as progress.

        If percent is None, the progressbar will pulse without a goal.
        """
        self.progressbar_revealer.set_reveal_child(True)

        if percent is not None:
            self.progressbar.set_fraction(percent)

    def hide_progress(self):
        """Hide the progressbar from the user.
        """
        self.progressbar_revealer.set_reveal_child(False)

    def show_infobar(self, message, message_type=Gtk.MessageType.INFO):
        """Show an infobar with a text message in it.

        Note: Latest gtk version color the infobar always blue.
              This is slightly retarted and basically makes
              the message_type parameter useless.
        """
        self.infobar.show(message, message_type)

    def hide_infobar(self):
        """Hide an infobar (if displayed)
        """
        self.infobar.hide()

    def set_search_mode(self, active):
        """Show the search bar.
        """
        # Trigger a fake keypress event on the search bar.
        if active:
            self.search_bar.show()
        else:
            GLib.timeout_add(150, self.search_bar.hide)
        self.search_bar.set_search_mode(active)

    @property
    def app_window(self):
        """The associated Gtk.ApplicationWindow."""
        return self._app.win

    @property
    def app(self):
        """The associated GtkApplication instance."""
        return self._app

    @GObject.Property(type=str, default='')
    def sub_title(self):
        """Title shown below the main title in the main window."""
        return self._sub_title

    @sub_title.setter
    def sub_title(self, new_sub_title):
        """Setter for sub_title. Use to describe current step."""
        self.app_window.headerbar.set_subtitle(new_sub_title)
        self._sub_title = new_sub_title

    @property
    def is_visible(self):
        """Is the view currently visible to the user?"""
        return self._is_visible

    def add_header_widget(self, widget, align=Gtk.Align.END):
        """Add a widget to the header, either left or right of the title.
        """
        self.app_window.add_header_widget(widget, align)
        self._header_widgets.append(widget)

    def remove_header_widget(self, widget):
        """Remove a previously added headerwidget. Noop if it did not exist"""
        self._header_widgets.remove(widget)
        self.app_window.remove_header_widget(widget)

    def clear_header_widgets(self):
        """Clear all widget headers of this view"""
        for widget in self._header_widgets:
            self.app_window.remove_header_widget(widget)

        self._header_widgets = []


class PopupMenu(Gtk.Menu):
    '''Just a simpler version of Gtk.Menu with quicker to type methods.

    Otherwise it is a normal Gtk.Menu and can be used as such.
    '''
    def __init__(self):
        Gtk.Menu.__init__(self)

    def _add_item(self, item):
        """Append and show_all for safety"""
        self.append(item)
        self.show_all()

    def simple_add(self, name, callback=None):
        '''Add a Gtk.MenuItem() with a certain name.

        :param callback: Callback that is called when the item is activated
        '''
        item = Gtk.MenuItem()
        item.set_label(name)

        if callable(callback):
            item.connect('activate', callback)
        self._add_item(item)

    def simple_add_checkbox(self, name, toggled=None):
        '''Add a Gtk.CheckMenuItem to the Menu with the initial *state* and
        the callable *toggled* that is called when the state changes.
        '''
        item = Gtk.CheckMenuItem()
        item.set_label(name)
        if callable(toggled):
            item.connect('toggled', toggled)
        self._add_item(item)

    def simple_add_separator(self):
        '''Add a Gtk.SeparatorMenuItem to the Menu.
        '''
        self._add_item(Gtk.SeparatorMenuItem())

    def simple_popup(self, button_event):
        'A simpler version of GtkMenu.popup(); only requiring GdkEventButton.'
        self.popup(
            None, None, None, None,
            button_event.button, button_event.time
        )


########################
# -- CELL RENDERERS -- #
########################

class CellRendererSize(Gtk.CellRendererText):
    """Render the byte size in a human readable form"""
    size = GObject.Property(type=float, default=0)

    def __init__(self, **kwargs):
        Gtk.CellRendererText.__init__(self, **kwargs)
        self.connect('notify::size', CellRendererSize._transform_size)

    def _transform_size(self, _):
        """Convert bytes to human readable sizes on size property changes"""
        self.set_property(
            'text', size_to_human_readable(self.get_property('size'))
        )


def _rnd(num):
    """Round to minimal decimal places & convert to str"""
    if round(num, 1) % 1:
        return str(round(num, 1))
    else:
        return str(int(num))


def pretty_seconds(second_diff):
    """Convert a second difference to sub-day human readable string"""
    if second_diff < 10:
        return "just now"
    elif second_diff < 60:
        return _rnd(second_diff) + " seconds ago"
    elif second_diff < 120:
        return "a minute ago"
    elif second_diff < 3600:
        return _rnd(second_diff / 60) + " minutes ago"
    elif second_diff < 7200:
        return "an hour ago"
    elif second_diff < 86400:
        return _rnd(second_diff / 3600) + " hours ago"


def pretty_date(time=False):
    """Get a datetime object or an int() Epoch timestamp and return a
    pretty string like 'an hour ago', 'Yesterday', '3 months ago',
    'just now', etc
    """
    diff = datetime.now() - time
    second_diff = diff.seconds
    day_diff = diff.days

    if day_diff <= 0:
        return pretty_seconds(second_diff)
    elif day_diff == 1:
        return "Yesterday"
    elif day_diff < 7:
        return _rnd(day_diff) + " days ago"
    elif day_diff < 31:
        return _rnd(day_diff / 7) + " weeks ago"
    elif day_diff < 365:
        return _rnd(day_diff / 30) + " months ago"

    return _rnd(day_diff / 365) + " years ago"


class CellRendererModifiedTime(Gtk.CellRendererText):
    """Display an mtime (unix timestamp) as readable difference to now"""
    mtime = GObject.Property(type=int, default=0)

    def __init__(self, **kwargs):
        Gtk.CellRendererText.__init__(self, **kwargs)
        self.connect(
            'notify::mtime',
            CellRendererModifiedTime._transform_mtime
        )

    def _transform_mtime(self, _):
        """Convert the modification time to a human readable form on change"""
        mtime = self.get_property('mtime')
        if mtime <= 0:
            pretty_date_str = ''
        else:
            pretty_date_str = pretty_date(time=datetime.fromtimestamp(mtime))

        self.set_property('text', pretty_date_str)


class CellRendererCount(Gtk.CellRendererText):
    """Render a count of objects (1 Object, 2 Objects...)"""
    count = GObject.Property(type=int, default=-1)

    def __init__(self, **kwargs):
        Gtk.CellRendererText.__init__(self, **kwargs)
        self.connect(
            'notify::count',
            CellRendererCount._transform_count
        )

    def _transform_count(self, _):
        """Convert the count property to a meaningful message.

        Negative numbers are regarded as twins,
        positive numbers as objects in a directory.
        """
        count = self.get_property('count')
        is_plural = abs(count) is not 1

        if count > 0:
            name = 'Objects' if is_plural else 'Object'
            text = '{} {}'.format(count, name)
        elif count < 0:
            name = 'Twins' if is_plural else 'Twin'
            text = '{} {}'.format(-count, name)
        else:
            text = ''

        self.set_property('text', text)


class NodeState:
    NONE = 0
    ORIGINAL = 1
    DUPLICATE = 2

    @staticmethod
    def should_keep(state):
        """Check if we should keep a file with this state"""
        return state is NodeState.ORIGINAL or state is NodeState.NONE


STATE_TO_SYMBOL = {
    NodeState.NONE: '',
    NodeState.ORIGINAL: '<span color="green">✔</span>',
    NodeState.DUPLICATE: '<span color="red">✗</span>',
}


class CellRendererLint(Gtk.CellRendererPixbuf):
    """Render the lint tag (checkmark, error, warning icon) in a cell.

    This cellrenderer caches previously rendered buffers.
    """
    ICON_SIZE = 10

    tag = GObject.Property(type=int, default=NodeState.DUPLICATE)

    def __init__(self, **kwargs):
        Gtk.CellRendererPixbuf.__init__(self, **kwargs)
        self.set_alignment(0.0, 0.6)

    def do_render(self, ctx, widget, bg, cell, *_):
        """Render a unicode symbol using Pango."""
        tag = self.get_property('tag')
        if tag is NodeState.NONE:
            return

        text = STATE_TO_SYMBOL.get(tag)
        if not text:
            return

        layout = PangoCairo.create_layout(ctx)
        font = Pango.FontDescription.new()
        font.set_size(6 * Pango.SCALE)
        layout.set_markup(text, -1)
        layout.set_alignment(Pango.Alignment.CENTER)

        xpad = self.get_property('xpad')
        ypad = self.get_property('ypad') - 10

        fw, fh = [num / Pango.SCALE / 2 for num in layout.get_size()]
        ctx.move_to(cell.x - fw + xpad, cell.y + fh + ypad)
        PangoCairo.show_layout(ctx, layout)

    def do_get_size(self, _, cell_area):
        xpad = self.get_property('xpad')
        width = height = xpad * 2 + CellRendererLint.ICON_SIZE

        x_loc, y_loc = 0, 0
        if cell_area:
            xalign = self.get_property('xalign')
            yalign = self.get_property('yalign')
            x_loc = max(0, xalign * (cell_area.width - width))
            y_loc = max(0, yalign * (cell_area.height - height))

        return x_loc, y_loc, width, height


###############################
# Multiple Choice List Widget #
###############################


class ChoiceRow(Gtk.ListBoxRow):
    """Row representing a single choice"""
    def __init__(self, value, is_default, capitalize=False):
        Gtk.ListBoxRow.__init__(self)

        self.value, self.is_default = value, is_default

        self.set_can_focus(False)
        self.set_margin_start(3)
        self.set_margin_end(3)

        self.symbol = Gtk.Image.new_from_gicon(
            Gio.ThemedIcon(name='emblem-ok-symbolic'),
            Gtk.IconSize.BUTTON
        )
        self.symbol.props.margin_start = 10
        self.symbol.set_no_show_all(True)

        if capitalize:
            display_value = value.capitalize()
        else:
            display_value = value

        label = Gtk.Label(display_value)
        label.props.xalign = 0

        box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        box.pack_start(label, True, True, 0)
        box.pack_start(self.symbol, False, False, 0)
        self.add(box)

    def set_show_checkmark(self, state):
        """Choose an icon based on the current `state`"""
        self.symbol.set_visible(state or self.is_default)

        checkmark_table = {
            (False, False): (None, False),
            (False, True): ('emblem-ok-symbolic', False),
            (True, False): ('non-starred-symbolic', True),
            (True, True): ('starred-symbolic', False)
        }

        icon_name, dim_down = checkmark_table[(self.is_default, bool(state))]
        if icon_name is not None:
            self.symbol.set_from_gicon(
                Gio.ThemedIcon(name=icon_name), Gtk.IconSize.MENU
            )

        ctx = self.symbol.get_style_context()
        if dim_down:
            ctx.add_class(Gtk.STYLE_CLASS_DIM_LABEL)
            self.symbol.set_opacity(0.5)
        else:
            ctx.remove_class(Gtk.STYLE_CLASS_DIM_LABEL)
            self.symbol.set_opacity(0.7)


class CurrentChoiceLabel(Gtk.Label):
    """Helper class for displaying the current choice as label"""
    def __init__(self, text):
        Gtk.Label.__init__(self)
        self.set_use_markup(True)
        self.set_choice(text)

    @GObject.Property(type=str, default='')
    def choice(self):
        """Currently active choice"""
        return self._choice

    def set_choice(self, new_value):
        """Set choice to a markup'd form of `new_value`"""
        self._choice = new_value

        display_value = new_value

        self.set_markup(
            '<u>{v}</u>'.format(
                v=display_value
            )
        )

        self.notify('choice')


class MultipleChoiceButton(Gtk.Button):
    """Button that offers a selection of different choices.

    - Only one choice can be active at the same time.
    - The button will look like an underlined label.
    - Default values are marked with a separate icon.
    - The popup is a GtkPopover.
    """
    __gsignals__ = {
        'row-selected': (GObject.SIGNAL_RUN_FIRST, None, ()),
    }

    def __init__(self, values, default, selected):
        Gtk.Button.__init__(self)
        self._selected_choice = selected
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
        """Set current row & update checkmarks accordingly"""
        for other_row in self.listbox:
            # Might be a different type:
            if isinstance(other_row, ChoiceRow):
                other_row.set_show_checkmark(row is other_row)

        self.value_label.set_choice(row.value)
        self._selected_choice = row.value

    def get_selected_choice(self):
        """Return the currently selected label text"""
        return self._selected_choice

    def set_selected_choice(self, value):
        """Set the choice of the widget by name"""
        for row in self.listbox:
            if isinstance(row, ChoiceRow):
                if row.value == value:
                    self._set_current_row(row)

    def on_update_value(self, _, row, popover):
        """Called on a click on a row. Will hide the popover."""
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


MAX_EXPONENT = max(EXPONENTS.values())
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

        self._last_val, self._curr_exp = 1, 1
        self._units = MultipleChoiceButton(
            [label for label, _ in SORTED_KEYS], 'MB', 'MB'
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
        return int(self._last_val * (1024 ** self._curr_exp))

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
        """Remember the exponent and select the fitting unit."""
        # linear search for the correct label
        key, value = None, None
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
        self.on_value_changed(self._entry)


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
        """Minimum value the widget (i.e. left side)"""
        return self._min_wdgt.get_bytes()

    @min_value.setter
    def min_value(self, val):
        """Set the minum value."""
        self._min_wdgt.set_bytes(val)

    @property
    def max_value(self):
        """Maximum value of the widget (i.e. right side)"""
        return self._max_wdgt.get_bytes()

    @max_value.setter
    def max_value(self, val):
        """Set the maximum value."""
        self._max_wdgt.set_bytes(val)

    def on_value_changed(self, wdgt, _):
        """Called when any of the both sides change."""
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
    def main():
        """Test main."""
        win = Gtk.Window()
        win.connect('destroy', Gtk.main_quit)
        win.add(FileSizeRange(1, 1024 ** MAX_EXPONENT))
        win.show_all()
        Gtk.main()

    main()
