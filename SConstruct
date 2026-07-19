#!/usr/bin/env python3
import os
import re
import sys
import glob
import shlex
import shutil
import subprocess
import platform

import SCons
import SCons.Conftest as tests
from SCons.Script import *
from SCons.Script.SConscript import SConsEnvironment

pkg_config = os.getenv('PKG_CONFIG', 'pkg-config')

DEFAULT_PREFIX = '/usr'
PREFIX_RECORD_FILE = '.prefix.txt'

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


VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_NAME, STATIC_GIT_REV = read_version()
Export('VERSION_MAJOR VERSION_MINOR VERSION_PATCH VERSION_NAME')

###########################################################################
#                                Utilities                                #
###########################################################################

def check_pkgconfig(context, version):
    context.Message('Checking for pkg-config... ')
    command = pkg_config + ' --atleast-pkgconfig-version=' + version
    ret = context.TryAction(command)[0]
    context.Result(ret)
    return ret


def check_pkg(context, name, varname, required=True):
    rc, text = 1, ''

    package = name.split()[0]
    if package in OPTIONAL_FLAGS and GetOption('with_' + package) is False:
        context.Message(f'Explicitly disabling {name}...')
        rc = 0

    if rc != 0:
        context.Message(f'Checking for {name}... ')
        rc, text = context.TryAction(f"{pkg_config} --exists '{name}'")

    # 0 is defined as error by TryAction
    if rc == 0 and required:
        print('Error: ' + name + ' not found.')
        Exit(1)

    # Remember we have it:
    conf.env[varname] = rc
    context.Result(rc)
    return rc, text


def check_git_rev(context):
    context.Message('Checking for git revision... ')
    rev = STATIC_GIT_REV

    try:
        rev = subprocess.check_output(
            ['git', 'log', '--pretty=format:%h', '-n', '1'],
            stderr=subprocess.DEVNULL,
        ).decode('ascii').strip()
    except (OSError, subprocess.CalledProcessError):
        # Not a git checkout (e.g. building from a release tarball, where
        # STATIC_GIT_REV from .version takes over) or git is unavailable.
        print('Unable to find git revision.')

    rev = rev or 'unknown'
    conf.env['gitrev'] = rev
    context.Result(rev)
    return rev


def check_sysmacro_h(context):
    rc = 1
    if rc and tests.CheckHeader(context, 'sys/sysmacros.h'):
        rc = 0

    conf.env['HAVE_SYSMACROS_H'] = rc
    context.did_show_result = True
    context.Result(rc)
    return rc


def check_libelf(context):
    rc = 1

    if GetOption('with_libelf') is False:
        rc = 0

    if rc and tests.CheckHeader(context, 'libelf.h', header="#include <stdlib.h>"):
        rc = 0

    if rc and tests.CheckLib(context, ['libelf']):
        rc = 0

    conf.env['HAVE_LIBELF'] = rc

    context.did_show_result = True
    context.Result(rc)
    return rc


def check_uname(context):
    rc = 1

    if rc and tests.CheckHeader(context, 'sys/utsname.h', header=""):
        rc = 0

    conf.env['HAVE_UNAME'] = rc

    context.did_show_result = True
    context.Result(rc)
    return rc


def check_gettext(context):
    rc = 1

    if GetOption('with_gettext') is False:
        rc = 0

    if rc and tests.CheckHeader(context, 'locale.h'):
        rc = 0

    conf.env['HAVE_LIBINTL'] = rc
    conf.env['HAVE_MSGFMT'] = int(WhereIs('msgfmt') is not None)
    conf.env['HAVE_GETTEXT'] = conf.env['HAVE_MSGFMT'] and conf.env['HAVE_LIBINTL']

    context.did_show_result = True
    context.Result(rc)
    return rc


def check_fiemap(context):
    rc = 1

    if GetOption('with_fiemap') is False:
        rc = 0

    if rc and tests.CheckType(context, 'struct fiemap', header='#include <linux/fiemap.h>\n'):
        rc = 0

    conf.env['HAVE_FIEMAP'] = rc

    context.did_show_result = True
    context.Result(rc)
    return rc


def check_bigfiles(context):
    off_t_is_big_enough = True

    if tests.CheckTypeSize(context, 'off_t', header='#include <sys/types.h>\n') < 8:
        off_t_is_big_enough = False

    have_stat64 = True
    if tests.CheckFunc(
        context, 'stat64'
    ):
        have_stat64 = False

    rc = int(off_t_is_big_enough or have_stat64)
    conf.env['HAVE_BIG_OFF_T'] = int(off_t_is_big_enough)
    conf.env['HAVE_BIG_STAT'] = int(have_stat64)
    conf.env['HAVE_BIGFILES'] = rc

    context.did_show_result = True
    context.Result(rc)
    return rc


