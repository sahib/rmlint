#!/usr/bin/env python
# encoding: utf-8

# Stdlib:
from functools import partial

# Internal:
import app
from app.util import IconButton

# External:
from gi.repository import Gdk, Gtk, GLib, Gio, GObject


class ViewSwitcher(Gtk.Box):
    def __init__(self, stack):
        Gtk.Box.__init__(self, orientation=Gtk.Orientation.HORIZONTAL)
        self._stack = stack
        self._prev = None

        # Make the buttons appear connected:
        self.get_style_context().add_class("linked")

        self.button_left, self.button_right = Gtk.Button(), Gtk.Button()
        for btn, arrow, direction in (
            (self.button_left, Gtk.ArrowType.LEFT, -1),
            (self.button_right, Gtk.ArrowType.RIGHT, +1)
        ):
            btn.add(Gtk.Arrow(arrow, Gtk.ShadowType.NONE))
            btn.connect('clicked', partial(self._set_widget_at, step=direction))
            btn.set_sensitive(False)
            self.add(btn)

        self.show_all()

    def _find_curr_index(self):
        visible = self._stack.get_visible_child()
        widgets = list(self._stack)

        try:
            return widgets.index(visible)
        except ValueError:
            return 0

    def _get_widget_at(self, idx):
        idx = max(0, min(len(self._stack) - 1, idx))
        return list(self._stack)[idx]

    def _set_widget_at(self, _, step):
        current_idx = self._find_curr_index()
        next_widget = self._get_widget_at(current_idx + step)
        self._set_visible_child(next_widget)
        self._update_sensitivness()

    def _set_visible_child(self, child, update_prev=True):
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
        idx = self._find_curr_index()
        if idx == 0:
            self.button_left.set_sensitive(False)
        else:
            self.button_left.set_sensitive(True)

        if idx == len(self._stack) - 1:
            self.button_right.set_sensitive(False)
        else:
            self.button_right.set_sensitive(True)

    def add_view(self, widget, name):
        widget.set_hexpand(True)
        widget.set_vexpand(True)
        widget.set_halign(Gtk.Align.FILL)
        widget.set_valign(Gtk.Align.FILL)
        widget.show_all()
        self._stack.add_named(widget, name)

    def switch(self, name):
        widget = self._stack.get_child_by_name(name)
        self._set_visible_child(widget)
        self._update_sensitivness()

    def switch_to_previous(self):
        if self._prev is None:
            return

        self._set_visible_child(self._prev, update_prev=False)
        self._update_sensitivness()


class HeaderBar(Gtk.HeaderBar):
    def __init__(self):
        Gtk.HeaderBar.__init__(self)

        self.set_title(app.APP_TITLE)
        self.set_subtitle(app.APP_DESCRIPTION)


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


def create_item(name, action, icon, variant=None):
    if variant is not None:
        name = '{n} ({v})'.format(n=name, v=str(variant))

    item = Gio.MenuItem.new(name, action)
    item.set_icon(Gio.ThemedIcon.new(icon))

    if variant:
        item.set_action_and_target_value(action, variant)

    return item


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


class MainWindow(Gtk.ApplicationWindow):
    __gsignals__ = {
        'suggested-action-clicked': (GObject.SIGNAL_RUN_FIRST, None, ()),
        'destructive-action-clicked': (GObject.SIGNAL_RUN_FIRST, None, ())
    }

    def __init__(self, application):
        Gtk.Window.__init__(
            self, title='Welcome to the Demo', application=application
        )

        # Set the css name:
        self.set_name('AppWindow')
        self.set_title(app.APP_TITLE)
        self.set_default_size(1260, 660)

        self.infobar = InfoBar()

        self.view_stack = Gtk.Stack()
        self.view_stack.set_transition_type(
            Gtk.StackTransitionType.SLIDE_LEFT_RIGHT
        )
        self.views = ViewSwitcher(self.view_stack)

        self.main_grid = Gtk.Grid()
        self.headerbar = HeaderBar()
        self.headerbar.pack_start(self.views)
        self.set_titlebar(self.headerbar)

        self.progressbar = Gtk.ProgressBar()
        self.progressbar.set_name('AppProgress')
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
        self.progressbar_revealer.set_transition_duration(
            500
        )

        def _on_action_clicked(button, signal):
            self.emit(signal)

        self.suggested_action = IconButton(
            'object-select-symbolic', _('Apply')
        )
        self.suggested_action.connect(
            'clicked', _on_action_clicked, 'suggested-action-clicked'
        )

        self.destructive_action = IconButton(
            'user-trash-symbolic', _('Cancel')
        )
        self.destructive_action.connect(
            'clicked', _on_action_clicked, 'destructive-action-clicked'
        )

        for btn, bad in ((self.suggested_action, False), (self.destructive_action, True)):
            btn.set_no_show_all(True)
            btn.set_hexpand(True)
            btn.set_vexpand(True)

            btn.get_style_context().add_class(
                Gtk.STYLE_CLASS_DESTRUCTIVE_ACTION if bad else Gtk.STYLE_CLASS_SUGGESTED_ACTION
            )

        main_menu = Gio.Menu()
        main_menu.append_item(
            create_item(_('About'), 'app.about', 'help-about')
        )
        main_menu.append_item(
            create_item(_('Search'), 'app.search', 'find-location-symbolic')
        )
        main_menu.append_item(
            create_item(_('Work'), 'app.work', 'folder-symbolic')
        )
        main_menu.append_item(
            create_item(_('Quit'), 'app.quit', 'window-close')
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
            'clicked', lambda btn: application.activate_action('app.search')
        )

        if app.APP_USE_TRADITIONAL_MENU:
            menu_button.set_use_popover(False)

        self.search_bar, self.search_entry = create_searchbar(self)
        self.main_grid.attach(self.infobar, 0, 0, 1, 1)
        self.main_grid.attach(self.search_bar, 0, 1, 1, 1)
        self.main_grid.attach(self.view_stack, 0, 2, 1, 1)
        self.main_grid.attach(self.progressbar_revealer, 0, 3, 1, 1)
        self.headerbar.pack_end(menu_button)
        self.headerbar.pack_end(search_button)
        self.headerbar.pack_end(self.suggested_action)
        self.headerbar.pack_start(self.destructive_action)
        self.add(self.main_grid)

    def show_action_buttons(self, apply_label, cancel_label):
        """Show the suggested/destructive action buttons.

        If either apply_label or cancel_label is None,
        the respective button will not be shown.
        """
        if apply_label:
            self.suggested_action.set_markup(apply_label)
            self.suggested_action.show()

        if cancel_label:
            self.destructive_action.set_markup(cancel_label)
            self.destructive_action.show()

    def hide_action_buttons(self):
        self.suggested_action.hide()
        self.destructive_action.hide()

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

    def set_search_mode(self, active):
        """Show the
        """
        self.search_bar.set_search_mode(active)
        if active:
            self.search_bar.show()
        else:
            self.search_bar.hide()
