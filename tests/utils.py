#############
# UTILITIES #
#############

import subprocess
import shutil
import json
import os


TESTDIR_NAME = '/tmp/rmlint-unit-testdir'


def create_testdir():
    try:
        os.makedirs(TESTDIR_NAME)
    except FileExistsError:
        pass


def run_rmlint(*args):
    cmd = ' '.join(['./rmlint', TESTDIR_NAME, '-o json:stdout'] + list(args))
    output = subprocess.check_output(cmd, shell=True)
    json_data = output.decode('utf-8')
    return json.loads(json_data)


def create_file(data, name):
    full_path = os.path.join(TESTDIR_NAME, name)
    if '/' in name:
        try:
            os.makedirs(os.path.dirname(full_path))
        except FileExistsError:
            pass

    with open(full_path, 'w') as handle:
        handle.write(data)
