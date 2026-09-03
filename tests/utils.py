"""Utilities"""
import contextlib
import json
import logging
import os
import pprint
import re
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from collections.abc import Iterable
from functools import cache
from typing import Final

import psutil
import pytest
import xattr

# TESTDIR_BASE holds every test directory. It is not created automatically.
# Note that some systems use a symbolic link for /tmp (e.g. macOS with /private/tmp),
# and some tests might fail if we use the unresolved version.
TESTDIR_BASE = os.path.realpath(os.getenv('RM_TS_DIR') or tempfile.gettempdir())

RMLINT_BINARY_DIR = os.getcwd()
RMLINT_BINARY = os.path.join(RMLINT_BINARY_DIR, 'rmlint')

# Directory of the currently running test, set from `tmp_path` by the autouse
# `rmlint_testdir` fixture in conftest.py.
_TESTDIR = None

# Known leaks
KNOWN_LEAK_OPTIONS: Final[frozenset[str]] = frozenset()
KNOWN_LEAK_SWITCHES: Final[frozenset[str]] = frozenset()
KNOWN_LEAK_LINT_TYPES: Final[tuple[str, ...]] = ()
LINT_TYPE_SWITCHES: Final[frozenset[str]] = frozenset({"-T", "--types"})

# XXX: metrocrc* used to be gated behind inexistent 'sse4' feature.
CKSUM_TYPES = [
    'murmur',
    'metro', 'metro256',
    # 'metrocrc', 'metrocrc256'
    'md5',
    'sha1',
    'sha256', 'sha512',
    'sha3-256', 'sha3-384', 'sha3-512',
    'blake2s', 'blake2b', 'blake2sp', 'blake2bp',
    'blake3', 'blake3_512',
    'xxhash',
    'highway64', 'highway128', 'highway256',
    # 'cumulative', 'ext',
    'paranoid',
]


def set_testdir(path):
    global _TESTDIR
    _TESTDIR = path


def get_testdir():
    """Directory of the currently running test."""
    if _TESTDIR is None:
        raise RuntimeError('get_testdir() is only available inside a test.')
    return _TESTDIR


@cache
def get_env_flag(name: str) -> bool:
    env_name = f'RM_TS_{name.upper()}'
    try:
        return bool(int(os.environ.get(env_name, 0)))
    except ValueError:
        logging.warning("%s should be an integer; assuming 0.", env_name)
        return False


@cache
def features() -> dict[str, bool]:
    version = subprocess.run(
        (RMLINT_BINARY, '--version'),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        check=True,
        text=True,
    ).stderr

    match = re.search(r'^compiled with:\s*(.+)$', version, re.MULTILINE)
    if not match:
        raise RuntimeError(f"could not extract features from: \n{version}")

    result = {}
    for token in match.group(1).split():
        sign, name = token[0], token[1:]
        if sign not in "+-":
            raise RuntimeError(f"unexpected feature token {token!r}")
        result[name] = sign == '+'
    return result


def has_feature(feature: str) -> bool:
    try:
        return features()[feature]
    except KeyError:
        raise LookupError(
            f"{feature!r} is not a known rmlint feature "
            f"(known: {sorted(features())})"
        ) from None


@cache
def get_bin_path(name: str) -> str:
    if path := shutil.which(name):
        return path
    raise RuntimeError(f"{name!r} not found.")


@cache
def debuginfo_env() -> dict[str, str]:
    if dbg_info_path := shutil.which("debuginfod-find"):
        dbg_info_path = os.path.dirname(dbg_info_path)

    env = {
        **{key: os.environ[key] for key in [
            "DEBUGINFOD_URLS", "XDG_CACHE_HOME", "HOME"] if key in os.environ},
        **({"PATH": dbg_info_path} if dbg_info_path else {}),
    }
    return env


def runs_as_root():
    return os.geteuid() == 0


# XXX: unrelated to _TESTDIR !
def create_testdir(*extra_path):
    os.makedirs(os.path.join(get_testdir(), *extra_path), exist_ok=True)