def check_blkid(context):
    rc = 1

    if GetOption('with_blkid') is False:
        rc = 0

    if rc == 1 and tests.CheckDeclaration(
        context,
        symbol='blkid_devno_to_wholedisk',
        includes='#include <blkid.h>\n'
    ):
        rc = 0

    conf.env['HAVE_BLKID'] = rc

    context.did_show_result = True
    context.Result(rc)
    return rc


def check_sys_block(context):
    rc = 1

    context.Message('Checking for existence of /sys/block... ')
    if not os.access('/sys/block', os.R_OK):
        rc = 0

    conf.env['HAVE_SYSBLOCK'] = rc

    context.Result(rc)
    return rc


def check_posix_fadvise(context):
    rc = 1

    if tests.CheckDeclaration(
        context, 'posix_fadvise',
        includes='#include <fcntl.h>'
    ):
        rc = 0

    conf.env['HAVE_POSIX_FADVISE'] = rc

    context.did_show_result = True
    context.Result(rc)
    return rc


def check_xattr(context):
    rc = 1

    for func in ['getxattr', 'setxattr', 'removexattr', 'listxattr']:
        if tests.CheckFunc(
            context, func
        ):
            rc = 0
            break

    conf.env['HAVE_XATTR'] = rc

    context.did_show_result = True
    context.Result(rc)
    return rc



def check_lxattr(context):
    rc = 1

    for func in ['lgetxattr', 'lsetxattr', 'lremovexattr', 'llistxattr']:
        if tests.CheckFunc(
            context, func
        ):
            rc = 0
            break

    conf.env['HAVE_LXATTR'] = rc

    context.did_show_result = True
    context.Result(rc)
    return rc


def check_sha512(context):
    rc = 1
    if tests.CheckDeclaration(context, 'G_CHECKSUM_SHA512', includes='#include <glib.h>\n'):
        rc = 0

    conf.env['HAVE_SHA512'] = rc

    context.did_show_result = True
    context.Result(rc)
    return rc


def check_btrfs_h(context):
    rc = 1
    if tests.CheckHeader(
        context, 'linux/btrfs.h',
        header='#include <stdlib.h>\n#include <sys/ioctl.h>'
    ):
        rc = 0

    conf.env['HAVE_BTRFS_H'] = rc
    context.did_show_result = True
    context.Result(rc)
    return rc

def check_linux_fs_h(context):
    rc = 1
    if tests.CheckHeader(context, 'linux/fs.h'):
        rc = 0

    conf.env['HAVE_LINUX_FS_H'] = rc
    context.did_show_result = True
    context.Result(rc)
    return rc

def check_linux_limits(context):
    rc = 1
    if tests.CheckHeader(context, 'linux/limits.h'):
        rc = 0

    conf.env['HAVE_LINUX_LIMITS'] = rc
    context.did_show_result = True
    context.Result(rc)
    return rc

def check_cygwin(context):
    rc = 0

    context.Message('Checking for cygwin environment...')
    try:
        uname = platform.uname()
        context.Message('/'.join(uname))
        rc = (uname[0].upper().startswith("CYGWIN"))
    except subprocess.CalledProcessError:
        rc = 0  # Oops.
        context.Message("platform.uname() failed")

    conf.env['IS_CYGWIN'] = rc
    context.Result(rc)
    return rc

def check_mm_crc32_u64(context):

    rc = 0 if tests.CheckDeclaration(
            context,
            symbol='_mm_crc32_u64',
            includes='#include <nmmintrin.h>\n'
            ) else 1

    conf.env['HAVE_MM_CRC32_U64'] = rc
    context.did_show_result = True
    context.Result(rc)
    return rc

def check_builtin_cpu_supports(context):
    rc = 0 if tests.CheckDeclaration(
            context,
            symbol='__builtin_cpu_supports'
            ) else 1

    conf.env['HAVE_BUILTIN_CPU_SUPPORTS'] = rc
    context.did_show_result = True
    context.Result(rc)
    return rc


