#!/usr/bin/env python
# encoding: utf-8

# Stdlib:
import re

# Internal
from app import APP_TITLE, APP_DESCRIPTION

# External:
from gi.repository import Gtk, Gio


MAIN_AUTHORS = [
    'Christopher Pahl <sahib@online.de>',
    'Daniel Thomas <thomas_d_j@yahoo.com.au>'
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
        match = re.search('version (\d.\d.\d)', data)
        if match:
            return match.group(1)

    return '?.?.?'

class ShredderAboutDialog(Gtk.AboutDialog):
    def __init__(self, app_win):
        Gtk.AboutDialog.__init__(self)

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
        self.set_logo(None)
        self.show_all()
