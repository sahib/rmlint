#!/usr/bin/env python
# encoding: utf-8

# Stdlib:
import gettext

# Internal
from app import APP_TITLE, APP_DESCRIPTION
from app.window import MainWindow
from app.views.settings import SettingsView
from app.views.locations import LocationView
from app.views.progress import ProgressView
from app.views.main import MainView
from app.util import load_css_from_data, IndicatorLabel

# External:
from gi.repository import Gtk, GLib, Gio, Rsvg, GdkPixbuf


def create_action(name, callback=None, var_type=None, value=None):
    if var_type is None:
        action = Gio.SimpleAction.new(name, None)
    else:
        action = Gio.SimpleAction.new_stateful(
            name,
            GLib.VariantType.new(var_type),
            GLib.Variant(var_type, value)
        )

    if callback is not None:
        action.connect('activate', callback)

    return action


class MainApplication(Gtk.Application):
    def __init__(self):
        Gtk.Application.__init__(
            self,
            application_id='org.gnome.Rmlint',
            flags=Gio.ApplicationFlags.FLAGS_NONE
        )

    def do_activate(self):
        self.win.present()

    def do_startup(self):
        Gtk.Application.do_startup(self)

        # Make tranlsating strings possible:
        gettext.install(APP_TITLE)

        # TODO: make sure to use correct path
        resource_bundle = Gio.Resource.load('app/resources/app.gresource')
        Gio.resources_register(resource_bundle)

        # Load the application CSS files.
        css_data = Gio.resources_lookup_data('/org/gnome/rmlint/app.css', 0)
        load_css_from_data(css_data.get_data())

        # Init the config system
        self.settings = Gio.Settings.new('org.gnome.Rmlint')

        self.add_action(create_action("about", self._on_show_about))
        self.add_action(create_action("search", lambda *_: self.win.set_search_mode(True)))
        self.add_action(create_action("quit", lambda *_: self.quit()))

        self.set_accels_for_action('app.quit', ['<Ctrl>Q'])
        self.set_accels_for_action('app.work', ['<Ctrl>W'])
        self.set_accels_for_action('app.search', ['<Ctrl>F'])

        location_view = LocationView()
        easy_view = SettingsView(self)
        easy_view.fill_from_settings()

        self.win = MainWindow(self)

        # Set the fallback window title.
        # This is only used if no .desktop file is provided.
        self.win.set_wmclass(APP_TITLE, APP_TITLE)

        # Load the application icon
        logo_svg = Gio.resources_lookup_data('/org/gnome/rmlint/logo.svg', 0)
        logo_handle = Rsvg.Handle.new_from_file('app/resources/logo.svg')
        logo_handle.set_dpi_x_y(75, 75)
        logo_pix = logo_handle.get_pixbuf()
        logo_pix = logo_pix.scale_simple(200, 200, GdkPixbuf.InterpType.HYPER)
        self.win.set_default_icon(logo_pix)

        # TODO: Debugging code. Remove when done.
        def _fake_work(*_):
            self.mark_busy()
            def _f():
                self.win.show_progress(_f.f)
                _f.f += 0.005
                return True
            _f.f = 0.0

            self.win.show_action_buttons("Apply", "Cancel")

            source_id = GLib.timeout_add(10, _f)
            def _h():
                GLib.source_remove(source_id)
                self.win.hide_progress()
                self.win.show_infobar('Did some work!')
                self.win.hide_action_buttons()
                self.unmark_busy()

            GLib.timeout_add(3000, _h)


        self.add_action(create_action("work", _fake_work))
        self.win.views.add_view(easy_view, 'settings')
        # self.win.views.add_view(ChartView(), 'chart') # TODO
        self.win.views.add_view(location_view, 'locations')
        self.win.views.add_view(ProgressView(self), 'overview')

        l = IndicatorLabel()
        l.set_markup('<small>120 Entries</small> ✔')

        self.win.views.add_view(l, 'detail')

        self.win.views.add_view(MainView(self), 'main')
        self.win.views.switch('main')
        l.set_valign(Gtk.Align.CENTER)
        l.set_halign(Gtk.Align.CENTER)
        l.set_hexpand(False)
        l.set_vexpand(False)

        def _x():
            GLib.timeout_add(0000, lambda: l.set_state(IndicatorLabel.SUCCESS))
            GLib.timeout_add(1000, lambda: l.set_state(IndicatorLabel.WARNING))
            GLib.timeout_add(2000, lambda: l.set_state(IndicatorLabel.ERROR))
            GLib.timeout_add(3000, lambda: l.set_state(IndicatorLabel.THEME))
            GLib.timeout_add(4000, lambda: l.set_state(None))
            return True
        GLib.timeout_add(5000, _x)
        _x()

        self.win.show_all()

    def action_clicked(self, action, variant):
        print(action, variant)

        if variant:
            action.set_state(variant)

    def _on_show_about(self, *_):
        dialog = Gtk.AboutDialog()

        main_authors = [
            'Christopher Pahl <sahib@online.de>',
            'Daniel Thomas <thomas_d_j@yahoo.com.au>'
        ]

        dialog.set_transient_for(self.win)
        dialog.set_modal(True)
        dialog.set_license_type(Gtk.License.GPL_3_0)
        dialog.set_comments(APP_DESCRIPTION)
        dialog.set_wrap_license(True)
        dialog.set_program_name('Besen')
        dialog.set_version('2.1.0')  # TODO: Paste rmlint --version here.
        dialog.set_authors(main_authors)
        dialog.set_documenters(main_authors)
        dialog.set_website('http://rmlint.rtfd.org')
        dialog.set_logo(None)
        dialog.show_all()
