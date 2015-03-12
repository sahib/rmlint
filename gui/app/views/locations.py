#!/usr/bin/env python
# encoding: utf-8

# Stdlib:
import os

# Internal:
from app.util import View, IconButton

# External:
from gi.repository import Gtk, GLib, Gio


class ShredderLocationEntry(Gtk.ListBoxRow):
    def __init__(self, name, path, themed_icon):
        Gtk.ListBoxRow.__init__(self)

        self.set_name('ShredderLocationEntry')

        grid = Gtk.Grid()
        self.add(grid)

        self.set_size_request(-1, 80)

        self.path = path
        self.name = name
        self.is_preferred = False

        self.separator = Gtk.Separator()
        self.separator.set_hexpand(True)
        self.separator.set_halign(Gtk.Align.FILL)

        name_label = Gtk.Label(
            '<b>{}</b>'.format(GLib.markup_escape_text(name))
        )
        name_label.set_use_markup(True)
        name_label.set_hexpand(True)
        name_label.set_halign(Gtk.Align.START)

        path_label = Gtk.Label(
            '<small>{}</small>'.format(GLib.markup_escape_text(path))
        )
        path_label.set_use_markup(True)
        path_label.set_hexpand(True)
        path_label.set_halign(Gtk.Align.START)

        icon_img = Gtk.Image.new_from_gicon(
            themed_icon,
            Gtk.IconSize.DIALOG
        )
        icon_img.set_halign(Gtk.Align.START)
        icon_img.set_margin_start(3)
        icon_img.set_margin_end(10)
        icon_img.set_vexpand(True)
        icon_img.set_valign(Gtk.Align.FILL)

        self.check_box = Gtk.CheckButton()
        self.check_box.connect('toggled', self._on_check_box_toggled)
        self.check_box.set_tooltip_text('Prefer this directory?')
        self.check_box.set_margin_end(5)
        self.check_box.set_margin_top(13)
        self.check_box.set_can_focus(False)

        level_bar = Gtk.LevelBar()
        level_bar.set_valign(Gtk.Align.START)
        level_bar.set_halign(Gtk.Align.END)
        level_bar.set_vexpand(False)
        level_bar.set_value(0.5)
        level_bar.set_size_request(150, 10)
        level_bar.set_margin_end(20)
        level_bar.set_margin_top(20)

        level_label = Gtk.Label()
        level_label.set_markup('<small>10 / 200 - 5%</small>')
        level_label.set_valign(Gtk.Align.START)
        level_label.set_halign(Gtk.Align.END)
        level_label.set_margin_end(20)
        level_label.set_vexpand(False)

        grid.attach(icon_img, 0, 0, 5, 5)
        grid.attach(name_label, 5, 2, 1, 1)
        grid.attach(path_label, 5, 3, 1, 1)
        grid.attach(level_bar, 6, 2, 1, 1)
        grid.attach(level_label, 6, 3, 1, 1)
        grid.attach(self.check_box, 7, 2, 1, 1)
        grid.attach(self.separator, 0, 8, 8, 1)

    def _on_check_box_toggled(self, btn):
        ctx = self.get_style_context()
        if btn.get_active():
            ctx.add_class('original')
        else:
            ctx.remove_class('original')

        self.is_preferred = btn.get_active()


