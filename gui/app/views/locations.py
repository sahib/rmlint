#!/usr/bin/env python
# encoding: utf-8

# Internal:
from app.util import View, IconButton

from gi.repository import Gtk, GLib, Gio


class LocationEntry(Gtk.Grid):
    def __init__(self, name, path, themed_icon):
        Gtk.Grid.__init__(self)
        self.set_border_width(5)
        self.set_can_focus(False)

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
        path_label.set_halign(Gtk.Align.START)

        icon_img = Gtk.Image.new_from_gicon(
            themed_icon,
            Gtk.IconSize.DIALOG
        )
        icon_img.set_halign(Gtk.Align.END)

        self.attach(name_label, 0, 0, 1, 1)
        self.attach(icon_img, 1, 0, 3, 3)
        self.attach(path_label, 0, 1, 1, 1)


class LocationView(View):
    def __init__(self, app):
        View.__init__(self, app)

        box = Gtk.ListBox()
        box.set_selection_mode(Gtk.SelectionMode.MULTIPLE)
        box.set_size_request(350, -1)
        box.set_hexpand(True)
        box.set_placeholder(Gtk.Label('No locations mounted.'))
        box.set_valign(Gtk.Align.FILL)

        self.chooser_button = IconButton(
            'list-add-symbolic', 'Open Location'
        )
        self.chooser_button.connect(
            'clicked', self.on_chooser_button_clicked
        )

        self.file_chooser = Gtk.FileChooserWidget()
        self.file_chooser.set_select_multiple(True)
        self.file_chooser.set_action(Gtk.FileChooserAction.SELECT_FOLDER)

        self.stack = Gtk.Stack()
        self.stack.set_transition_type(Gtk.StackTransitionType.SLIDE_UP)
        self.stack.add_named(box, 'list')
        self.stack.add_named(self.file_chooser, 'chooser')
        self.add(self.stack)

        monitor = Gio.VolumeMonitor.get()
        for mount in monitor.get_mounts():
            entry = LocationEntry(
                mount.get_name(),
                mount.get_root().get_path(),
                mount.get_symbolic_icon()
            )

            if len(box) is not 0:
                row = Gtk.ListBoxRow()
                row.add(Gtk.Separator())
                row.set_selectable(False)
                row.set_activatable(False)
                box.insert(Gtk.Separator(), -1)

            # Prepend to the front
            row = Gtk.ListBoxRow()
            row.set_can_focus(False)
            row.add(entry)

            box.insert(row, -1)

    def on_view_enter(self):
        self.app_window.add_header_widget(self.chooser_button)

    def on_view_leave(self):
        self.app_window.remove_header_widget(self.chooser_button)

    def on_chooser_button_clicked(self, btn):
        self.stack.set_visible_child_name('chooser')
        self.app_window.remove_header_widget(self.chooser_button)

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

        open_button.connect('clicked', _open_clicked)
        open_button.show_all()
