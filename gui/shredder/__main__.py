#!/usr/bin/env python
# encoding: utf-8

# Stdlib:
import sys

# Internal:
from shredder.application import Application
from shredder.logger import create_logger

ROOT_LOGGER = create_logger(None)

# Gtk will take over now.
app = Application()
ROOT_LOGGER.info('Starting up')
sys.exit(app.run(sys.argv))