def tokenise(args: Iterable[str]) -> tuple[str, ...]:
    """Flatten a mix of command strings and pre-split argv entries into tokens."""
    return tuple(token for arg in args for token in shlex.split(arg))


def has_known_leak(tokens: Iterable[str]) -> bool:
    """Return True if the command line enables an option known to leak."""
    for token in (it := iter(tokens)):
        if token in KNOWN_LEAK_OPTIONS:
            return True

        if token.startswith("-") and not token.startswith("--"):
            if any(switch in token for switch in KNOWN_LEAK_SWITCHES):
                return True

        name, sep, value = token.partition("=")
        if name in LINT_TYPE_SWITCHES:
            if not sep:  # value is the next token, e.g. `-T foo`
                value = next(it, "")
            if any(lint_type in value for lint_type in KNOWN_LEAK_LINT_TYPES):
                return True

    return False


def run_rmlint_once(*args,
                    dir_suffix=None,
                    use_default_dir=True,
                    outputs=(),
                    with_json=True,
                    directly_return_output=False,
                    use_shell=False,
                    uses_py_formatter=False,
                    verbosity="-V",
                    check=True,
                    timeout=None):
    if use_default_dir:
        target_dir = os.path.join(get_testdir(), dir_suffix) if dir_suffix else get_testdir()
    else:
        target_dir = ""

    args_tokens = tokenise(args)

    if get_env_flag('use_valgrind'):
        env = {
            **debuginfo_env(),
            'G_DEBUG': 'gc-friendly',
            'G_SLICE': 'always-malloc'
        }
        cmd = [get_bin_path('valgrind'), '--error-exitcode=1', '-q']
        if get_env_flag('check_leaks'):
            if has_known_leak(args_tokens):
                logging.warning("skipping known leak check for `%s`", shlex.join(args_tokens))
            else:
                cmd += ('--leak-check=full', '--show-leak-kinds=definite', '--errors-for-leak-kinds=definite')
    elif get_env_flag('use_gdb'):
        env, cmd = debuginfo_env(), [ get_bin_path('gdb'),
                                     '-batch', '--silent', '-ex=run', '-ex=thread apply all bt', '-ex=quit', '--args']
    else:
        env, cmd = {}, []

    cmd.append(RMLINT_BINARY)
    cmd.extend(arg for arg in (verbosity, target_dir) if arg)

    cmd.extend(args_tokens)

    # TODO: maybe use .rmlint.json if uses_py_formatter=True
    if with_json:
        cmd.extend(('-o', 'json:' + os.path.join(get_testdir(), 'out.json'), '-c', 'json:oneline'))

    output_files = [(output, os.path.join(get_testdir(), f".{output}-{idx}"))
                    for idx, output in enumerate(outputs)]

    for output, path in output_files:
        cmd.extend(('-o', f'{output}:{path}'))

    assert all(arg.strip() for arg in cmd), "empty argument in: " + repr(cmd)

    run_args = {
        'env': env,
        'cwd': get_testdir(),
        'stdout': subprocess.PIPE,
        'stderr': subprocess.PIPE,
        'shell': use_shell,
        'timeout': timeout,
    }

    if use_shell:
        run_args['executable'] = get_bin_path('bash')

    if uses_py_formatter:
        # The py formatter writes its JSON document to `.rmlint.json` in
        # rmlint's CWD and only once the traversal is over. If present from
        # the previousrun remove it so it does not get in the way on this run.
        with contextlib.suppress(FileNotFoundError):
            os.unlink(os.path.join(get_testdir(), '.rmlint.json'))

    cmd_str = ' '.join(cmd)

    logging.info("running%s from `%s`: %s",
                 ' in shell' if use_shell else '', get_testdir(), cmd_str)

    if get_env_flag('sleep'):
        print('Waiting for 1000 seconds.')
        time.sleep(1000)

    result = subprocess.run(cmd_str if use_shell else cmd, **run_args)
    sys.stdout.buffer.write(result.stderr)

    if get_env_flag('use_gdb'):
        sys.stdout.buffer.write(b"\n==> START OF GDB OUTPUT <==\n")
        sys.stdout.buffer.write(result.stdout)
        sys.stdout.buffer.write(b"==> END OF GDB OUTPUT <==\n")

    if check:
        result.check_returncode()

    if directly_return_output:
        return result.stdout if check else (result, result.stdout)

    if with_json:
        with open(os.path.join(get_testdir(), 'out.json'), encoding='utf8') as f:
            json_data = json.loads(f.read())
    else:
        json_data = []

    for _, path in output_files:
        with open(path, encoding='utf8') as handle:
            json_data.append(handle.read())

    return json_data if check else (result, json_data)


