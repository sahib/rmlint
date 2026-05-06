#!/usr/bin/env python
# encoding: utf-8

"""Commandline parsing facility for Shredder.

Produces an option dict that can be used by shredder.application.
Some options are processed immediately however.
"""

# Stdlib:
import os
import sys
import logging
import argparse

# External:
from gi.repository import Gio


def show_version():
    """Print the version as shown by `rmlint --version`"""
    proc = Gio.Subprocess.new(
        ['rmlint', '--version'],
        Gio.SubprocessFlags.STDERR_PIPE
    )
    *_, data = proc.communicate_utf8()

    # Shredder version is always the same as rmlint.
    # So, let's just replace `rmlint` with `Shredder` :-)
    print(data.replace('rmlint', 'Shredder', 1), end='')
    sys.exit(-1)


def adjust_loglevel(root_logger, count):
    """Convert a -v count to a python loglevel."""
    count = max(0, min(4, count))

    root_logger.setLevel({
        0: logging.CRITICAL,
        1: logging.ERROR,
        2: logging.WARNING,
        3: logging.INFO,
        4: logging.DEBUG,
    }[count])


def parse_arguments(root_logger):
    """Parse the cmdline options."""
    sys.argv[0] = 'shredder'
    parser = argparse.ArgumentParser(
        prog='shredder',
        usage='%(prog)s [options] PATHS ...',
        description="A gui frontend to rmlint.",
    )
    parser.add_argument(
        "--add-location", "-a", action="append", dest="locations",
        help="Add locations to locations view."
    )
    parser.add_argument(
        "--scan", "-s", action="append", dest="untagged",
        help="Add location to scan (as untagged path)."
    )
    parser.add_argument(
        "--scan-tagged", "-S", action="append", dest="tagged",
        help="Add location to scan (as tagged path)."
    )
    parser.add_argument(
        "--load-script", "-l", action="store", dest="script",
        help="Show `script` in editor view."
    )
    parser.add_argument(
        "--verbose", "-v", action="count", default=0,
        dest='more_verbosity', help="Be more verbose."
    )
    parser.add_argument(
        "--less-verbose", "-V", action="count", default=0,
        dest='less_verbosity', help="Be less verbose."
    )
    parser.add_argument(
        "--show-settings", "-c", action="store_true",
        dest='show_settings', help="Show the settings view."
    )
    parser.add_argument(
        "--version", action="store_true", dest="show_version",
        help="Show the version of Shredder."
    )
    parser.add_argument("paths", nargs="*")

    vals = parser.parse_args()
    if vals.show_version:
        show_version()

    if vals.paths:
        vals.locations = (vals.locations or []) + vals.paths

    adjust_loglevel(
        root_logger,
        vals.more_verbosity +
        -vals.less_verbosity +
        4  # Default loglevel: debug.
    )

    # Check paths to be valid:
    paths = (vals.tagged or []) + (vals.untagged or []) + [vals.script]
    for path in (path for path in paths if path):
        if not os.path.exists(path):
            root_logger.error('`%s` does not exist.', path)
            sys.exit(-1)

    return vals


if __name__ == '__main__':
    LOGGER = logging.getLogger('test-cmdline')
    print(parse_arguments(LOGGER))
