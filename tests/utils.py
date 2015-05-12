#############
# UTILITIES #
#############

import os
import json
import time
import pprint
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


def has_feature(feature):
    return ('+' + feature) in subprocess.check_output(
        ['./rmlint', '--version'], stderr=subprocess.STDOUT
    ).decode('utf-8')


def run_rmlint_once(*args, dir_suffix=None, use_default_dir=True, outputs=None):
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
    # print(' '.join(cmd))

    if os.environ.get('PRINT_CMD'):
        print('Run:', ' '.join(cmd))

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


def compare_json_doc(doc_a, doc_b, compare_checksum=False):
    keys = [
        'disk_id', 'inode', 'mtime', 'path', 'size', 'type'
    ]

    if compare_checksum and 'checkum' in doc_a and 'checksum' in doc_b:
        keys.append('checksum')

    for key in keys:
        # It's okay for unfinished checksums to have some missing fields.
        if doc_a['type'] == doc_b['type'] == 'unfinished_cksum':
            continue

        if doc_a[key] != doc_b[key]:
            print('  !! Key differs: ', key, doc_a[key], '!=', doc_b[key])
            return False

    return True


def compare_json_docs(docs_a, docs_b, compare_checksum=False):
    paths_a, paths_b = {}, {}

    for doc_a in docs_a[1:-1]:
        paths_a[doc_a['path']] = doc_a

    for doc_b in docs_b[1:-1]:
        paths_b[doc_b['path']] = doc_b

    for path_a, doc_a in paths_a.items():
        # if path_a not in paths_b:
        #     print('####', doc_a, path_a, '\n', docs_b, '\n\n', list(paths_b))
        doc_b = paths_b[path_a]
        if not compare_json_doc(doc_a, doc_b, compare_checksum):
            print('!! OLD:')
            pprint.pprint(doc_a)
            print('!! NEW:')
            pprint.pprint(doc_b)
            print('------- DIFF --------')
            return False

    if docs_a[-1] != docs_b[-1]:
        print('!! FOOTER DIFFERS', docs_a[-1], docs_b[-1])
        return False

    return True


def run_rmlint_pedantic(*args, **kwargs):
    options = [
        '--with-fiemap',
        '--without-fiemap',
        '--fake-pathindex-as-disk',
        '--fake-fiemap',
        '-P',
        '-PP',
        '--max-paranoid-mem 1M --algorithm=paranoid',
        '--buffered-read',
        '--threads=1',
        '--shred-never-wait',
        '--shred-always-wait',
        '--with-metadata-cache'
    ]

    cksum_types = [
        'paranoid', 'sha1', 'sha256', 'spooky', 'bastard', 'city',
        'md5', 'city256', 'city512', 'murmur', 'murmur256', 'murmur512',
        'spooky32', 'spooky64'
    ]

    # Note: sha512 is supported on all system which have
    #       no recent enough glib with. God forsaken debian people.
    if has_feature('sha512'):
        cksum_types.append('sha512')

    for cksum_type in cksum_types:
        options.append('--algorithm=' + cksum_type)

    data = None

    output_len = len(kwargs['outputs']) if 'outputs' in kwargs else 0

    for option in options:
        new_data = run_rmlint_once(*(args + (option, )), **kwargs)

        data_skip, new_data_skip = data, new_data
        if output_len is not 0:
            if new_data:
                new_data_skip = new_data[:-output_len]

            if data:
                data_skip = data[:-output_len]

        # We cannot compare checksum in all cases.
        compare_checksum = not option.startswith('--algorithm=')
        for arg in args:
            if '--cache' in args or '-C' in arg:
                compare_checksum = False

        if data_skip is not None and not compare_json_docs(data_skip, new_data_skip, compare_checksum):
            pprint.pprint(data_skip)
            pprint.pprint(new_data_skip)
            raise AssertionError("Optimisation too optimized: " + option)

        data = new_data

    return data


def run_rmlint(*args, **kwargs):
    if os.environ.get('PEDANTIC'):
        return run_rmlint_pedantic(*args, **kwargs)
    else:
        return run_rmlint_once(*args, **kwargs)


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

    return full_path


def warp_file_to_future(name, seconds):
    now = time.time()
    os.utime(os.path.join(TESTDIR_NAME, name), (now + 2, now + 2))


def usual_setup_func():
    create_testdir()


def usual_teardown_func():
    shutil.rmtree(TESTDIR_NAME)
