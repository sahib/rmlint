"""Build helpers"""
import glob
import os
import re
import shutil
import sys

from SCons.Defaults import Chmod, Delete

PKG_CONFIG = os.getenv('PKG_CONFIG', 'pkg-config')

# Features that can be toggled with --with-<name>/--without-<name>:
OPTIONAL_FLAGS = ['libelf', 'gettext', 'fiemap', 'blkid', 'gui', 'compile-glib-schemas']


def read_version():
    with open('.version', 'r') as handle:
        version_string = handle.read()

    static_git_rev = None
    version_numbers, release_name = version_string.split(' ', 1)
    if '@' in release_name:
        release_name, static_git_rev = release_name.split('@', 1)
        static_git_rev = static_git_rev.strip()

    major, minor, patch = (int(v) for v in version_numbers.split('.'))
    return major, minor, patch, release_name.strip(), static_git_rev


###########################################################################
#                                 Colo(u)rs!                              #
###########################################################################

COLORS = {
    'cyan': '\033[96m',
    'purple': '\033[95m',
    'blue': '\033[94m',
    'green': '\033[92m',
    'yellow': '\033[93m',
    'red': '\033[91m',
    'grey': '\x1b[30;1m',
    'end': '\033[0m'
}

if not sys.stdout.isatty():
    COLORS = dict.fromkeys(COLORS, '')

# Configure the actual colors to our liking:
compile_source_message = \
    f"{COLORS['blue']}Compiling {COLORS['purple']}==> {COLORS['yellow']}$SOURCE{COLORS['end']}"

compile_shared_source_message = \
    f"{COLORS['blue']}Compiling shared {COLORS['purple']}==> {COLORS['yellow']}$SOURCE{COLORS['end']}"

link_program_message = \
    f"{COLORS['red']}Linking Program {COLORS['purple']}==> {COLORS['yellow']}$TARGET{COLORS['end']}"

link_library_message = \
    f"{COLORS['red']}Linking Static Library {COLORS['purple']}==> {COLORS['yellow']}$TARGET{COLORS['end']}"

ranlib_library_message = \
    f"{COLORS['red']}Ranlib Library {COLORS['purple']}==> {COLORS['yellow']}$TARGET{COLORS['end']}"

link_shared_library_message = \
    f"{COLORS['red']}Linking Shared Library {COLORS['purple']}==> {COLORS['yellow']}$TARGET{COLORS['end']}"


###########################################################################
#                             Install helpers                             #
###########################################################################

def InstallPerm(env, dest, files, perm):
    obj = env.Install(dest, files)
    for i in obj:
        env.AddPostAction(i, Chmod(str(i), perm))
    return dest


def create_uninstall_target(env, path):
    env.Command('uninstall-' + path, path, [
        Delete('$SOURCE'),
    ])
    env.Alias('uninstall', 'uninstall-' + path)


###########################################################################
#                              Misc helpers                               #
###########################################################################

def find_sphinx_binary():
    binary = shutil.which('sphinx-build')
    if binary:
        return binary

    # Fall back to versioned names like sphinx-build-8.1, newest first.
    def version_key(binary):
        match = re.search(r'(\d+(?:\.\d+)?)$', binary)
        return float(match.group(1)) if match else 0.0

    binaries = []
    for path in os.environ.get('PATH', '').split(os.pathsep):
        binaries.extend(glob.glob(os.path.join(path, 'sphinx-build-*')))

    binaries.sort(key=version_key, reverse=True)
    if binaries:
        print(f'Using sphinx-build binary: {binaries[0]}')
        return binaries[0]

    print('Unable to find sphinx binary in PATH')
    print('Will be unable to build manpage or html docs')
    return None


def get_cpu_count():
    if 'NUM_CPU' in os.environ:
        return int(os.environ['NUM_CPU'])
    return os.cpu_count() or 4
