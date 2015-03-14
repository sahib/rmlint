#!/usr/bin/env python
# encoding: utf-8

# External:
from gi.repository import Gtk, Gdk, Gio, GObject


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
            self.label.set_margin_left(5)
            box.add(self.label)

        box.show_all()
        self.add(box)

    def set_markup(self, text):
        if self.label is not None:
            self.label.set_markup(text)


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
            IndicatorLabel.ERROR: 'AppIndicatorLabelError',
            IndicatorLabel.SUCCESS: 'AppIndicatorLabelSuccess',
            IndicatorLabel.WARNING: 'AppIndicatorLabelWarning',
            IndicatorLabel.THEME: 'AppIndicatorLabelTheme',
            IndicatorLabel.NONE: 'AppIndicatorLabelEmpty'
        }

        # Will act as normal label for invalid states.
        # Useful for highlighting problematic input.
        self.set_name(classes.get(state, 'AppIndicatorLabelEmpty'))


class View(Gtk.ScrolledWindow):
    """Default View class that has some utility extras.
    """

    __gsignals__ = {
        'view-enter': (GObject.SIGNAL_RUN_FIRST, None, ()),
        'view-leave': (GObject.SIGNAL_RUN_FIRST, None, ())
    }

    def __init__(self, app, sub_title=None):
        Gtk.ScrolledWindow.__init__(self)
        self._app = app
        self._sub_title = sub_title or View.sub_title.default
        self._is_visible = False

        self.connect('view-enter', self._on_view_enter)
        self.connect('view-leave', self._on_view_leave)

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
