#!/usr/bin/env python
# encoding: utf-8

"""Init triggering goes here.

This code will be executed first when doing:

    $ python -m shredder
"""

# Stdlib:
import sys

# Internal:
from shredder.cmdline import parse_arguments
from shredder.logger import create_logger

ROOT_LOGGER = create_logger(None)
OPTIONS = parse_arguments(ROOT_LOGGER)


# Later import due to logging.
from shredder.application import Application


if OPTIONS:
    # Gtk will take over now.
    APP = Application(OPTIONS)
    ROOT_LOGGER.info('Starting up.')
    sys.exit(APP.run([sys.argv[0]]))