def read_cpu_flags():
    try:
        from cpuinfo import get_cpu_info
        return set(get_cpu_info().get('flags', []))
    except Exception:
        pass

    # No py-cpuinfo; /proc/cpuinfo uses the same flag names.
    try:
        with open('/proc/cpuinfo') as handle:
            for line in handle:
                key, _, value = line.partition(':')
                if key.strip() == 'flags':
                    return set(value.split())
    except OSError:
        pass

    print('   Unable to detect CPU flags (tried py-cpuinfo and /proc/cpuinfo)')
    return set()


def check_cpu_extensions(context):
    print('==> Checking CPU checksum and vector extensions...')

    cpu_flags = set()
    if ARGUMENTS.get('CPU_EXTENSIONS') != '0':
        cpu_flags = read_cpu_flags()

    for ext in ['AVX512F', 'AVX512VL', 'AVX2', 'SSE4_1', 'SSE2']:
        have_ext = int(ext.lower() in cpu_flags)
        conf.env['HAVE_' + ext] = have_ext
        print(f'    {ext}: {have_ext}')

    context.did_show_result = True
    context.Result(1)
    return 1

def create_uninstall_target(env, path):
    env.Command("uninstall-" + path, path, [
        Delete("$SOURCE"),
    ])
    env.Alias("uninstall", "uninstall-" + path)


Export('create_uninstall_target')


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


Export('find_sphinx_binary')

###########################################################################
#                                 Colors!                                 #
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
#                            Option Parsing                               #
###########################################################################

def get_default_prefix():
    if 'uninstall' in COMMAND_LINE_TARGETS:
        try:
            with open(PREFIX_RECORD_FILE, 'r') as handle:
                prefix = handle.read()
            print(f'===> Using cached installation prefix "{prefix}"')
            return prefix
        except OSError as err:
            print(f'===> Failed to get cached installation prefix: {err}')
    return DEFAULT_PREFIX


AddOption(
    '--prefix', default=get_default_prefix(),
    dest='prefix', type='string', nargs=1,
    action='store', metavar='DIR', help='installation prefix'
)

AddOption(
    '--actual-prefix', default=None,
    dest='actual_prefix', type='string', nargs=1,
    action='store', metavar='DIR', help='where files will eventually land'
)

AddOption(
    '--libdir', default='lib',
    dest='libdir', type='string', nargs=1,
    action='store', metavar='DIR', help='libdir name (lib or lib64)'
)

for suffix in OPTIONAL_FLAGS:
    AddOption(
        '--without-' + suffix, action='store_const', default=False, const=False,
        dest='with_' + suffix
    )
    AddOption(
        '--with-' + suffix, action='store_const', default=True, const=True,
        dest='with_' + suffix
    )

if 'install' in COMMAND_LINE_TARGETS:
    # record the installation prefix for later uninstall
    with open(PREFIX_RECORD_FILE, 'w') as f:
        f.write(GetOption('prefix'))


# General Environment
options = dict(
    CXXCOMSTR=compile_source_message,
    CCCOMSTR=compile_source_message,
    SHCCCOMSTR=compile_shared_source_message,
    SHCXXCOMSTR=compile_shared_source_message,
    ARCOMSTR=link_library_message,
    RANLIBCOMSTR=ranlib_library_message,
    SHLINKCOMSTR=link_shared_library_message,
    LINKCOMSTR=link_program_message,
    PREFIX=GetOption('prefix'),
    ENV = dict([ (key, os.environ[key])
                 for key in os.environ
                 if key in ['PATH', 'TERM', 'HOME', 'PKG_CONFIG_PATH']
              ])
)

if ARGUMENTS.get('VERBOSE') == "1":
    del options['CCCOMSTR']
    del options['LINKCOMSTR']

# Actually instance the Environment with all collected information:
env = Environment(**options)
Export('env')

###########################################################################
#                           Dependency Checks                             #
###########################################################################

# Configuration:
conf = Configure(env, custom_tests={
    'check_pkgconfig': check_pkgconfig,
    'check_pkg': check_pkg,
    'check_git_rev': check_git_rev,
    'check_libelf': check_libelf,
    'check_fiemap': check_fiemap,
    'check_xattr': check_xattr,
    'check_lxattr': check_lxattr,
    'check_sha512': check_sha512,
    'check_blkid': check_blkid,
    'check_posix_fadvise': check_posix_fadvise,
    'check_sys_block': check_sys_block,
    'check_bigfiles': check_bigfiles,
    'check_gettext': check_gettext,
    'check_linux_limits': check_linux_limits,
    'check_btrfs_h': check_btrfs_h,
    'check_linux_fs_h': check_linux_fs_h,
    'check_uname': check_uname,
    'check_cygwin': check_cygwin,
    'check_mm_crc32_u64': check_mm_crc32_u64,
    'check_cpu_extensions': check_cpu_extensions,
    'check_builtin_cpu_supports': check_builtin_cpu_supports,
    'check_sysmacro_h': check_sysmacro_h
})

