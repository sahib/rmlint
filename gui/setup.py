#!/usr/bin/env python3
# encoding: utf-8
#!/usr/bin/env python

from distutils.core import setup
from distutils.command.install_data import install_data

import os
import sys
import subprocess

GRESOURCE_DIR = 'shredder/resources'
GRESOURCE_FILE = 'shredder.gresource.xml'
GSCHEMA_DIR_SUFFIX = 'share/glib-2.0/schemas'

def read_version():
    with open('../.version', 'r') as handle:
        version_string = handle.read()

    return version_string.strip()

class install_glib_resources(install_data):
    def run(self):
        self._build_gresources()
        super().run()
        self._build_gschemas()

    def _build_gresources(self):
        '''
        Compile the resource bundle
        '''
        print('==> Calling glib-compile-resources')
        try:
            subprocess.call([
                'glib-compile-resources',
                '--sourcedir={}'.format(GRESOURCE_DIR),
                os.path.join(GRESOURCE_DIR, GRESOURCE_FILE)
            ])
        except subprocess.CalledProcessError as err:
            print('==> Failed :(')

    def _build_gschemas(self):
        '''
        Make sure the schema file is updated after installation,
        otherwise the gui will trace trap.
        '''
        print('==> Compiling GLib Schema files')
        # Use 'self.install_dir' to build the path, so that it works
        # for both global and local '--user' installs.
        gschema_dir = os.path.join(self.install_dir, GSCHEMA_DIR_SUFFIX)
        compile_command = [
                'glib-compile-schemas',
                gschema_dir]
        try:
            subprocess.call(compile_command)
        except subprocess.CalledProcessError as err:
            print('==> Could not update schemas: ', err)
            print('==> Please run the following manually:\n')
            print('    sudo {}'.format(' '.join(compile_command)))
        else:
            print('==> OK!')


setup(
    name='Shredder',
    version=read_version(),
    description='A gui frontend to rmlint',
    long_description='A graphical user interface to rmlint using GTK+',
    author='Christopher Pahl',
    author_email='sahib@online.de',
    url='https://rmlint.rtfd.org',
    license='GPLv3',
    platforms='any',
    cmdclass={'install_data': install_glib_resources},
    packages=['shredder', 'shredder.views'],
    package_data={'': [
        'resources/*.gresource'
    ]},
    data_files=[
        (
            'share/icons/hicolor/scalable/apps',
            ['shredder/resources/shredder.svg']
        ),(
            'share/glib-2.0/schemas',
            ['shredder/resources/org.gnome.Shredder.gschema.xml']
        ),(
            'share/applications',
            ['shredder.desktop']
        ),
    ],
    scripts=['bin/shredder'],
)