class LocationView(View):
    def __init__(self, app):
        View.__init__(self, app, sub_title='Choose locations to check')
        self.selected_locations = []
        self.known_paths = set()

        self.box = Gtk.ListBox()
        self.box.set_selection_mode(Gtk.SelectionMode.NONE)
        self.box.set_hexpand(True)
        self.box.set_placeholder(Gtk.Label('No locations mounted.'))
        self.box.set_valign(Gtk.Align.FILL)
        self.box.set_vexpand(True)

        self.chooser_button = IconButton(
            'list-add-symbolic', 'Add Location'
        )
        self.chooser_button.connect(
            'clicked', self._on_chooser_button_clicked
        )

        self.file_chooser = Gtk.FileChooserWidget()
        self.file_chooser.set_select_multiple(True)
        self.file_chooser.set_action(Gtk.FileChooserAction.SELECT_FOLDER)
        self.file_chooser.set_create_folders(False)

        self.stack = Gtk.Stack()
        self.stack.set_transition_type(Gtk.StackTransitionType.SLIDE_UP)

        scw = Gtk.ScrolledWindow()
        scw.add(self.box)

        self.stack.add_named(scw, 'list')
        self.stack.add_named(self.file_chooser, 'chooser')

        self.box.set_activate_on_single_click(True)
        self.box.set_filter_func(self._filter_func)
        self.box.connect('row-activated', self._on_row_clicked)

        self.app_window.search_entry.connect(
            'search-changed', self._on_search_changed
        )

        self.volume_monitor = Gio.VolumeMonitor.get()
        self.recent_mgr = Gtk.RecentManager.get_default()
        self.recent_mgr.connect('changed', self.refill_entries)
        self.volume_monitor.connect('mount-changed', self.refill_entries)
        self.refill_entries()

        run_button = IconButton('emblem-system', 'Scan folders')
        run_button.get_style_context().add_class(
            Gtk.STYLE_CLASS_SUGGESTED_ACTION
        )

        self.selected_label = Gtk.Label()
        self.selected_label.get_style_context().add_class(
            Gtk.STYLE_CLASS_DIM_LABEL
        )

        action_bar = Gtk.ActionBar()
        action_bar.pack_start(self.selected_label)
        action_bar.pack_end(run_button)

        self.revealer = Gtk.Revealer()
        self.revealer.add(action_bar)
        self.revealer.set_hexpand(True)
        self.revealer.set_halign(Gtk.Align.FILL)

        grid = Gtk.Grid()
        grid.attach(self.stack, 0, 0, 1, 1)
        grid.attach(self.revealer, 0, 1, 1, 1)

        self.add(grid)

    def refill_entries(self, *_):
        for child in list(self.box):
            self.box.remove(child)

        self.add_entry(
            'Personal directory',
            os.path.expanduser('~'),
            Gio.ThemedIcon(name='user-home')
        )
        self.add_entry(
            'Cache and logs',
            '/var',
            Gio.ThemedIcon(name='folder-templates')
        )

        for mount in self.volume_monitor.get_mounts():
            self.add_entry(
                mount.get_name(),
                mount.get_root().get_path(),
                mount.get_icon()
            )

        for item in self.recent_mgr.get_items():
            if item.get_mime_type() == 'inode/directory' and item.exists():
                path = item.get_uri()
                if path.startswith('file://'):
                    path = path[7:]

                self.add_entry(
                    os.path.basename(path),
                    path,
                    item.get_gicon()
                )

        self.show_all()

    def add_entry(self, name, path, icon):
        path = path.strip()
        if path == '/':
            return

        if path in self.known_paths:
            return

        self.known_paths.add(path)
        self.box.insert(ShredderLocationEntry(name, path, icon), -1)

    def _on_row_clicked(self, box, row):
        style_ctx = row.get_style_context()
        if style_ctx.has_class('selected'):
            style_ctx.remove_class('selected')
            self.selected_locations.remove(row)
        else:
            style_ctx.add_class('selected')
            self.selected_locations.append(row)

        self.revealer.set_reveal_child(bool(self.selected_locations))

        prefd_paths = sum(row.is_preferred for row in self.selected_locations)
        self.selected_label.set_markup(
            '{sel} directories - {pref} of them preferred'.format(
                sel=len(self.selected_locations),
                pref=prefd_paths
            )
        )

    def _on_search_changed(self, entry):
        if self.is_visible:
            self.box.invalidate_filter()

    def _filter_func(self, row):
        query = self.app_window.search_entry.get_text().lower()
        if query in row.path.lower():
            return True

        return query in row.name.lower()

    def on_view_enter(self):
        self.app_window.add_header_widget(self.chooser_button)

    def on_view_leave(self):
        self.app_window.remove_header_widget(self.chooser_button)

    def _on_chooser_button_clicked(self, btn):
        self.stack.set_visible_child_name('chooser')
        self.app_window.remove_header_widget(self.chooser_button)
        self.app_window.views.go_right.set_sensitive(False)
        self.app_window.views.go_left.set_sensitive(False)
        self.sub_title = 'Choose a new location'

        open_button = IconButton('emblem-ok-symbolic', 'Add selected')
        open_button.get_style_context().add_class(
            Gtk.STYLE_CLASS_SUGGESTED_ACTION
        )
        self.app_window.add_header_widget(open_button)

        def _open_clicked(_):
            self.app_window.remove_header_widget(open_button)
            self.app_window.add_header_widget(self.chooser_button)
            self.stack.set_visible_child_name('list')
            print(self.file_chooser.get_filenames())
            self.app_window.views.go_right.set_sensitive(True)
            self.app_window.views.go_left.set_sensitive(True)
            self.sub_title = 'Choose a new location'

        def _selection_changed(chooser):
            is_sensitive = bool(self.file_chooser.get_filenames())
            open_button.set_sensitive(is_sensitive)

        open_button.connect('clicked', _open_clicked)
        self.file_chooser.connect('selection-changed', _selection_changed)
        open_button.show_all()
