"""Build helpers"""
import glob
import os
import re
import shutil
import sys
from pathlib import Path

from SCons.Action import Action
from SCons.Builder import Builder
from SCons.Defaults import Chmod, Delete
from SCons.Errors import UserError

PKG_CONFIG = os.getenv('PKG_CONFIG', 'pkg-config')

# Features that can be toggled with --with-<name>/--without-<name>:
OPTIONAL_FLAGS = ['libelf', 'gettext', 'fiemap', 'blkid', 'gui', 'compile-glib-schemas']


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


def create_uninstall_target(env, path: str|Path):
    path = str(path)
    cmd = env.Command('uninstall-' + path, path, [
        Delete('$SOURCE'),
    ])
    env.Alias('uninstall', 'uninstall-' + path)
    return cmd


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


###########################################################################
#                      Build-time generated C sources                     #
###########################################################################


def build_config_header(target, source, env) -> None:
    """Substitute the configure results into lib/config.h.in."""
    template = source[0].get_text_contents()
    Path(target[0].get_abspath()).write_text(
        template.format(**source[1].read()), encoding='utf-8'
    )


def encode_payload(data: bytes, name: str) -> str:
    """Render *data* as a C byte list mirroring its own line structure."""
    lines = data.splitlines(keepends=True)
    rows = []

    for number, line in enumerate(lines, start=1):
        text = line.rstrip(b'\r\n')
        if b'*/' in text:
            raise UserError(
                f'{name}:{number}: contains "*/", which would close the '
                'generated comment early'
            )

        last = number == len(lines)
        rows.append(f'/* {text.decode("utf-8", "replace")} */')
        rows.append(','.join(f'0x{byte:02x}' for byte in line) + ('' if last else ','))

    return '\n'.join(rows) + '\n'


def embed_payload(target, source, env) -> None:
    """Encode source[0] into a byte list for the formatters to #include."""
    Path(target[0].get_abspath()).write_text(
        encode_payload(source[0].get_contents(), source[0].path), encoding='utf-8'
    )


ConfigHeaderBuilder = Builder(
    action=Action(build_config_header, 'Generating $TARGET'),
)

EmbedPayloadBuilder = Builder(
    action=Action(embed_payload, 'Encoding $SOURCE ==> $TARGET'),
    single_source=True,
)


###########################################################################
#                     clang tooling (clangd / clang-tidy)                 #
###########################################################################

# others flags might be irrelevant. TODO: refine
CLANG_FLAG_KEEP_PREFIXES = ('-std=', '-D', '-U', '-I', '-m', '-pthread', '-fPIC')


def collect_clang_flags(env):
    """Compile flags for clang tooling"""
    flags = ['-I.', '-Ilib']  # resolve "config.h" + lib-root quote includes
    for flag in env['CCFLAGS']:
        flag = str(flag)
        if flag.startswith(CLANG_FLAG_KEEP_PREFIXES):
            flags.append(flag)
    for path in env.get('CPPPATH', []):
        flags.append('-I' + env.subst(str(path)))
    for define in env.get('CPPDEFINES', []):
        if isinstance(define, (list, tuple)):
            name = str(define[0])
            value = define[1] if len(define) > 1 else None
        else:
            name = str(define)
            value = None
        flags.append(f'-D{name}={value}' if value is not None else f'-D{name}')

    seen, unique = set(), []  # de-dup, keep order
    for flag in flags:
        if flag not in seen:
            seen.add(flag)
            unique.append(flag)
    return unique


def write_compile_flags(target, source, env):
    """Write compile_flags.txt and .clang_complete link"""
    flags = source[0].read()
    with open(target[0].get_abspath(), 'w', encoding='utf-8') as handle:
        handle.write('\n'.join(flags) + '\n')

    link = os.path.join(os.path.dirname(target[0].get_abspath()), '.clang_complete')
    try:
        if os.path.lexists(link):
            os.remove(link)
        os.symlink(os.path.basename(target[0].get_abspath()), link)  # relative
    except OSError as err:
        print('Warning: could not create .clang_complete symlink: ' + str(err))
