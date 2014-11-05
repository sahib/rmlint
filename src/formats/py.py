#!/usr/bin/env python3
# encoding: utf-8

import os
import sys
import pwd
import json
import shutil
import argparse
import subprocess


def handle_duplicate_dir(path):
    shutil.rmtree(path)


def handle_duplicate_file(path):
    os.remove(path)


def handle_empty_dir(path):
    os.rmdir(path)


def handle_empy_file(path):
    os.remove(path)


def handle_nonstripped(path):
    subprocess.call(["strip", "--strip-debug", path])


def handle_badlink(path):
    os.remove(path)


CURRENT_UID = os.geteuid()
CURRENT_GID = pwd.getpwuid(CURRENT_UID).pw_gid


def handle_baduid(path):
    os.chmod(path, CURRENT_UID, -1)


def handle_badgid(path):
    os.chmod(path, -1, CURRENT_GID)


def handle_badugid(path):
    os.chmod(path, CURRENT_UID, CURRENT_GID)


OPERATIONS = {
    "duplicate_dir": handle_duplicate_dir,
    "duplicate_file": handle_duplicate_file,
    "emptydir": handle_empty_dir,
    "emptyfile": handle_empy_file,
    "nonstripped": handle_nonstripped,
    "badlink": handle_badlink,
    "baduid": handle_baduid,
    "badgid": handle_badgid,
    "badugid": handle_badugid,
}


def exec_operation(item):
    try:
        OPERATIONS[item['type']](item['path'])
    except OSError as err:
        print('That did not work: ', err)


def main(args, header, data, footer):
    seen_cksums = set()
    for item in data:
        if item['type'].startswith('duplicate_') and item['is_original']:
            print("\nDeleting twins off " + item['path'])
            continue

        if not args.dry_run:
            exec_operation(item)

        print('Handling ({t}): {p}'.format(t=item['type'], p=item['path']))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Process some integers.')
    parser.add_argument(
        'json_docs', metavar='json_doc', type=open, nargs='*',
        help='A json output of rmlint to handle'
    )
    parser.add_argument(
        '-n', '--dry-run', action='store_true',
        help='Only print what would be done.'
    )

    try:
        args = parser.parse_args()
    except FileNotFoundError as err:
        print(err)
        sys.exit(-1)

    if not args.json_docs:
        # None given on the commandline
        try:
            args.json_docs.append(open('rmlint.json', 'r'))
        except FileNotFoundError as err:
            print('Cannot load default json document: ', str(err))
            sys.exit(-2)


    json_docus = [json.load(doc) for doc in args.json_docs]
    json_elems = [item for sublist in json_docus for item in sublist]

    for json_doc in json_docus:
        head, *data, footer = json_doc
        main(args, head, data, footer)
