#!/usr/bin/env python
# encoding: utf-8

"""Commandline parsing facility for Shredder.

Produces an option dict that can be used by shredder.application.
Some options are processed immediately however.
"""

# Stdlib:
import argparse
import os
import sys
import logging

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
    """Parse the cmdline options using argparse."""
    sys.argv[0] = 'shredder'
    parser = argparse.ArgumentParser(
        prog='shredder',
        description="A gui frontend to rmlint.",
    )
    parser.add_argument(
        "-a", "--add-location",
        action="append",
        dest="locations",
        metavar="PATH",
        help="Add locations to locations view."
    )
    parser.add_argument(
        "-s", "--scan",
        action="append",
        dest="untagged",
        metavar="PATH",
        help="Add location to scan (as untagged path)."
    )
    parser.add_argument(
        "-S", "--scan-tagged",
        action="append",
        dest="tagged",
        metavar="PATH",
        help="Add location to scan (as tagged path)."
    )
    parser.add_argument(
        "-l", "--load-script",
        dest="script",
        help="Show `script` in editor view."
    )
    parser.add_argument(
        "-v", "--verbose",
        action="count",
        dest='more_verbosity',
        help="Be more verbose."
    )
    parser.add_argument(
        "-V", "--less-verbose",
        action="count",
        dest='less_verbosity',
        help="Be less verbose."
    )
    parser.add_argument(
        "-c", "--show-settings",
        action="store_true",
        dest='show_settings',
        help="Show the settings view."
    )
    parser.add_argument(
        "--version",
        action="store_true",
        dest="show_version",
        help="Show the version of Shredder."
    )
    parser.add_argument(
        "paths",
        nargs="*",
        metavar="PATH",
        help=argparse.SUPPRESS
    )

    vals = parser.parse_args()
    if vals.show_version:
        show_version()

    adjust_loglevel(
        root_logger,
        (vals.more_verbosity or 0) -
        (vals.less_verbosity or 0) +
        4  # Default loglevel: info.
    )

    for path in vals.paths:
        root_logger.warning(
            'argument %s ignored. Will be rejected in a future version.',
            path
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
