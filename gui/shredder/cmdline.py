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

# External:
from gi.repository import Gio
from gi.repository import GLib


def show_version():
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
    count = max(0, min(4, count))

    root_logger.setLevel({
        0: logging.CRITICAL,
        1: logging.ERROR,
        2: logging.WARNING,
        3: logging.INFO,
        4: logging.DEBUG,
    }[count])


def parse_arguments(root_logger):
    sys.argv[0] = 'shredder'
    parser = GLib.option.OptionParser(
        "PATHS ...",
        description="A gui frontend to rmlint.",
        option_list=[
            GLib.option.make_option(
                "--add-location", "-a",
                type="filename",
                action="append",
                dest="locations",
                help="Add locations to locations view."
            ),
            GLib.option.make_option(
                "--scan", "-s",
                type="filename",
                action="append",
                dest="untagged",
                help="Add location to scan (as untagged path)."
            ),
            GLib.option.make_option(
                "--scan-tagged", "-S",
                type="filename",
                action="append",
                dest="tagged",
                help="Add location to scan (as tagged path)."
            ),
            GLib.option.make_option(
                "--load-script", "-l",
                type="filename",
                action="store",
                dest="script",
                help="Show `script` in editor view."
            ),
            GLib.option.make_option(
                "--verbose", "-v",
                action="count",
                dest='more_verbosity',
                help="Be more verbose."
            ),
            GLib.option.make_option(
                "--less-verbose", "-V",
                action="count",
                dest='less_verbosity',
                help="Be less verbose."
            ),
            GLib.option.make_option(
                "--show-settings", "-c",
                action="store_true",
                dest='show_settings',
                help="Show the settings view."
            ),
            GLib.option.make_option(
                "--version", "",
                action="store_true",
                dest="show_version",
                help="Show the version of Shredder."
            ),
        ]
    )

    try:
        parser.parse_args()
    except GLib.Error as err:
        root_logger.error(str(err))
        return None

    vals = parser.values
    if parser.values.show_version:
        show_version()

    adjust_loglevel(
        root_logger,
        (vals.more_verbosity or 0) +
        (vals.less_verbosity or 0) +
        4  # Default loglevel: info.
    )

    # Check paths to be valid:
    paths = (vals.tagged or []) + (vals.untagged or []) + [vals.script]
    for path in (path for path in paths if path):
        if not os.path.exists(path):
            root_logger.error('`%s` does not exist.', path)
            sys.exit(-1)

    return vals


if __name__ == '__main__':
    print(parse_arguments(LOGGER))