#######################################################################
#                      Compiler Checks and Flags                      #
#######################################################################

if 'CC' in os.environ:
    conf.env.Replace(CC=os.environ['CC'])
    print(">> Using compiler: " + os.environ['CC'])

if 'CFLAGS' in os.environ:
    conf.env.Append(CCFLAGS=os.environ['CFLAGS'])
    print(">> Appending custom build flags : " + os.environ['CFLAGS'])

if 'LDFLAGS' in os.environ:
    conf.env.Append(LINKFLAGS=os.environ['LDFLAGS'])
    print(">> Appending custom link flags : " + os.environ['LDFLAGS'])

if 'AR' in os.environ:
    conf.env.Replace(AR=os.environ['AR'])
    print(">> Using ar: " + os.environ['AR'])

if 'NM' in os.environ:
    conf.env.Replace(NM=os.environ['NM'])
    print(">> Using nm: " + os.environ['NM'])

if 'RANLIB' in os.environ:
    conf.env.Replace(RANLIB=os.environ['RANLIB'])
    print(">> Using ranlib: " + os.environ['RANLIB'])

if not conf.CheckCC():
    print('Error: Your compiler and/or environment is not correctly configured.')
    Exit(1)

conf.check_git_rev()
conf.check_pkgconfig('0.15.0')

# Pkg-config to internal name
conf.env['HAVE_GLIB'] = 0
conf.check_pkg('glib-2.0 >= 2.64', 'HAVE_GLIB', required=True)
conf.env.Append(CCFLAGS=[
    '-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_64',
    '-DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_64',
])

conf.env['HAVE_GIO_UNIX'] = 0
conf.check_pkg('gio-unix-2.0', 'HAVE_GIO_UNIX', required=False)

conf.env['HAVE_BLKID'] = 0
conf.check_pkg('blkid', 'HAVE_BLKID', required=False)

conf.env['HAVE_JSON_GLIB'] = 0
conf.check_pkg('json-glib-1.0', 'HAVE_JSON_GLIB', required=True)

packages = ['glib-2.0', 'json-glib-1.0']
if conf.env['HAVE_BLKID']:
    packages.append('blkid')

if conf.env['HAVE_GIO_UNIX']:
    packages.append('gio-unix-2.0')

# C11 is assumed on all supported platforms (Debian 13 / RHEL 10 floors):
conf.env.Append(CCFLAGS=[
    '-std=c11', '-pipe', '-D_GNU_SOURCE'
])

# Support cygwin:
conf.check_cygwin()
if conf.env['IS_CYGWIN']:
    conf.env.Append(CCFLAGS=['-U__STRICT_ANSI__'])
else:
    conf.env.Append(CCFLAGS=['-fPIC'])

# check _mm_crc32_u64 (SSE4.2) support:
conf.check_mm_crc32_u64()

if any(cc in os.path.basename(conf.env['CC']) for cc in ('clang', 'include-what-you-use')):
    conf.env.Append(CCFLAGS=['-fcolor-diagnostics'])  # Colored warnings
    conf.env.Append(CCFLAGS=['-Qunused-arguments'])   # Hide wrong messages
    conf.env.Append(CCFLAGS=['-Wno-bad-function-cast'])
else:
    conf.env.Append(CCFLAGS=['-Wno-cast-function-type'])

# Optional flags:
conf.env.Append(CCFLAGS=[
    '-Wall', '-W', '-Wextra',
    '-Winit-self',
    '-Wstrict-aliasing',
    '-Wmissing-include-dirs',
    '-Wuninitialized',
    '-Wstrict-prototypes',
    '-Wno-implicit-fallthrough',
])


env.ParseConfig(pkg_config + ' --cflags --libs ' + ' '.join(packages))


conf.env.Append(_LIBFLAGS=['-lm'])

conf.check_builtin_cpu_supports()
conf.check_blkid()
conf.check_sys_block()
conf.check_libelf()
conf.check_fiemap()
conf.check_xattr()
conf.check_lxattr()
conf.check_bigfiles()
conf.check_sha512()
conf.check_gettext()
conf.check_linux_limits()
conf.check_posix_fadvise()
conf.check_btrfs_h()
conf.check_linux_fs_h()
conf.check_uname()
conf.check_sysmacro_h()
conf.check_cpu_extensions()

