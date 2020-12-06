#!/usr/bin/env python3
# encoding: utf-8
#!/usr/bin/env python

from distutils.core import setup
from distutils.command.install import install

import os
import sys
import subprocess


def read_version():
    with open('../.version', 'r') as handle:
        version_string = handle.read()

    version_numbers, _ = version_string.split(' ', 1)
    return version_numbers


def get_prefix():
    # distutils apparently has no sane way to get the install prefix early.
    # (I realize that this is a stupid hack to do something obvious)
    for idx, arg in enumerate(sys.argv):
        if arg == '--user':
            return os.path.expanduser('~/.local')

        if arg.startswith('--prefix'):
            if '=' in arg:
                _, path = arg.split('=', 1)
                return path
            else:
                return sys.argv[idx + 1]

    return '/usr'


PREFIX = get_prefix()


class PrePlusPostInstall(install):
    def run(self):
        # Compile the resource bundle freshly
        print('==> Compiling resource bundle')

        if os.access('shredder/resources/shredder.gresource', os.R_OK):
            print('==> Using existing. Lucky boy.')
        else:
            print('==> Calling glib-compile-resources')
            try:
                subprocess.call([
                    'glib-compile-resources',
                    'shredder/resources/shredder.gresource.xml',
                    '--sourcedir',
                    'shredder/resources'
                ])
            except subprocess.CalledProcessError as err:
                print('==> Failed :(')

        # Run the usual distutils install routine:
        install.run(self)

        # Make sure the schema file is updated.
        # Otherwise the gui will trace trap.
        print('==> Compiling GLib Schema files')

        try:
            schema_dir = os.path.join(PREFIX, 'share/glib-2.0/schemas')
            subprocess.call([
                'glib-compile-schemas',
                schema_dir,
                "--targetdir",
                schema_dir,
            ])
        except subprocess.CalledProcessError as err:
            print('==> Could not update schemas: ', err)
            print('==> Please run the following manually:\n')
            print('    sudo glib-compile-schemas {prefix}'.format(
                prefix=os.path.join(PREFIX, 'share/glib-2.0/schemas')
            ))
        else:
            print('==> OK!')


setup(
    name='Shredder',
    version=read_version(),
    description='A gui frontend to rmlint',
    long_description='A graphical user interface to rmlint using GTK+',
    author='Christopher Pahl',
    author_email='sahib@online.de',
    maintainer='Cebtenzzre',
    maintainer_email='cebtenzzre@gmail.com',
    url='https://rmlint.rtfd.org',
    license='GPLv3',
    platforms='any',
    cmdclass={'install': PrePlusPostInstall},
    packages=['shredder', 'shredder.views'],
    package_data={'': [
        'resources/*.gresource'
    ]},
    data_files=[
        (
            os.path.join(PREFIX, 'share/icons/hicolor/scalable/apps'),
            ['shredder/resources/shredder.svg']
        ),
        (
            os.path.join(PREFIX, 'share/glib-2.0/schemas'),
            ['shredder/resources/org.gnome.Shredder.gschema.xml']
        ),
        (
            os.path.join(PREFIX, 'share/applications'),
            ['shredder.desktop']
        ),
    ]
)
