#############
# UTILITIES #
#############

import os
import json
import shutil
import shlex
import subprocess

USE_VALGRIND = True
TESTDIR_NAME = '/tmp/rmlint-unit-testdir'

def runs_as_root():
    return os.geteuid() is 0


def create_testdir():
    try:
        os.makedirs(TESTDIR_NAME)
    except OSError:
        pass


def which(program):
    def is_exe(fpath):
        return os.path.isfile(fpath) and os.access(fpath, os.X_OK)

    fpath, fname = os.path.split(program)
    if fpath and is_exe(program):
        return program
    else:
        for path in os.environ["PATH"].split(os.pathsep):
            path = path.strip('"')
            exe_file = os.path.join(path, program)
            if is_exe(exe_file):
                return exe_file

    return None


def run_rmlint(*args, dir_suffix=None, use_default_dir=True, outputs=None):
    if use_default_dir:
        if dir_suffix:
            target_dir = os.path.join(TESTDIR_NAME, dir_suffix)
        else:
            target_dir = TESTDIR_NAME
    else:
        target_dir = ""

    if os.environ.get('USE_VALGRIND'):
        env = {
            'G_DEBUG': 'gc-friendly',
            'G_SLICE': 'always-malloc'
        }
        cmd = [which('valgrind'), '--show-possibly-lost=no', '-q']
    else:
        env, cmd = {}, []

    cmd += ['./rmlint', '-V', target_dir, '-o', 'json:stdout'] + shlex.split(' '.join(args))

    for output in outputs or []:
        cmd.append('-o')
        cmd.append('{f}:{p}'.format(
            f=output, p=os.path.join(TESTDIR_NAME, '.' + output))
        )

    # filter empty strings
    cmd = list(filter(None, cmd))

    output = subprocess.check_output(cmd, shell=False, env=env)
    json_data = json.loads(output.decode('utf-8'))

    read_outputs = []
    for output in outputs or []:
        with open(os.path.join(TESTDIR_NAME, '.' + output), 'r') as handle:
            read_outputs.append(handle.read())

    if outputs is None:
        return json_data
    else:
        return json_data + read_outputs


def create_dirs(path):
    os.makedirs(os.path.join(TESTDIR_NAME, path))


def create_link(path, target, symlink=False):
    f = os.symlink if symlink else os.link
    f(
        os.path.join(TESTDIR_NAME, path),
        os.path.join(TESTDIR_NAME, target)
    )


def create_file(data, name):
    full_path = os.path.join(TESTDIR_NAME, name)
    if '/' in name:
        try:
            os.makedirs(os.path.dirname(full_path))
        except OSError:
            pass

    with open(full_path, 'w') as handle:
        handle.write(data)


def usual_setup_func():
    create_testdir()


def usual_teardown_func():
    shutil.rmtree(TESTDIR_NAME)
