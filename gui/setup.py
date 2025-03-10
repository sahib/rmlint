#!/usr/bin/env python3

from setuptools import setup
from setuptools.command.install import install
from distutils.command.install_data import install_data

import os
import logging
import subprocess

GRESOURCE_DIR = 'shredder/resources'
GRESOURCE_FILE = 'shredder.gresource.xml'
GSCHEMA_DIR_SUFFIX = 'share/glib-2.0/schemas'


def read_version():
    vfp = os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir, '.version')
    with open(vfp, 'r') as handle:
        version_string = handle.read()

    version_numbers, _ = version_string.split(' ', 1)
    return version_numbers


class install_glib_resources(install):
    def run(self):
        self._build_gresources()
        super().run()

    def _build_gresources(self):
        '''
        Compile the resource bundle
        '''
        logging.info('==> Calling glib-compile-resources')
        try:
            subprocess.call([
                'glib-compile-resources',
                '--sourcedir={}'.format(GRESOURCE_DIR),
                os.path.join(GRESOURCE_DIR, GRESOURCE_FILE)
            ])
        except subprocess.CalledProcessError as err:
            logging.error('==> Failed :(')


class compile_glib_schemas(install_data):
    def run(self):
        super().run()
        if os.environ.get("COMPILE_GLIB_SCHEMA", False):
            self._build_gschemas()
        else:
            self.print_compile_instructions()

    def gschema_dir(self):
        return os.path.join(self.install_dir, GSCHEMA_DIR_SUFFIX)

    def print_compile_instructions(self):
        logging.info('==> You may need to compile glib schemas manually:\n')
        logging.info('    sudo glib-compile-schemas {}\n'.format(
            self.gschema_dir()))

    def _build_gschemas(self):
        '''
        Make sure the schema file is updated after installation,
        otherwise the gui will trace trap.
        '''
        logging.info('==> Compiling GLib Schema files')
        try:
            subprocess.call(['glib-compile-schemas', self.gschema_dir()])
            logging.info('==> OK!')
        except subprocess.CalledProcessError as err:
            logging.error('==> Could not update schemas: ', err)
            self.print_compile_instructions()


setup(
    name='Shredder',
    version=read_version(),
    description='A gui frontend to rmlint',
    long_description='A graphical user interface to rmlint using GTK+',
    author='Christopher Pahl',
    author_email='sahib@online.de',
    url='https://rmlint.rtfd.org',
    platforms='any',
    cmdclass={
        'install': install_glib_resources,
        'install_data': compile_glib_schemas
    },
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
