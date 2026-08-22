"""Shredder's GtkApplication implementation.

It loads all initially required resources and triggers
the gui build by instancing the MainWindow.
"""

import gettext
import logging
import os

from gi.repository import GdkPixbuf, Gio, GLib, Gtk

from shredder import APP_TITLE
from shredder.about import AboutDialog
from shredder.runner import Script
from shredder.util import load_css_from_data
from shredder.views.editor import EditorView
from shredder.views.locations import LocationView
from shredder.views.runner import RunnerView
from shredder.views.settings import SettingsView
from shredder.window import MainWindow

LOGGER = logging.getLogger('application')


def have_feature(feature):
    """Execute rmlint --version to check for some feature.

    --version will print the compile time configuration of rmlint.
    If a feature is missing, -somefeature is printed. A + in front else.
    """
    proc = Gio.Subprocess.new(
        ['rmlint', '--version'],
        Gio.SubprocessFlags.STDERR_PIPE
    )
    result, _, data = proc.communicate_utf8()
    if not result or not data:
        return False

    return '+' + feature in data


def _create_action(name, callback=None):
    """Create a named GAction with a callback for its activation"""
    action = Gio.SimpleAction.new(name, None)
    if callback is not None:
        action.connect('activate', callback)

    return action


def _load_app_icon():
    """Load & render the application svg icon from the resource bundle.
    Or return None if no loader is available.
    """
    try:
        return GdkPixbuf.Pixbuf.new_from_resource_at_scale(
            '/org/gnome/shredder/shredder.svg', 200, 200, True
        )
    except GLib.Error as err:
        LOGGER.warning('Could not render the application icon: %s', err)
        return None


class Application(Gtk.Application):
    """GtkApplication implementation of Shredder."""
    def __init__(self, options):
        GLib.set_application_name(APP_TITLE)

        super().__init__(
            application_id='org.gnome.Shredder',
            flags=Gio.ApplicationFlags.DEFAULT_FLAGS
        )

        self.cmd_opts = options
        self.settings = self.win = None

    def do_activate(self, **kw):
        Gtk.Application.do_activate(self, **kw)
        self.win.present()

    def do_startup(self, **kw):
        Gtk.Application.do_startup(self, **kw)

        # Make translating strings possible:
        # (We use the same message catalouge as rmlint)
        gettext.install('rmlint')

        rel_dir = os.path.dirname(__file__)
        resource_file = os.path.join(rel_dir, 'resources/shredder.gresource')
        LOGGER.info("Loading resources from: %s", resource_file)
        resource_bundle = Gio.Resource.load(resource_file)
        Gio.resources_register(resource_bundle)

        # Load the application CSS files.
        css_data = Gio.resources_lookup_data(
            '/org/gnome/shredder/shredder.css', 0
        )

        try:
            load_css_from_data(css_data.get_data())
        except Exception as err:
            LOGGER.warning("Failed to load css data: %s", err)

        # Init the config system
        self.settings = Gio.Settings.new('org.gnome.Shredder')

        self.win = MainWindow(self)

        self.add_action(_create_action(
            'settings', lambda *_: self.win.views.switch('settings')
        ))
        self.add_action(_create_action(
            'about', lambda *_: AboutDialog(self.win).show_all()
        ))
        self.add_action(_create_action(
            'search', lambda *_: self.win.views.set_search_mode(True)
        ))
        self.add_action(_create_action(
            'activate', lambda *_: self.win.views.do_default_action()
        ))
        self.add_action(_create_action(
            'quit', lambda *_: self.quit()
        ))

        self.set_accels_for_action('app.quit', ['<Ctrl>Q'])
        self.set_accels_for_action('app.search', ['<Ctrl>F'])
        self.set_accels_for_action('app.activate', ['<Ctrl>Return'])

        # Load the application icon
        app_icon = _load_app_icon()
        if app_icon is not None:
            self.win.set_default_icon(app_icon)

        LOGGER.debug('Instancing views.')
        self.win.views.add_view(SettingsView(self), 'settings')
        self.win.views.add_view(LocationView(self), 'locations')
        self.win.views.add_view(RunnerView(self), 'runner')
        self.win.views.add_view(EditorView(self), 'editor')
        LOGGER.debug('Done instancing views.')

        initial_view = 'locations'

        if self.cmd_opts.tagged or self.cmd_opts.untagged:
            self.win.views['runner'].trigger_run(
                self.cmd_opts.untagged or [],
                self.cmd_opts.tagged or []
            )
            initial_view = 'runner'

        if self.cmd_opts.show_settings:
            initial_view = 'settings'

        for path in self.cmd_opts.locations or []:
            self.win.views['locations'].add_recent_item(path)

        if self.cmd_opts.script:
            self.win.views['editor'].override_script(
                Script(self.cmd_opts.script)
            )
            initial_view = 'editor'

        # Set the default view visible at startup
        self.win.views.switch(initial_view)
        self.win.show_all()
