#!/usr/bin/env python
# encoding: utf-8

"""Location view.

Give the users a list of directories he might want to scan.
Also give him the choice of adding new directories.

Integrate with GtkRecentManager and detect mounted volumes.
Also show the directory size alongside.
"""


# Stdlib:
import os
import logging

# External:
from gi.repository import Gtk
from gi.repository import Gio
from gi.repository import GLib
from gi.repository import GObject

# Internal:
from shredder.util import View, IconButton, size_to_human_readable


LOGGER = logging.getLogger('locations')


class DeferSizeLabel(Gtk.Bin):
    """Recursively calculates the size of a directory in a non-blocking way.

    While calculating the widget will look like a spinner, when done the size
    is displayed as normal text label.
    """
    def __init__(self, path):
        Gtk.Bin.__init__(self)

        spinner = Gtk.Spinner()
        spinner.start()
        self.add(spinner)

        # `du` still seems to be the fastest way to do the job.
        # All self-implemented ways in python were way slower.
        du_proc = Gio.Subprocess.new(
            ['du', '-s', path],
            Gio.SubprocessFlags.STDERR_SILENCE |
            Gio.SubprocessFlags.STDOUT_PIPE
        )
        du_proc.communicate_utf8_async(None, None, self._du_finished)

    def _du_finished(self, du_proc, result):
        """Called when the coreutil du finished running. Harvest results."""
        result, du_data, _ = du_proc.communicate_utf8_finish(result)
        kbytes = int(''.join([num for num in du_data if num.isdigit()]))

        self.remove(self.get_child())
        self.add(Gtk.Label(size_to_human_readable(kbytes * 1024)))
        self.show_all()


class LocationEntry(Gtk.ListBoxRow):
    """A single entry representing an existing file system location."""
    preferred = GObject.Property(type=bool, default=False)

    def __init__(self, name, path, themed_icon, fill_level=None):
        Gtk.ListBoxRow.__init__(self)
        self.set_size_request(-1, 80)
        self.set_can_focus(False)

        # CSS Name
        self.set_name('ShredderLocationEntry')

        self.path, self.name = path, name

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
        icon_img.props.pixel_size = 64

        icon_img.set_halign(Gtk.Align.START)
        icon_img.set_margin_start(3)
        icon_img.set_margin_end(10)
        icon_img.set_vexpand(True)
        icon_img.set_valign(Gtk.Align.FILL)

        self.check_box = Gtk.Switch()
        self.check_box.connect('notify::active', self.on_check_box_toggled)
        self.check_box.set_tooltip_text('Prefer this directory?')
        self.check_box.set_margin_end(5)
        self.check_box.set_margin_top(13)
        self.check_box.set_can_focus(False)

        self.separator = Gtk.Separator()
        self.separator.set_hexpand(True)
        self.separator.set_halign(Gtk.Align.FILL)

        #######################
        # Put it all together #
        #######################

        grid = Gtk.Grid()
        self.add(grid)

        grid.attach(icon_img, 0, 0, 5, 5)
        grid.attach(name_label, 5, 2, 1, 1)
        grid.attach(path_label, 5, 3, 1, 1)
        grid.attach(self.check_box, 7, 2, 1, 1)
        grid.attach(self.separator, 0, 8, 8, 1)

        if fill_level is not None:
            level_bar = Gtk.LevelBar()
            level_bar.set_valign(Gtk.Align.START)
            level_bar.set_halign(Gtk.Align.END)
            level_bar.set_vexpand(False)
            level_bar.set_size_request(150, 10)
            level_bar.set_margin_end(20)
            level_bar.set_margin_top(20)

            # Define new values for 'high' and 'low'
            level_bar.remove_offset_value(Gtk.LEVEL_BAR_OFFSET_HIGH)
            level_bar.remove_offset_value(Gtk.LEVEL_BAR_OFFSET_LOW)
            level_bar.add_offset_value(Gtk.LEVEL_BAR_OFFSET_LOW, 0.75)
            level_bar.add_offset_value(Gtk.LEVEL_BAR_OFFSET_HIGH, 0.25)

            level_label = Gtk.Label()
            level_label.set_valign(Gtk.Align.START)
            level_label.set_halign(Gtk.Align.END)
            level_label.set_margin_end(20)
            level_label.set_vexpand(False)

            used, total = fill_level
            percent = int(used / total * 100)
            level_label.set_markup(
                '<small>{f} / {t} - {p}%</small>'.format(
                    f=size_to_human_readable(used),
                    t=size_to_human_readable(total),
                    p=percent
                )
            )
            level_bar.set_value(percent / 100)

            grid.attach(level_label, 6, 3, 1, 1)
            grid.attach(level_bar, 6, 2, 1, 1)
        else:
            size_widget = DeferSizeLabel(path)
            size_widget.set_margin_top(15)
            size_widget.set_margin_end(20)
            grid.attach(size_widget, 6, 2, 1, 1)

    def on_check_box_toggled(self, btn, _):
        """Called once the `original` checkbox was hit."""
        ctx = self.get_style_context()
        if btn.get_active():
            ctx.add_class('original')
        else:
            ctx.remove_class('original')

        self.props.preferred = btn.get_active()


