#!/usr/bin/env python
# encoding: utf-8

"""Shredder's GtkApplication implementation.

It loads all initially required resources and triggers
the gui build by instancing the MainWindow.
"""

# Stdlib:
import os
import sys
import gettext
import logging
import shutil
import subprocess

# External:
from gi.repository import Gtk, Gio, Rsvg, GdkPixbuf

# Internal
from shredder import APP_TITLE
from shredder.util import load_css_from_data
from shredder.about import AboutDialog
from shredder.runner import Script
from shredder.window import MainWindow

from shredder.views.settings import SettingsView
from shredder.views.locations import LocationView
from shredder.views.runner import RunnerView
from shredder.views.editor import EditorView


LOGGER = logging.getLogger('application')


def _language_catalogs():
    """Return gettext catalog names requested by the current locale."""
    catalogs = []
    seen = set()

    for env_name in ('LANGUAGE', 'LC_ALL', 'LC_MESSAGES', 'LANG'):
        env_value = os.environ.get(env_name)
        if not env_value:
            continue

        for lang in env_value.split(':'):
            lang = lang.split('.', 1)[0].split('@', 1)[0]
            if not lang or lang in ('C', 'POSIX'):
                continue

            for candidate in (lang, lang.split('_', 1)[0]):
                if candidate and candidate not in seen:
                    seen.add(candidate)
                    catalogs.append(candidate)

    return catalogs


def _install_gettext(rel_dir):
    """Install gettext, preferring source-tree catalogs when available."""
    po_dir = os.path.abspath(os.path.join(rel_dir, '..', '..', 'po'))

    for lang in _language_catalogs():
        try:
            translation = gettext.translation(
                'rmlint',
                localedir=po_dir,
                languages=[lang]
            )
        except FileNotFoundError:
            continue
        else:
            translation.install()
            return

    gettext.install('rmlint')


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
    """Load & render the application svg icon from the resource bundle"""
    logo_svg = Gio.resources_lookup_data('/org/gnome/shredder/shredder.svg', 0)
    logo_handle = Rsvg.Handle.new_from_data(logo_svg.get_data())
    logo_handle.set_dpi_x_y(75, 75)
    return logo_handle.get_pixbuf().scale_simple(
        200, 200, GdkPixbuf.InterpType.HYPER
    )


class Application(Gtk.Application):
    """GtkApplication implementation of Shredder."""
    def __init__(self, options):
        Gtk.Application.__init__(
            self,
            application_id='org.gnome.Shredder',
            flags=Gio.ApplicationFlags.FLAGS_NONE
        )
        self.cmd_opts = options
        self.settings = self.win = None
        self._schema_cache_dir = None

        # Check compile time features of rmlint that we need later.
        if not have_feature('replay'):
            LOGGER.error('No support for +replay in rmlint binary.')
            LOGGER.error('Please recompile with --with-json-glib…')
            LOGGER.error('…and `json-glib-1.0` installed on your system.')
            sys.exit(-1)

    def do_activate(self, **kw):
        Gtk.Application.do_activate(self, **kw)
        self.win.present()

    def do_startup(self, **kw):
        Gtk.Application.do_startup(self, **kw)

        # Make translating strings possible:
        # (We use the same message catalouge as rmlint)
        rel_dir = os.path.dirname(__file__)
        _install_gettext(rel_dir)

        resource_file = os.path.join(rel_dir, 'resources/shredder.gresource')
        LOGGER.info('Loading resources from: ' + resource_file)
        resource_bundle = Gio.Resource.load(resource_file)
        Gio.resources_register(resource_bundle)

        # Load the application CSS files.
        css_data = Gio.resources_lookup_data(
            '/org/gnome/shredder/shredder.css', 0
        )

        try:
            load_css_from_data(css_data.get_data())
        except Exception as err:
            LOGGER.warning("Failed to load css data: " + str(err))

        # Init the config system
        try:
            self.settings = self._new_settings(rel_dir)
        except RuntimeError as err:
            LOGGER.error(str(err))
            sys.exit(1)

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

        # Set the fallback window title.
        # This is only used if no .desktop file is provided.
        self.win.set_wmclass(APP_TITLE, APP_TITLE)

        # Load the application icon
        self.win.set_default_icon(_load_app_icon())

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

    def _new_settings(self, rel_dir):
        schema_id = 'org.gnome.Shredder'
        default_source = Gio.SettingsSchemaSource.get_default()
        if default_source and default_source.lookup(schema_id, True):
            return Gio.Settings.new(schema_id)

        schema_dir = os.path.join(rel_dir, 'resources')
        schema_source = self._new_schema_source(schema_dir, default_source)
        if schema_source:
            schema = schema_source.lookup(schema_id, False)
            if schema:
                return Gio.Settings.new_full(schema, None, None)

        raise RuntimeError(
            'Could not load GSettings schema org.gnome.Shredder. '
            'Run `glib-compile-schemas gui/shredder/resources` or install the GUI.'
        )

    def _new_schema_source(self, schema_dir, default_source):
        compiled_schema = os.path.join(schema_dir, 'gschemas.compiled')
        if os.path.exists(compiled_schema):
            return Gio.SettingsSchemaSource.new_from_directory(
                schema_dir, default_source, False
            )

        schema_xml = os.path.join(schema_dir, 'org.gnome.Shredder.gschema.xml')
        compiler = shutil.which('glib-compile-schemas')
        if not os.path.exists(schema_xml) or not compiler:
            return None

        cache_dir = os.path.join(
            os.path.expanduser('~'), '.cache', 'shredder', 'schemas'
        )
        os.makedirs(cache_dir, exist_ok=True)
        shutil.copy2(schema_xml, cache_dir)

        try:
            subprocess.run(
                [compiler, cache_dir],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                check=True
            )
        except (OSError, subprocess.CalledProcessError) as err:
            LOGGER.warning('Could not compile GSettings schema: %s', err)
            return None

        self._schema_cache_dir = cache_dir
        return Gio.SettingsSchemaSource.new_from_directory(
            cache_dir, default_source, False
        )
