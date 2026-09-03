"""Own module for the about dialog."""
from gi.repository import Gio, Gtk

from shredder import APP_DESCRIPTION, APP_TITLE, version

MAIN_AUTHORS = [
    'Christopher Pahl <sahib@online.de>',
    'Daniel Thomas <thomas_d_j@yahoo.com.au>',
    'Cebtenzzre <cebtenzzre@gmail.com>',
    'Vassili Tchersky <vt+rmlint@vbcy.org>'
]

# Change when needed.
DOCUMENTERS = MAIN_AUTHORS


class AboutDialog(Gtk.AboutDialog):
    """GtkAboutDialog for Shreddder"""
    def __init__(self, app_win):
        super().__init__()

        self.set_transient_for(app_win)
        self.set_modal(True)
        self.set_license_type(Gtk.License.GPL_3_0)
        self.set_comments(APP_DESCRIPTION)
        self.set_wrap_license(True)
        self.set_program_name(APP_TITLE)
        self.set_version(version.get_version())
        self.set_authors(MAIN_AUTHORS)
        self.set_documenters(DOCUMENTERS)
        self.set_website('https://rmlint.rtfd.org')
        self.set_website_label('rmlint.rtfd.org')
        self.set_logo(None)


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
        app_icon = _load_app_icon()
        if app_icon is not None:
            win.set_default_icon(app_icon)

        about = AboutDialog(win)
        about.show_all()

        Gtk.main()

    main()
