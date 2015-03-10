#!/usr/bin/env python
# encoding: utf-8

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


class LocationView(Gtk.ScrolledWindow):
    def __init__(self):
        Gtk.ScrolledWindow.__init__(self)

        box = Gtk.ListBox()
        box.set_selection_mode(Gtk.SelectionMode.BROWSE)
        box.set_size_request(350, -1)
        box.set_hexpand(True)
        self.add(box)

        monitor = Gio.VolumeMonitor.get()
        for mount in monitor.get_mounts():
            entry = LocationEntry(
                mount.get_name(),
                mount.get_root().get_path(),
                mount.get_symbolic_icon()
            )

            if len(box) is not 0:
                box.insert(Gtk.Separator(), -1)

            # Prepend to the front
            row = Gtk.ListBoxRow()
            row.set_can_focus(False)
            row.add(entry)

            box.insert(row, -1)
