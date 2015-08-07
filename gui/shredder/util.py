#!/usr/bin/env python
# encoding: utf-8

# External:
from gi.repository import Gtk
from gi.repository import Gdk
from gi.repository import Gio
from gi.repository import GObject
from gi.repository import GLib


def size_to_human_readable(size):
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

    return human_readable


def render_pixbuf(widget, width=-1, height=-1):
    """Renders any widget that is not realized yet
    into a pixbuf with the supplied width and height.

    Note: this function may trigger mainloop events,
          since it needs to spin the loop to trigger
          the rendering.
    """
    # Use an OffscreenWindow to render the widget
    off_win = Gtk.OffscreenWindow()
    off_win.add(widget)
    off_win.set_size_request(width, height)
    off_win.show_all()

    # this is needed, otherwise the screenshot is black
    while Gtk.events_pending():
        Gtk.main_iteration()

    # Render the widget to a GdkPixbuf
    widget_pix = off_win.get_pixbuf()
    off_win.remove(widget)
    return widget_pix


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
    scw = Gtk.ScrolledWindow()
    scw.add(widget)
    return scw


def get_theme_color(widget, background=True, state=Gtk.StateFlags.SELECTED):
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
        if self.label is not None:
            self.label.set_markup(text)


class SuggestedButton(IconButton):
    def __init__(self, text=None):
        IconButton.__init__(self, 'object-select-symbolic', text or _('Apply'))
        self.get_style_context().add_class(
            Gtk.STYLE_CLASS_SUGGESTED_ACTION
        )


class DestructiveButton(IconButton):
    def __init__(self, text=None):
        IconButton.__init__(self, 'user-trash-symbolic', text or _('Cancel'))
        self.get_style_context().add_class(
            Gtk.STYLE_CLASS_DESTRUCTIVE_ACTION
        )


class IndicatorLabel(Gtk.Label):
    """A label that has a rounded, colored background.

    It is mainly useful for showing new entries or indicate errors.
    There are 3 colors available, plus a color derived from the
    theme's main color. In case of Adwaita blue.
    """
    NONE, SUCCESS, WARNING, ERROR, THEME = range(5)

    def __init__(self, *args):
        Gtk.Label.__init__(self, *args)
        self.set_use_markup(True)

        # Do not expand space.
        self.set_valign(Gtk.Align.CENTER)
        self.set_halign(Gtk.Align.CENTER)
        self.set_vexpand(False)
        self.set_hexpand(False)

        # Use the theme's color by default.
        self.set_state(IndicatorLabel.THEME)

    def set_state(self, state):
        classes = {
            IndicatorLabel.ERROR: 'ShredderIndicatorLabelError',
            IndicatorLabel.SUCCESS: 'ShredderIndicatorLabelSuccess',
            IndicatorLabel.WARNING: 'ShredderIndicatorLabelWarning',
            IndicatorLabel.THEME: 'ShredderIndicatorLabelTheme',
            IndicatorLabel.NONE: 'ShredderIndicatorLabelEmpty'
        }

        # Will act as normal label for invalid states.
        # Useful for highlighting problematic input.
        self.set_name(classes.get(state, 'ShredderIndicatorLabelEmpty'))


def create_searchbar(win):
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
        if not search_bar.get_search_mode():
            search_bar.hide()
        return False

    def _key_press_event(win, event, bar):
        bar.handle_event(event)
        if event.keyval == Gdk.KEY_Escape:
            bar.set_search_mode(False)
            GLib.timeout_add(250, _hide_search_bar)

    win.connect('key-press-event', _key_press_event, search_bar)
    return search_bar, search_entry


class InfoBar(Gtk.InfoBar):
    def __init__(self):
        Gtk.InfoBar.__init__(self)
        self._label = Gtk.Label()

        self.set_show_close_button(True)
        self.get_content_area().add(self._label)
        self.get_content_area().show_all()
        self.set_no_show_all(True)
        self.connect('response', self._on_response)

    def show(self, message, message_type):
        self.set_message_type(message_type)
        self._label.set_markup(GLib.markup_escape_text(message, -1))
        Gtk.InfoBar.show(self)

    def _on_response(self, infobar, response_id):
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

        self.progressbar = Gtk.ProgressBar()
        self.progressbar.set_name('ShredderProgress')
        self.progressbar.set_pulse_step(0.1)

        # This is a workaround for removing a small gap at the bottom
        # of the application. Set the widget to be a backdrop always.
        def _on_state_cange(pgb, flags):
            pgb.set_state_flags(flags | Gtk.StateFlags.BACKDROP, True)

        self.progressbar.connect('state-flags-changed', _on_state_cange)
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
        self.scw.add(widget)

    def _on_view_enter(self, _):
        self._is_visible = True
        if hasattr(self, 'on_view_enter'):
            self.on_view_enter()

        # Restore the sub_title.
        self.sub_title = self._sub_title

    def _on_view_leave(self, _):
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
        else:
            self.progressbar.pulse()

    def hide_progress(self):
        """Hide the progressbar from the user.
        """
        self.progressbar_revealer.set_reveal_child(False)

    def show_infobar(self, message, message_type=Gtk.MessageType.INFO):
        """Show an inforbar with a text message in it.

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
        self.search_bar.set_search_mode(active)
        if active:
            self.search_bar.show()
        else:
            self.search_bar.hide()

    @property
    def app_window(self):
        return self._app.win

    @property
    def app(self):
        return self._app

    @GObject.Property(type=str, default='')
    def sub_title(self):
        return self._sub_title

    @sub_title.setter
    def sub_title(self, new_sub_title):
        self.app_window.headerbar.set_subtitle(new_sub_title)
        self._sub_title = new_sub_title

    @property
    def is_visible(self):
        return self._is_visible


class PopupMenu(Gtk.Menu):
    '''Just a simpler version of Gtk.Menu with quicker to type methods.

    Otherwise it is a normal Gtk.Menu and can be used as such.
    '''
    def __init__(self):
        Gtk.Menu.__init__(self)

    def _add_item(self, item):
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

    def simple_add_checkbox(self, name, state=False, toggled=None):
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
        'A simpler version of Gtk.Menu.popup(), only requiring a GdkEventButton.'
        self.popup(None, None, None, None, button_event.button, button_event.time)
