#!/usr/bin/env python3
# encoding: utf-8

""" This file is part of rmlint.

rmlint is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

rmlint is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with rmlint.  If not, see <http://www.gnu.org/licenses/>.

Authors:

- Christopher <sahib> Pahl 2010-2017 (https://github.com/sahib)
- Daniel <SeeSpotRun> T.   2014-2017 (https://github.com/SeeSpotRun)
"""

# This is the python remover utility shipped inside the rmlint binary.
# The 200 lines source presented below is meant to be clean and hackable.
# It is intended to be used for corner cases where the built-in sh formatter
# is not enough or as an alternative to it. By default it works the same.
#
# Disable a few pylint warnings, in case someone integrates into scripts:
# pylint: disable=unused-argument,missing-docstring,invalid-name
# pylint: disable=redefined-outer-name,unused-variable

# Python2 compat:
from __future__ import print_function

import os
import sys
import pwd
import json
import shutil
import filecmp
import argparse
import subprocess

CURRENT_UID = os.geteuid()
CURRENT_GID = pwd.getpwuid(CURRENT_UID).pw_gid

USE_COLOR = sys.stdout.isatty() and sys.stderr.isatty()
COLORS = {
    'red':    "\x1b[0;31m" if USE_COLOR else "",
    'blue':   "\x1b[1;34m" if USE_COLOR else "",
    'green':  "\x1b[0;32m" if USE_COLOR else "",
    'yellow': "\x1b[0;33m" if USE_COLOR else "",
    'reset':  "\x1b[0m" if USE_COLOR else "",
}


def original_check(path, original, be_paranoid=True):
    try:
        stat_p, stat_o = os.stat(path), os.stat(original)
        if (stat_p.st_dev, stat_p.st_ino) == (stat_o.st_dev, stat_o.st_ino):
            print('{c[red]}Same inode; ignoring:{c[reset]} {o} <=> {p}'.format(
                c=COLORS, o=original, p=path))
            return False

        if stat_p.st_size != stat_o.st_size:
            print('{c[red]}Size differs; ignoring:{c[reset]} '
                  '{o} <=> {p}'.format(c=COLORS, o=original, p=path))
            return False

        if be_paranoid and not filecmp.cmp(path, original):
            print('{c[red]}Content differs; ignoring:{c[reset]} '
                  '{o} <=> {p}'.format(c=COLORS, o=original, p=path))
            return False

        return True
    except OSError as exc:
        print('{c[red]}{exc}{c[reset]}'.format(c=COLORS, exc=exc))
        return False


def handle_duplicate_dir(path, original, **kwargs):
    if not args.dry_run:
        shutil.rmtree(path)


def handle_duplicate_file(path, original, args, **kwargs):
    if original_check(path, original['path'], be_paranoid=args.paranoid):
        if not args.dry_run:
            os.remove(path)


def handle_unfinished_cksum(path, **kwargs):
    pass  # doesn't need any handling.


def handle_empty_dir(path, **kwargs):
    if not args.dry_run:
        os.rmdir(path)


def handle_empty_file(path, **kwargs):
    if not args.dry_run:
        os.remove(path)


def handle_nonstripped(path, **kwargs):
    if not args.dry_run:
        subprocess.call(["strip", "--strip-debug", path])


def handle_badlink(path, **kwargs):
    if not args.dry_run:
        os.remove(path)


def handle_baduid(path, **kwargs):
    if not args.dry_run:
        os.chown(path, args.user, -1)


def handle_badgid(path, **kwargs):
    if not args.dry_run:
        os.chown(path, -1, args.group)


def handle_badugid(path, **kwargs):
    if not args.dry_run:
        os.chown(path, args.user, args.group)


OPERATIONS = {
    "duplicate_dir": handle_duplicate_dir,
    "duplicate_file": handle_duplicate_file,
    "unfinished_cksum": handle_unfinished_cksum,
    "emptydir": handle_empty_dir,
    "emptyfile": handle_empty_file,
    "nonstripped": handle_nonstripped,
    "badlink": handle_badlink,
    "baduid": handle_baduid,
    "badgid": handle_badgid,
    "badugid": handle_badugid,
}


def exec_operation(item, original=None, args=None):
    try:
        OPERATIONS[item['type']](
            item['path'], original=original, item=item, args=args)
    except OSError as err:
        print('{c[red]}# {err}{c[reset]}'.format(
            err=err, c=COLORS
        ), file=sys.stderr)


