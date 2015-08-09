#!/usr/bin/env python
# encoding: utf-8

"""
Shredder's implementation og GtkApplicationWindow.
Notable classes:

    - ViewSwitcher: Logic for switching main content.
    - HeaderBar: Logic for the upper header.
    - MainWindow: Window with HeaderBar and View stack.
"""

# Stdlib:
from functools import partial
from gettext import gettext

# External:
from gi.repository import Gtk
from gi.repository import Gio

# Internal:
import shredder


_ = gettext


class ViewSwitcher(Gtk.Box):
    """Implements the logic for switching through views.
    Looks like a linked two button box on the outside.
    """
    def __init__(self, stack):
        Gtk.Box.__init__(self, orientation=Gtk.Orientation.HORIZONTAL)
        self._stack = stack
        self._prev = None

        # Make the buttons appear connected:
        self.get_style_context().add_class('linked')

        self.go_left, self.go_right = Gtk.Button(), Gtk.Button()
        for btn, arrow, direction in (
                (self.go_left, Gtk.ArrowType.LEFT, -1),
                (self.go_right, Gtk.ArrowType.RIGHT, +1)
        ):
            btn.add(Gtk.Arrow(arrow, Gtk.ShadowType.NONE))
            btn.connect('clicked', partial(
                self._set_widget_at, step=direction
            ))
            btn.set_sensitive(False)
            self.add(btn)

        self.show_all()

    def __getitem__(self, name):
        return self._stack.get_child_by_name(name)

    def _find_curr_index(self):
        """Find the numeric index where the current view lies"""
        visible = self._stack.get_visible_child()
        widgets = list(self._stack)

        try:
            return widgets.index(visible)
        except ValueError:
            return 0

    def _get_widget_at(self, idx):
        """Return the widgets at a certain numeric index"""
        idx = max(0, min(len(self._stack) - 1, idx))
        return list(self._stack)[idx]

    def _set_widget_at(self, _=None, step=+1):
        """Step right (or left if step is negative) from current view"""
        current_idx = self._find_curr_index()
        next_widget = self._get_widget_at(current_idx + step)
        self._set_visible_child(next_widget)
        self._update_sensitivness()

    def _set_visible_child(self, child, update_prev=True):
        """Set and notify about the changed view"""
        prev = self._stack.get_visible_child()
        self._stack.set_visible_child(child)

        try:
            child.emit('view-enter')
        except TypeError:
            pass

        try:
            prev.emit('view-leave')
        except TypeError:
            pass

        if update_prev:
            self._prev = prev

    def _update_sensitivness(self):
        """Check if we need to grey out left/right buttons"""
        idx = self._find_curr_index()
        self.go_left.set_sensitive(idx != 0)
        self.go_right.set_sensitive(idx != len(self._stack) - 1)

    def add_view(self, view, name):
        """Add a new `view` widget to the view switcher.
        It will be selectable by `name`.
        """
        view.set_hexpand(True)
        view.set_vexpand(True)
        view.set_halign(Gtk.Align.FILL)
        view.set_valign(Gtk.Align.FILL)
        view.show_all()

        self._stack.add_named(view, name)

    def switch(self, name):
        """Switch to a certain view by name."""
        widget = self._stack.get_child_by_name(name)
        self._set_visible_child(widget)
        self._update_sensitivness()

    def switch_to_previous(self):
        """Switch to last visible view."""
        if self._prev is None:
            return

        self._set_visible_child(self._prev, update_prev=False)
        self._update_sensitivness()

    def set_search_mode(self, mode):
        """Activate or deactivate search bar for current view."""
        view = self._stack.get_visible_child()
        view.set_search_mode(mode)


class HeaderBar(Gtk.HeaderBar):
    """Container for the headerbar logic."""
    def __init__(self):
        Gtk.HeaderBar.__init__(self)

        self.set_title(shredder.APP_TITLE)
        self.set_subtitle(shredder.APP_DESCRIPTION)

        # This is a hack to get a small annoying bug under control:
        # If adding buttons to the headerbar, it's size will increase
        # This sometimes lead to small drawing errors and jumpy interface.
        # This fix is not very clever but works at least.
        self.set_size_request(-1, 46)


def _create_item(name, action, icon, variant=None):
    """Create a GMenuItem from a action, optionally with an icon"""
    if variant is not None:
        name = '{n} ({v})'.format(n=name, v=str(variant))

    item = Gio.MenuItem.new(name, action)
    item.set_icon(Gio.ThemedIcon.new(icon))

    if variant:
        item.set_action_and_target_value(action, variant)

    return item


class MainWindow(Gtk.ApplicationWindow):
    """Shredder's top level GtkApplicationWindow"""
    def __init__(self, application):
        Gtk.ApplicationWindow.__init__(
            self, title='Shredder', application=application
        )

        # Set the css name:
        self.set_name('ShredderWindow')
        self.set_title(shredder.APP_TITLE)
        self.set_default_size(1280, 660)

        self.view_stack = Gtk.Stack()
        self.view_stack.set_transition_type(
            Gtk.StackTransitionType.SLIDE_LEFT_RIGHT
        )
        self.views = ViewSwitcher(self.view_stack)

        self.headerbar = HeaderBar()
        self.headerbar.pack_start(self.views)
        self.set_titlebar(self.headerbar)

        main_menu = Gio.Menu()
        main_menu.append_item(
            _create_item(_('About'), 'app.about', 'help-about')
        )
        main_menu.append_item(
            _create_item(_('Search'), 'app.search', 'find-location-symbolic')
        )
        main_menu.append_item(
            _create_item(_('Quit'), 'app.quit', 'window-close')
        )

        # Add the menu button up right:
        menu_button = Gtk.MenuButton()
        menu_button.add(
            Gtk.Image.new_from_gicon(
                Gio.ThemedIcon(name="emblem-system-symbolic"),
                Gtk.IconSize.BUTTON
            )
        )

        menu_button.set_menu_model(main_menu)

        # Add the symbolic search button:
        search_button = Gtk.ToggleButton()
        search_button.add(
            Gtk.Image.new_from_gicon(
                Gio.ThemedIcon(name="edit-find-symbolic"),
                Gtk.IconSize.BUTTON
            )
        )

        application = Gio.Application.get_default()
        search_button.connect(
            'clicked', lambda btn: self.views.set_search_mode(btn.get_active())
        )

        if shredder.APP_USE_TRADITIONAL_MENU:
            menu_button.set_use_popover(False)

        self.main_grid = Gtk.Grid()
        self.main_grid.attach(self.view_stack, 0, 3, 1, 1)
        self.headerbar.pack_end(menu_button)
        self.headerbar.pack_end(search_button)
        self.add(self.main_grid)

    def add_header_widget(self, widget, align=Gtk.Align.END):
        """Add a widget to the header, either left or right of the title.
        """
        if align is Gtk.Align.END:
            self.headerbar.pack_end(widget)
        elif align is Gtk.Align.START:
            self.headerbar.pack_start(widget)
        else:
            raise ValueError('{align} not supported here.'.format(align=align))

        widget.show_all()

    def remove_header_widget(self, widget):
        """Remove a previously added headerwidget. Noop if it did not exist"""
        if widget in self.headerbar:
            self.headerbar.remove(widget)
