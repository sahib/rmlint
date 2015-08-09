#!/usr/bin/env python
# encoding: utf-8

"""Init triggering goes here.

This code will be executed first when doing:

    $ python -m shredder
"""

# Stdlib:
import sys

# Internal:
from shredder.application import Application
from shredder.logger import create_logger

ROOT_LOGGER = create_logger(None)

# Gtk will take over now.
APP = Application()
ROOT_LOGGER.info('Starting up')
sys.exit(APP.run(sys.argv))
