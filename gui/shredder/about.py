#!/usr/bin/env python
# encoding: utf-8

"""Own module for the about dialog."""


# Stdlib:
import re
import logging

# Internal
from shredder import APP_TITLE, APP_DESCRIPTION

# External:
from gi.repository import Gtk, Gio


LOGGER = logging.getLogger('about')


MAIN_AUTHORS = [
    'Christopher Pahl <sahib@online.de>',
    'Daniel Thomas <thomas_d_j@yahoo.com.au>',
    'Cebtenzzre <cebtenzzre@gmail.com>',
]


# Change when needed.
DOCUMENTERS = MAIN_AUTHORS


def _guess_rmlint_version():
    """Execute rmlint --version to extract the version.

    Shredder is always versioned the same way as rmlint.
    This is to make version problems less likely.
    """
    proc = Gio.Subprocess.new(
        ['rmlint', '--version'],
        Gio.SubprocessFlags.STDERR_PIPE
    )
    result, _, data = proc.communicate_utf8()
    if result and data:
        match = re.search(r'version (\d+\.\d+\.\d+)', data)
        if match:
            return match.group(1)

    return '?.?.?'


class AboutDialog(Gtk.AboutDialog):
    """GtkAboutDialog for Shreddder"""
    def __init__(self, app_win):
        Gtk.AboutDialog.__init__(self)

        try:
            buttons = list(self.get_action_area())
            close_button = buttons[2]
            close_button.connect('clicked', lambda _: self.destroy())
            license_button = buttons[1]
            license_button.set_no_show_all(True)
        except IndexError:
            LOGGER.error('GtkAboutDialog layout changed...')

        self.set_transient_for(app_win)
        self.set_modal(True)
        self.set_license_type(Gtk.License.GPL_3_0)
        self.set_comments(APP_DESCRIPTION)
        self.set_wrap_license(True)
        self.set_program_name(APP_TITLE)
        self.set_version(_guess_rmlint_version())
        self.set_authors(MAIN_AUTHORS)
        self.set_documenters(DOCUMENTERS)
        self.set_website('http://rmlint.rtfd.org')
        self.set_website_label('rmlint.rtfd.org')
        self.set_logo(None)
        self.show_all()


if __name__ == '__main__':
    def main():
        """Show the about dialog as modal window."""
        import os
        from shredder.application import _load_app_icon

        win = Gtk.Window()
        win.connect('destroy', Gtk.main_quit)
        win.show_all()

        rel_dir = os.path.dirname(__file__)
        resource_file = os.path.join(rel_dir, 'resources/shredder.gresource')
        resource_bundle = Gio.Resource.load(resource_file)
        Gio.resources_register(resource_bundle)
        win.set_default_icon(_load_app_icon())

        about = AboutDialog(win)
        about.show()

        Gtk.main()

    main()