def compare_json_doc(doc_a, doc_b, compare_checksum=False):
    keys = [
        'disk_id', 'inode', 'mtime', 'path', 'size', 'type'
    ]

    if compare_checksum and 'checksum' in doc_a and 'checksum' in doc_b:
        keys.append('checksum')

    for key in keys:
        # It's okay for unfinished checksums to have some missing fields.
        if doc_a['type'] == doc_b['type'] == 'unique_file':
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
        '--buffered-read',
        '--threads=1',
        '--shred-never-wait',
        '--shred-always-wait',
        '--no-mount-table',
        '-P',
        '-PP',
        '-PPP',
        '--algorithm=paranoid --limit-mem 1M'
    ]

    # XXX: 'paranoid' is in CKSUM_TYPES
    for cksum_type in CKSUM_TYPES:
        options.append('--algorithm=' + cksum_type)

    data = None

    output_len = len(kwargs.get('outputs', []))

    for option in options:
        new_data = run_rmlint_once(*(args + (option, )), **kwargs)

        data_skip, new_data_skip = data, new_data

        if output_len != 0:
            if new_data:
                new_data_skip = new_data[:-output_len]

            if data:
                data_skip = data[:-output_len]

        # We cannot compare checksum in all cases.
        # XXX: algorithm options must be grouped at the end of the options list.
        # TODO: end-to-end tests of algorithms
        compare_checksum = not any((option.startswith('--algorithm='),
                                    option.startswith('-P'), option.startswith('-p')))

        if (data_skip and 'directly_return_output' not in kwargs
            and not compare_json_docs(data_skip, new_data_skip, compare_checksum)
            ):
            pprint.pprint(data_skip)
            pprint.pprint(new_data_skip)
            raise AssertionError("Optimisation too optimized: " + option)

        data = new_data

    return data


def run_rmlint(*args, force_no_pedantic=False, **kwargs):
    if get_env_flag('pedantic') and force_no_pedantic is False:
        return run_rmlint_pedantic(*args, **kwargs)

    return run_rmlint_once(*args, **kwargs)


def create_dirs(path):
    full_path = os.path.join(get_testdir(), path)
    os.makedirs(full_path, exist_ok=True)
    return full_path


def create_link(path, target, symlink=False):
    f = os.symlink if symlink else os.link
    f(
        os.path.join(get_testdir(), path),
        os.path.join(get_testdir(), target)
    )


def create_file(data, name, mtime=None, write_binary=False, sparse_bytes_before = 0, sparse_bytes_total = 0):
    full_path = os.path.join(get_testdir(), name)
    if '/' in name:
        os.makedirs(os.path.dirname(full_path), exist_ok=True)

    with open(full_path, 'wb' if write_binary else 'w') as handle:
        if sparse_bytes_before > 0:
            handle.truncate(sparse_bytes_before)
        if write_binary:
            if isinstance(data, int):
                handle.write(struct.pack('i', data))
            else:
                assert False, "Unhandled data type for binary write: " + data
        else:
            handle.write(data)
        if sparse_bytes_total > 0:
            handle.truncate(sparse_bytes_total)

    if mtime is not None:
        subprocess.call(['touch', '-m', '-d', str(mtime), full_path])

    return full_path