MESSAGES = {
    'duplicate_dir':    '{c[yellow]}Deleting duplicate directory:',
    'duplicate_file':   '{c[yellow]}Deleting duplicate:',
    'unfinished_cksum': 'checking',
    'emptydir':         '{c[green]}Deleting empty directory:',
    'emptyfile':        '{c[green]}Deleting empty file:',
    'nonstripped':      '{c[green]}Stripping debug symbols:',
    'badlink':          '{c[green]}Deleting bad symlink:',
    'baduid':           '{c[green]}chown {u}',
    'badgid':           '{c[green]}chgrp {g}',
    'badugid':          '{c[green]}chown {u}:{g}',
}

ORIGINAL_MESSAGES = {
    'duplicate_file':   '{c[green]}Keeping original:  ',
    'duplicate_dir':    '{c[green]}Keeping original directory:  ',
}


def main(args, data):
    last_original_item = None

    # Process header and footer, if present
    header, footer = [], []
    if data[0].get('description'):
        header = data.pop(0)
    if data[-1].get('total_files'):
        footer = data.pop(-1)

    if not args.no_ask and not args.dry_run:
        print('rmlint was executed in the following way:\n',
              header.get('args'),
              '\n\nPress Enter to continue and perform modifications, '
              'or CTRL-C to exit.'
              '\nExecute this script with -d to disable this message.',
              file=sys.stderr)
        sys.stdin.read(1)

    for item in data:
        progress_prefix = '{c[blue]}[{p:3}%]{c[reset]} '.format(
            c=COLORS, p=item['progress'])

        if item['is_original']:
            msg = ORIGINAL_MESSAGES[item['type']].format(c=COLORS)
            print('{prog}{v}{c[reset]} {path}'.format(
                c=COLORS, prog=progress_prefix, v=msg, path=item['path']))
            last_original_item = item
            # Do not handle originals.
            continue

        msg = MESSAGES[item['type']].format(
            c=COLORS, u=args.user, g=args.group)
        print('{prog}{v}{c[reset]} {path}'.format(
            c=COLORS, prog=progress_prefix, v=msg, path=item['path']))
        exec_operation(item, original=last_original_item, args=args)

    print('{c[blue]}[100%] Done!{c[reset]}'.format(c=COLORS))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='Handle the files in a JSON output of rmlint.'
    )

    parser.add_argument(
        'json_files', metavar='json_file', nargs='*', default=['.rmlint.json'],
        help='A JSON output of rmlint to handle (can be given multiple times)'
    )
    parser.add_argument(
        '-n', '--dry-run', action='store_true',
        help='Do not perform any modifications, just print what would be '
             'done. (implies -d)'
    )
    parser.add_argument(
        '-d', '--no-ask', action='store_true', default=False,
        help='Do not ask for confirmation before running.'
    )
    parser.add_argument(
        '-p', '--paranoid', action='store_true', default=False,
        help='Recheck that files are still identical before removing '
             'duplicates.'
    )
    parser.add_argument(
        '-u', '--user', type=int, default=CURRENT_UID,
        help='Numerical uid for chown operations'
    )
    parser.add_argument(
        '-g', '--group', type=int, default=CURRENT_GID,
        help='Numerical gid for chgrp operations'
    )

    args = parser.parse_args()
    json_docs = []
    for json_file in args.json_files:
        try:
            with open(json_file) as f:
                j = json.load(f)
            json_docs.append(j)
        except IOError as err:      # Cannot open file
            print(err, file=sys.stderr)
            sys.exit(-1)
        except ValueError as err:   # File is not valid JSON
            print('{}: {}'.format(err, json_file), file=sys.stderr)
            sys.exit(-1)

    try:
        if args.dry_run:
            print(
                '{c[green]}#{c[reset]} '
                'This is a dry run. Nothing will be modified.'.format(
                    c=COLORS
                )
            )

        for json_doc in json_docs:
            main(args, json_doc)

        if args.dry_run:
            print(
                '{c[green]}#{c[reset]} '
                'This was a dry run. Nothing was modified.'.format(
                    c=COLORS
                )
            )
    except KeyboardInterrupt:
        print('\ncanceled.')