if conf.env['HAVE_LIBELF']:
    conf.env.Append(_LIBFLAGS=['-lelf'])

if conf.env['HAVE_AVX2']:
    conf.env.Append(CCFLAGS=['-mavx2'])

if conf.env['HAVE_AVX512F'] and conf.env['HAVE_AVX512VL']:
    conf.env.Append(CCFLAGS=['-mavx512f', '-mavx512vl'])

if conf.env['HAVE_SSE4_1']:
    conf.env.Append(CCFLAGS=['-msse4.1'])

if conf.env['HAVE_SSE2']:
    conf.env.Append(CCFLAGS=['-msse2'])

# NB: After checks so they don't fail
conf.env.Append(CCFLAGS=['-Werror=undef'])


if ARGUMENTS.get('GDB') == '1':
    ARGUMENTS['DEBUG'] = '1'
    ARGUMENTS['SYMBOLS'] = '1'

O_DEBUG   = 'g' # The optimisation level for a debug   build
O_RELEASE = '2' # The optimisation level for a release build

# build modes
if ARGUMENTS.get('DEBUG') == "1":
    print("Compiling in debug mode")
    conf.env.Append(CCFLAGS=['-DRM_DEBUG', '-fno-inline'])
    O_value = ARGUMENTS.get('O', O_DEBUG)
else:
    conf.env.Append(CCFLAGS=['-DG_DISABLE_ASSERT', '-DNDEBUG'])
    conf.env.Append(LINKFLAGS=['-s'])
    O_value = ARGUMENTS.get('O', O_RELEASE)

if O_value == 'debug':
    O_value = O_DEBUG
elif O_value == 'release':
    O_value = O_RELEASE

cc_O_option = '-O' + O_value

print("Using compiler optimisation {} (to change, run scons with O=[0|1|2|3|s|fast])".format(cc_O_option))
conf.env.Append(CCFLAGS=[cc_O_option])

if ARGUMENTS.get('SYMBOLS') == '1':
    print("Compiling with debugging symbols")
    conf.env.Append(CCFLAGS='-g3')

value = ARGUMENTS.get('CCFLAGS')
if value:
    print("Appending custom build flags provided on command line: " + value)
    conf.env.Append(CCFLAGS=shlex.split(value))


def InstallPerm(env, dest, files, perm):
    obj = env.Install(dest, files)
    for i in obj:
        env.AddPostAction(i, Chmod(str(i), perm))
    return dest

# put this function "in" scons
SConsEnvironment.InstallPerm = InstallPerm

# Your extra checks here
env = conf.Finish()

def get_cpu_count():
    if 'NUM_CPU' in os.environ:
        return int(os.environ['NUM_CPU'])
    return os.cpu_count() or 4


# set number of parallel jobs during build
# note: while not particularly intuitive or obvious from the documentation,
# SetOption() will *not* over-ride commandline option passed by `scons -j<n>`
# or `scons --jobs=<n>`
SetOption('num_jobs', get_cpu_count())

print(f"Running with --jobs={GetOption('num_jobs')}")

library = SConscript('lib/SConscript')
programs = SConscript('src/SConscript', exports='library')
env.Default(library)

SConscript('tests/SConscript', exports='programs')
SConscript('po/SConscript')
SConscript('docs/SConscript')
SConscript('gui/SConscript')


def build_tar_gz(target=None, source=None, env=None):
    tarball = f'rmlint-{VERSION_MAJOR}.{VERSION_MINOR}.{VERSION_PATCH}.tar.gz'
    subprocess.call(['git', 'archive', 'HEAD', '-9', '--format', 'tar.gz', '-o', tarball])
    print('Wrote tarball to ./' + tarball)


if 'dist' in COMMAND_LINE_TARGETS:
    env.Command('dist', None, Action(build_tar_gz, "Building release tarball..."))


if 'release' in COMMAND_LINE_TARGETS:
    def replace_version_strings(target=None, source=None, env=None):
        print('Patching .version file...')
        with open('.version', 'r') as handle:
            text = handle.read().strip()

        if '@' not in text:
            with open('.version', 'w') as handle:
                handle.write(f"{text}@{conf.env['gitrev']}\n")

            # Commit the .version change, so git archive can see it.
            subprocess.check_call(
                'git add .version && git commit -m ".version bump; you should not see this commit."',
                shell=True
            )

        # Build the .tgz on the current state
        build_tar_gz()

        # We do not want lots of temp commits, so revert the latest one.
        if '@' not in text:
            subprocess.check_call('git reset --hard HEAD^', shell=True)
            with open('.version', 'w') as handle:
                handle.write(text + '\n')

    release = env.Command(
        'release', None, Action(replace_version_strings, "Bumping version...")
    )
    env.Depends(release, env.Alias('gettext'))


