#!/usr/bin/env python
# encoding: utf-8

"""Common constants."""

import gi

gi.require_version('Gtk', '3.0')
gi.require_version('Rsvg', '2.0')
gi.require_version('PangoCairo', '1.0')
gi.require_version('Polkit', '1.0')
gi.require_version('GtkSource', '3.0')

# Name of your application:
APP_TITLE = 'Shredder'

# One sentence description of the application:
APP_DESCRIPTION = 'Find & clean duplicate files'

# Use boxy old menus or new popovers?
APP_USE_TRADITIONAL_MENU = False