class LocationView(View):
    """The actual view instance."""
    def __init__(self, app):
        View.__init__(self, app)
        self.selected_locations = []
        self.known_paths = set()
        self._set_title()

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
            'clicked', self.on_chooser_button_clicked
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
        self.box.connect('row-activated', self.on_row_clicked)

        self.search_entry.connect(
            'search-changed', self.on_search_changed
        )

        self.volume_monitor = Gio.VolumeMonitor.get()
        self.recent_mgr = Gtk.RecentManager.get_default()
        self.recent_mgr.connect('changed', self.refill_entries)
        self.volume_monitor.connect('volume-changed', self.refill_entries)
        self.volume_monitor.connect('drive-changed', self.refill_entries)
        self.volume_monitor.connect('mount-changed', self.refill_entries)
        self.refill_entries()

        run_button = IconButton('edit-find-symbolic', 'Scan folders')
        run_button.connect('clicked', self._run_clicked)
        run_button.get_style_context().add_class(
            Gtk.STYLE_CLASS_SUGGESTED_ACTION
        )

        del_button = IconButton('user-trash-symbolic', 'Remove from list')
        del_button.connect('clicked', self._del_clicked)

        self.selected_label = Gtk.Label()
        self.selected_label.get_style_context().add_class(
            Gtk.STYLE_CLASS_DIM_LABEL
        )

        action_bar = Gtk.ActionBar()
        action_bar.pack_start(del_button)
        action_bar.set_center_widget(self.selected_label)
        action_bar.pack_end(run_button)

        self.revealer = Gtk.Revealer()
        self.revealer.add(action_bar)
        self.revealer.set_hexpand(True)
        self.revealer.set_halign(Gtk.Align.FILL)

        grid = Gtk.Grid()
        grid.attach(self.stack, 0, 0, 1, 1)
        grid.attach(self.revealer, 0, 1, 1, 1)

        self.add(grid)

    def _set_title(self):
        """Make it an own method, so we don't need to retype it."""
        self.sub_title = 'Choose locations to scan'

    def add_recent_item(self, path):
        """Add item to GtkRecentManager"""
        data = Gtk.RecentData()
        data.is_private = False
        data.app_exec = 'gedit %s'
        data.app_name = 'gedit'
        data.description = path
        data.display_name = path
        data.mime_type = 'inode/directory'

        if not self.recent_mgr.add_full(path, data):
            LOGGER.warning('Could not add to recently used: ' + path)

    def refill_entries(self, *_):
        """Re-read all LocationEntries from every possible source."""
        LOGGER.info('Refilling location entries')
        for child in list(self.box):
            self.box.remove(child)

        self.known_paths = set()

        # Static entries:
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

        # Mounted volumes:
        for mount in self.volume_monitor.get_mounts():
            info = mount.get_root().query_filesystem_info(
                ','.join([
                    Gio.FILE_ATTRIBUTE_FILESYSTEM_SIZE,
                    Gio.FILE_ATTRIBUTE_FILESYSTEM_USED
                ])
            )

            self.add_entry(
                mount.get_name(),
                mount.get_root().get_path(),
                mount.get_icon(),
                fill_level=(
                    info.get_attribute_uint64(
                        Gio.FILE_ATTRIBUTE_FILESYSTEM_USED),
                    info.get_attribute_uint64(
                        Gio.FILE_ATTRIBUTE_FILESYSTEM_SIZE)))

        # Recently used items:
        for item in self.recent_mgr.get_items():
            # Note: item.get_exists() tells us bullshit sometimes.
            if item.get_mime_type() != 'inode/directory':
                continue

            path = item.get_uri()
            if path.startswith('file://'):
                path = path[7:]

            self.add_entry(
                os.path.basename(path),
                path,
                item.get_gicon()
            )

        self.show_all()

    def add_entry(self, name, path, icon, fill_level=None, idx=-1):
        """Add a new LocationEntry to the list"""
        path = path.strip()

        if path == '/':
            return

        if path in self.known_paths:
            LOGGER.info('In known paths: ' + path)
            return

        entry = LocationEntry(name, path, icon, fill_level)
        self.known_paths.add(path)
        self.box.insert(entry, idx)

        entry.connect(
            'notify::preferred',
            lambda *_: self._update_selected_label()
        )
        return entry

    def on_row_clicked(self, _, row):
        """Highlight an entry when a row was clicked."""
        style_ctx = row.get_style_context()
        if style_ctx.has_class('selected'):
            style_ctx.remove_class('selected')
            self.selected_locations.remove(row)
        else:
            style_ctx.add_class('selected')
            self.selected_locations.append(row)

        self._update_selected_label()

    def _update_selected_label(self):
        """Update the lower count of selected LocationEntries."""
        prefd_paths = sum(rw.props.preferred for rw in self.selected_locations)
        self.selected_label.set_markup(
            '{sel} directories - {pref} of them preferred'.format(
                sel=len(self.selected_locations),
                pref=prefd_paths
            )
        )
        self.revealer.set_reveal_child(bool(self.selected_locations))

    def on_search_changed(self, _):
        """Called once the user enteres a new search query."""
        if self.is_visible:
            self.box.invalidate_filter()

    def _filter_func(self, row):
        """Decide if a row shoul be visible depending on the search term."""
        query = self.search_entry.get_text().lower()
        if query in row.path.lower():
            return True

        return query in row.name.lower()

    def on_view_enter(self):
        """Called when the view gets visible."""
        self.add_header_widget(self.chooser_button)
        self.chooser_button.show_all()

        # If no process is currently running it should not be
        # possible to go right from locations view.
        main_view = self.app_window.views['runner']
        if not main_view.is_running:
            GLib.idle_add(
                lambda: self.app_window.views.go_right.set_sensitive(False)
            )

    def on_chooser_button_clicked(self, _):
        """Button click on the location chooser."""
        self.stack.set_visible_child_name('chooser')
        self.app_window.remove_header_widget(self.chooser_button)
        self.app_window.views.go_right.set_sensitive(False)
        self.app_window.views.go_left.set_sensitive(False)
        self.revealer.set_reveal_child(False)
        self.sub_title = 'Choose a new location'

        open_button = IconButton('emblem-ok-symbolic', 'Add selected')
        open_button.get_style_context().add_class(
            Gtk.STYLE_CLASS_SUGGESTED_ACTION
        )

        close_button = IconButton('window-close-symbolic', 'Cancel')

        self.add_header_widget(open_button)
        self.add_header_widget(close_button, align=Gtk.Align.START)

        def _go_back():
            """Switch back to the LocationEntry list."""
            self.app_window.remove_header_widget(open_button)
            self.app_window.remove_header_widget(close_button)
            self.add_header_widget(self.chooser_button)
            self.stack.set_visible_child_name('list')
            self.app_window.views.go_right.set_sensitive(True)
            self.app_window.views.go_left.set_sensitive(True)
            self.revealer.set_reveal_child(True)
            self._set_title()

        def _open_clicked(_):
            """The open file button was clicked. Add paths."""
            for path in self.file_chooser.get_filenames():
                name = os.path.basename(path)
                # self.recent_mgr.add_item(path)
                self.add_recent_item(path)
                entry = self.add_entry(
                    name, path, Gio.ThemedIcon(
                        name='folder-new'
                    ),
                    idx=0
                )
                self.box.select_row(entry)
            self.box.show_all()

            _go_back()

        def _close_clicked(_):
            """Abort choosing."""
            _go_back()

        def _selection_changed(_):
            """Make the open button sensitive when something is selected."""
            is_sensitive = bool(self.file_chooser.get_filenames())
            open_button.set_sensitive(is_sensitive)

        open_button.connect('clicked', _open_clicked)
        close_button.connect('clicked', _close_clicked)
        self.file_chooser.connect('selection-changed', _selection_changed)
        open_button.show_all()
        close_button.show_all()

    def _run_clicked(self, _):
        """Switch one view further to the runner view."""
        main_view = self.app_window.views['runner']

        tagged, untagged = [], []
        for row in self.selected_locations:
            if row.props.preferred:
                tagged.append(row.path)
            else:
                untagged.append(row.path)

        main_view.trigger_run(untagged, tagged)
        self.app_window.views.switch('runner')

    def _del_clicked(self, _):
        """Delete all selected LocationEntries."""
        for row in self.selected_locations:
            LOGGER.debug('Removing location entry:' + row.path)
            self.box.remove(row)

            try:
                Gtk.RecentManager.get_default().remove_item(row.path)
            except GLib.Error:
                LOGGER.warning('Could not remove recent item: %s', row.path)

            if row.path in self.known_paths:
                self.known_paths.remove(row.path)
        self.selected_locations = []
        self._update_selected_label()

    def on_default_action(self):
        pass
