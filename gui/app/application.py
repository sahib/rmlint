#!/usr/bin/env python
# encoding: utf-8

# Stdlib:
import gettext

# Internal
from app import APP_TITLE, APP_DESCRIPTION
from app.util import load_css_from_data
from app.about import ShredderAboutDialog
from app.window import MainWindow

from app.views.settings import SettingsView
from app.views.locations import LocationView
from app.views.main import MainView
from app.views.editor import EditorView

# External:
from gi.repository import Gtk, Gio, Rsvg, GdkPixbuf


def _create_action(name, callback=None):
    action = Gio.SimpleAction.new(name, None)
    if callback is not None:
        action.connect('activate', callback)

    return action


def _load_app_icon():
    logo_svg = Gio.resources_lookup_data('/org/gnome/rmlint/logo.svg', 0)
    logo_handle = Rsvg.Handle.new_from_data(logo_svg.get_data())
    logo_handle.set_dpi_x_y(75, 75)
    return logo_handle.get_pixbuf().scale_simple(
        200, 200, GdkPixbuf.InterpType.HYPER
    )

class ShredderApplication(Gtk.Application):
    def __init__(self):
        Gtk.Application.__init__(
            self,
            application_id='org.gnome.Shredder',
            flags=Gio.ApplicationFlags.FLAGS_NONE
        )

    def do_activate(self):
        self.win.present()

    def do_startup(self):
        Gtk.Application.do_startup(self)

        # Make tranlsating strings possible:
        gettext.install(APP_TITLE)

        resource_bundle = Gio.Resource.load('app/resources/app.gresource')
        Gio.resources_register(resource_bundle)

        # Load the application CSS files.
        css_data = Gio.resources_lookup_data('/org/gnome/rmlint/app.css', 0)
        load_css_from_data(css_data.get_data())

        # Init the config system
        self.settings = Gio.Settings.new('org.gnome.Rmlint')

        self.win = MainWindow(self)

        self.add_action(_create_action(
            'about', lambda *_: ShredderAboutDialog(self.win).show_all()
        ))
        self.add_action(_create_action(
            'search', lambda *_: self.win.set_search_mode(True)
        ))
        self.add_action(_create_action(
            'quit', lambda *_: self.quit()
        ))

        self.set_accels_for_action('app.quit', ['<Ctrl>Q'])
        self.set_accels_for_action('app.search', ['<Ctrl>F'])

        # Set the fallback window title.
        # This is only used if no .desktop file is provided.
        self.win.set_wmclass(APP_TITLE, APP_TITLE)

        # Load the application icon
        self.win.set_default_icon(_load_app_icon())

        self.win.views.add_view(SettingsView(self), 'settings')
        self.win.views.add_view(LocationView(self), 'locations')
        self.win.views.add_view(MainView(self), 'main')
        self.win.views.add_view(EditorView(self), 'editor')

        # Set the default view visible at startup
        self.win.views.switch('editor')
        self.win.show_all()