def warp_file_to_future(name, seconds):
    now = time.time()
    os.utime(os.path.join(get_testdir(), name), (now + seconds, now + seconds))


# XXX: now unused, but might be handy.
@contextlib.contextmanager
def create_special_fs(name, fs_type='ext4'):
    """
    Used to create a special filesystem container in TESTDIR_NAME
    under «name». The type of the filesystem will be «fs_type» (as long
    we have a «mkfs.fs_type» binary for that).
    This method needs root privileges.

    Returns: The path of the created directory.
    """
    mount_path = os.path.join(get_testdir(), name)
    device_path = mount_path + ".device"

    commands = [
        f"dd if=/dev/zero of={device_path} bs=1M count=20",
        f"mkdir -p {mount_path}",
        f"mkfs.{fs_type} {device_path}",
        f"mount -o loop {device_path} {mount_path}",
    ]

    for command in commands:
        subprocess.run(
                command,
                shell=True,
                check=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
        )

    try:
        yield mount_path
    finally:
        # whatever happens: unmount it again.
        # we'll get test errors in the next tests otherwise.
        unmount_command = f"umount {mount_path}"
        subprocess.run(unmount_command, shell=True, check=True)


@contextlib.contextmanager
def bind_mount_a_b(mnt_root):
    mnt_dir = os.path.join(mnt_root, 'a/b')
    if sys.platform.startswith("linux"):
        subprocess.call(('mount', '--bind', mnt_root, mnt_dir))
    elif sys.platform.startswith("freebsd"):
        pytest.xfail("https://bugs.freebsd.org/bugzilla/show_bug.cgi?id=297174")
        subprocess.call(('mount', '-t', 'nullfs', mnt_root, mnt_dir))
    else:
        pytest.skip(f"bind_mount: {sys.platform} not implemented/supported")
    try:
        yield
    finally:
        subprocess.call(('umount', mnt_dir))


@contextlib.contextmanager
def assert_exit_code(status_code):
    """
    Assert that the with block yields a subprocess.CalledProcessError
    with a certain return code. If nothing is thrown, status_code
    is required to be 0 to survive the test.
    """
    try:
        yield
    except subprocess.CalledProcessError as exc:
        assert exc.returncode == status_code
    else:
        # No exception? status_code should be fine.
        assert status_code == 0


def _up(path):
    while path:
        yield path
        if path == "/":
            break
        path = os.path.dirname(path)


_REFLINK_CAPABLE_FILESYSTEMS = {'btrfs', 'xfs', 'ocfs2'}
def is_on_reflink_fs(path):
    parts = psutil.disk_partitions(all=True)

    # iterate up from `path` until mountpoint found
    for up_path in _up(path):
        for part in parts:
            if up_path == part.mountpoint:
                print(f"{path} is {part.fstype} mounted at {part.mountpoint}")
                return part.fstype in _REFLINK_CAPABLE_FILESYSTEMS

    print(f"No mountpoint found for {path}")
    return False


def check_reflink_capable() -> str | None:
    if not has_feature('btrfs-support'):
        return "btrfs not supported"
    # Probing the base so that it works also at collection time.
    if not is_on_reflink_fs(TESTDIR_BASE):
        return "testdir is not on reflink-capable filesystem"
    return None


def check_xattr_capable() -> str | None:
    if not has_feature('xattr'):
        return "xattr not supported"

    with tempfile.NamedTemporaryFile(dir=TESTDIR_BASE) as probe:
        try:
            xattr.xattr(probe.name).set('user.rmlint_probe', b'1')
        except OSError as exc:
            return f"testdir does not support xattr: {exc}"

    return None


def pattern_count(path, patterns):
    """count the number of line in a file which start with each pattern"""
    counts = [0] * len(patterns)
    with open(path, encoding='utf-8') as f:
        for line in f:
            for i, pattern in enumerate(patterns):
                if re.match(pattern, line):
                    counts[i] += 1
    return counts