if 'config' in COMMAND_LINE_TARGETS:
    def print_config(target=None, source=None, env=None):
        yesno = lambda boolean: COLORS['green'] + 'yes' + COLORS['end'] if boolean else COLORS['red'] + 'no' + COLORS['end']

        sphinx_bin = find_sphinx_binary()

        print('''
{grey}rmlint will be compiled with the following features:{end}

    Find non-stripped binaries (needs libelf)             : {libelf}
    Optimize using ioctl(FS_IOC_FIEMAP) (needs linux)     : {fiemap}
    Support for SHA512 (needs glib >= 2.31)               : {sha512}
    AVX512F and AVX512VL cpu extensions                   : {avx512}
    AVX2 cpu extensions                                   : {avx2}
    SSE4.1 cpu extensions                                 : {sse41}
    SSE2 cpu extensions                                   : {sse2}
    Build manpage from docs/rmlint.1.rst                  : {sphinx}
    Support for caching checksums in file's xattr         : {xattr}
    Checking for proper support of big files >= 4GB       : {bigfiles}
        (needs either sizeof(off_t) >= 8 ...)             : {bigofft}
        (... or presence of stat64)                       : {bigstat}

    Optimize non-rotational disks                         : {nonrotational}
        (needs libblkid for resolving dev_t to path)      : {blkid}
        (needs gio-unix-2.0)                              : {gio_unix}

    Enable gettext localization                           : {gettext}
        (needs <locale.h> for compile side support)       : {locale}
        (needs msgfmt to compile .po files)               : {msgfmt}

{grey}The following constants will be used during the build:{end}

    Version information  : {version}
    Compiler             : {compiler}
    Install prefix       : {prefix}
    Actual prefix        : {actual_prefix}
    Verbose building     : {verbose}
    Adding debug checks  : {debug}
    Adding debug symbols : {symbols}
    Compile Glib schemas : {compile_glib_schemas}

Type 'scons' to actually compile rmlint now. Good luck.
    '''.format(
            grey=COLORS['grey'], end=COLORS['end'],
            libelf=yesno(env['HAVE_LIBELF']),
            gettext=yesno(env['HAVE_GETTEXT']),
            locale=yesno(env['HAVE_LIBINTL']),
            msgfmt=yesno(env['HAVE_MSGFMT']),
            xattr=yesno(env['HAVE_XATTR']),
            nonrotational=yesno(env['HAVE_GIO_UNIX'] & env['HAVE_BLKID']),
            gio_unix=yesno(env['HAVE_GIO_UNIX']),
            blkid=yesno(env['HAVE_BLKID']),
            fiemap=yesno(env['HAVE_FIEMAP']),
            sha512=yesno(env['HAVE_SHA512']),
            avx512=yesno(env['HAVE_AVX512F'] and env['HAVE_AVX512VL']),
            avx2=yesno(env['HAVE_AVX2']),
            sse41=yesno(env['HAVE_SSE4_1']),
            sse2=yesno(env['HAVE_SSE2']),
            bigfiles=yesno(env['HAVE_BIGFILES']),
            bigofft=yesno(env['HAVE_BIG_OFF_T']),
            bigstat=yesno(env['HAVE_BIG_STAT']),
            sphinx=COLORS['green'] + 'yes, using ' + COLORS['end'] + sphinx_bin if sphinx_bin else yesno(sphinx_bin),
            compiler=env['CC'],
            prefix=GetOption('prefix'),
            actual_prefix=GetOption('actual_prefix') or GetOption('prefix'),
            compile_glib_schemas=yesno(GetOption('with_compile-glib-schemas')),
            verbose=yesno(ARGUMENTS.get('VERBOSE') == '1'),
            debug=yesno(ARGUMENTS.get('DEBUG') == '1'),
            symbols=yesno(ARGUMENTS.get('SYMBOLS') == '1'),
            version='{a}.{b}.{c} "{n}" (rev {r})'.format(
                a=VERSION_MAJOR, b=VERSION_MINOR, c=VERSION_PATCH,
                n=VERSION_NAME, r=env.get('gitrev', 'unknown')
            )
        ))

    env.Command('config', None, Action(print_config, "Printing configuration..."))
