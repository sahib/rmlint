#!/usr/bin/env python

from distutils.core import setup
from distutils.command.install import install

import os
import subprocess


class pre_and_post_install(install):
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
            subprocess.call([
                'glib-compile-schemas',
                '/usr/share/glib-2.0/schemas'
            ])
        except subprocess.CalledProcessError as err:
            print('==> Could not update schemas: ', err)
            print('==> Please run the following manually:\n')
            print('    sudo glib-compile-schemas /usr/share/glib-2.0/schemas')
        else:
            print('==> OK!')


setup(
    name='Shredder',
    version='2.3.0',
    description='A gui frontend to rmlint',
    author='Christopher Pahl',
    author_email='sahib@online.de',
    url='https://rmlint.rtfd.org',
    cmdclass={'install': pre_and_post_install},
    packages=['shredder', 'shredder.views'],
    package_data={'': [
        'resources/*.svg',
        'resources/*.css',
        'resources/*.gresource'
    ]},
    data_files=[
        (
            '/usr/share/glib-2.0/schemas',
            ['shredder/resources/org.gnome.Shredder.gschema.xml']
        ),
        (
            '/usr/share/applications',
            ['shredder.desktop']
        ),
    ]
)
