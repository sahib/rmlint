#!/usr/bin/env python

from distutils.core import setup


setup(
    name='Shredder',
    version='2.3.0',
    description='A gui frontend to rmlint',
    author='Christopher Pahl',
    author_email='sahib@online.de',
    url='https://rmlint.rtfd.org',
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
