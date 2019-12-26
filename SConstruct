#!/usr/bin/env python
# encoding: utf-8

import os
import sys
import glob
import subprocess
import platform

import SCons
import SCons.Conftest as tests
from SCons.Script.SConscript import SConsEnvironment

pkg_config = os.getenv('PKG_CONFIG', 'pkg-config')

def read_version():
    with open('.version', 'r') as handle:
        version_string = handle.read()

    static_git_rev = None
    version_numbers, release_name = version_string.split(' ', 1)
    if '@' in release_name:
        release_name, static_git_rev = release_name.split('@', 1)
        static_git_rev.strip()

    major, minor, patch = [int(v) for v in version_numbers.split('.')]
    return major, minor, patch, release_name.strip(), static_git_rev


VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_NAME, STATIC_GIT_REV = read_version()
Export('VERSION_MAJOR VERSION_MINOR VERSION_PATCH VERSION_NAME')

###########################################################################
#                                Utilities                                #
###########################################################################

def check_gcc_version(context):
    context.Message('Checking for GCC version... ')

    try:
        v = subprocess.check_output("printf '%s\n' __GNUC__ | gcc -E -P -", shell=True)
        try:
            v = int(v)
            context.Result(str(v))
        except ValueError:
            print('Expected a number, but got: ' + v)
            v = 0
    except subprocess.CalledProcessError:
        print('Unable to find GCC version.')
        v = 0
    except AttributeError:
        print('Not allowed.')
        v = 0

    conf.env['__GNUC__'] = v
    return v


def check_pkgconfig(context, version):
    context.Message('Checking for pkg-config... ')
    command = pkg_config + ' --atleast-pkgconfig-version=' + version
    ret = context.TryAction(command)[0]
    context.Result(ret)
    return ret


def check_pkg(context, name, varname, required=True):
    rc, text = 1, ''

    try:
        if GetOption('with_' + name.split()[0]) is False:
            context.Message('Explicitly disabling %s...' % name)
            rc = 0
    except AttributeError:
        pass

    if rc != 0:
        context.Message('Checking for %s... ' % name)
        rc, text = context.TryAction('%s --exists \'%s\'' % (pkg_config, name))

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
        rev = subprocess.check_output('git log --pretty=format:"%h" -n 1', shell=True)
    except subprocess.CalledProcessError:
        print('Unable to find git revision.')
    except AttributeError:
        # Patch for some special sandbox permission problems.
        # See https://github.com/sahib/rmlint/issues/143#issuecomment-139929733
        print('Not allowed.')

    if hasattr(rev, 'decode'):
       rev = rev.decode('ascii')

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

    if GetOption('with_gettext') is False:
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
        context, 'stat64',
        header=
            '#include <sys/types.h>'
            '#include <sys/stat.h>'
            '#include <unistd.h>'

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


def check_faccessat(context):
    # Seems to be missing in Mac OSX <= 10.9
    rc = 1

    if tests.CheckDeclaration(
        context, 'faccessat',
        includes='#include <unistd.h>'
    ):
        rc = 0

    if rc == 1 and tests.CheckDeclaration(
        context, 'AT_FDCWD',
        includes='#include <fcntl.h>'
    ):
        rc = 0

    conf.env['HAVE_FACCESSAT'] = rc

    context.did_show_result = True
    context.Result(rc)
    return rc


def check_xattr(context):
    rc = 1

    for func in ['getxattr', 'setxattr', 'removexattr', 'listxattr']:
        if tests.CheckFunc(
            context, func,
            header=
                '#include <sys/types.h>'
                '#include <sys/xattr.h>'
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
            context, func,
            header=
                '#include <sys/types.h>'
                '#include <sys/xattr.h>'
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


def check_c11(context):
    rc = 1

    context.Message('Checking for -std=c11 support...')
    try:
        cmd = 'echo "#if __STDC_VERSION__ < 201112L\n#error \"No C11 support!\"\n#endif" | {cc} -xc - -std=c11 -c'
        subprocess.check_call(
            cmd.format(cc=conf.env['CC']),
            shell=True
        )
    except subprocess.CalledProcessError:
        rc = 0  # Oops.

    conf.env['HAVE_C11'] = rc
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


def create_uninstall_target(env, path):
    env.Command("uninstall-" + path, path, [
        Delete("$SOURCE"),
    ])
    env.Alias("uninstall", "uninstall-" + path)


Export('create_uninstall_target')


def find_sphinx_binary():
    PATH = os.getenv('PATH')
    binaries = []
    for path in PATH.split(os.pathsep):
        binaries.extend(glob.glob(os.path.join(path, 'sphinx-build*')))

    def version_key(binary):
        splitted = binary.rsplit('-', 1)
        if len(splitted) < 3:
            return 0
        return float(splitted[-1])

    binaries = sorted(binaries, key=version_key, reverse=True)
    if binaries:
        print('Using sphinx-build binary: {}'.format(binaries[0]))
        return binaries[0]
    else:
        print('Unable to find sphinx binary in PATH')
        print('Will be unable to build manpage or html docs')


Export('find_sphinx_binary')

###########################################################################
#                                 Colors!                                 #
###########################################################################

if sys.stdout.isatty():
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
else:
    COLORS = {
        'cyan': '',
        'purple': '',
        'blue': '',
        'green': '',
        'yellow': '',
        'red': '',
        'grey': '',
        'end': ''
    }

# If the output is not a terminal, remove the COLORS
if not sys.stdout.isatty():
    for key, value in COLORS.items():
        COLORS[key] = ''

# Configure the actual colors to our liking:
compile_source_message = '%sCompiling %s==> %s$SOURCE%s' % \
    (COLORS['blue'], COLORS['purple'], COLORS['yellow'], COLORS['end'])

compile_shared_source_message = '%sCompiling shared %s==> %s$SOURCE%s' % \
    (COLORS['blue'], COLORS['purple'], COLORS['yellow'], COLORS['end'])

link_program_message = '%sLinking Program %s==> %s$TARGET%s' % \
    (COLORS['red'], COLORS['purple'], COLORS['yellow'], COLORS['end'])

link_library_message = '%sLinking Static Library %s==> %s$TARGET%s' % \
    (COLORS['red'], COLORS['purple'], COLORS['yellow'], COLORS['end'])

ranlib_library_message = '%sRanlib Library %s==> %s$TARGET%s' % \
    (COLORS['red'], COLORS['purple'], COLORS['yellow'], COLORS['end'])

link_shared_library_message = '%sLinking Shared Library %s==> %s$TARGET%s' % \
    (COLORS['red'], COLORS['purple'], COLORS['yellow'], COLORS['end'])

###########################################################################
#                            Option Parsing                               #
###########################################################################

AddOption(
    '--prefix', default='/usr/local',
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

for suffix in ['libelf', 'gettext', 'fiemap', 'blkid', 'json-glib', 'gui']:
    AddOption(
        '--without-' + suffix, action='store_const', default=False, const=False,
        dest='with_' + suffix
    )
    AddOption(
        '--with-' + suffix, action='store_const', default=True, const=True,
        dest='with_' + suffix
    )

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

# Actually instance the Environement with all collected information:
env = Environment(**options)
Export('env')

###########################################################################
#                           Dependency Checks                             #
###########################################################################

# Configuration:
conf = Configure(env, custom_tests={
    'check_gcc_version': check_gcc_version,
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
    'check_faccessat': check_faccessat,
    'check_sys_block': check_sys_block,
    'check_bigfiles': check_bigfiles,
    'check_c11': check_c11,
    'check_gettext': check_gettext,
    'check_linux_limits': check_linux_limits,
    'check_btrfs_h': check_btrfs_h,
    'check_linux_fs_h': check_linux_fs_h,
    'check_uname': check_uname,
    'check_cygwin': check_cygwin,
    'check_mm_crc32_u64': check_mm_crc32_u64,
    'check_builtin_cpu_supports': check_builtin_cpu_supports,
    'check_sysmacro_h': check_sysmacro_h
})

if not conf.CheckCC():
    print('Error: Your compiler and/or environment is not correctly configured.')
    Exit(1)

conf.check_git_rev()
conf.check_pkgconfig('0.15.0')

# Pkg-config to internal name
conf.env['HAVE_GLIB'] = 0
conf.check_pkg('glib-2.0 >= 2.32', 'HAVE_GLIB', required=True)

conf.env['HAVE_GIO_UNIX'] = 0
conf.check_pkg('gio-unix-2.0', 'HAVE_GIO_UNIX', required=False)

conf.env['HAVE_BLKID'] = 0
conf.check_pkg('blkid', 'HAVE_BLKID', required=False)

conf.env['HAVE_JSON_GLIB'] = 0
conf.check_pkg('json-glib-1.0', 'HAVE_JSON_GLIB', required=False)

if GetOption('with_json-glib') is False:
    conf.env['HAVE_JSON_GLIB'] = 0

packages = ['glib-2.0']
if conf.env['HAVE_BLKID']:
    packages.append('blkid')

if conf.env['HAVE_JSON_GLIB']:
    packages.append('json-glib-1.0')

if conf.env['HAVE_GIO_UNIX']:
    packages.append('gio-unix-2.0')

###########################################################################
#                           Compiler Flags                                #
###########################################################################

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

# Support museums or other debian flavours:
conf.check_c11()
if conf.env['HAVE_C11']:
    c_standard = ['-std=c11']
else:
    c_standard = ['-std=c99', '-fms-extensions']

conf.env.Append(CCFLAGS=c_standard)

conf.env.Append(CCFLAGS=[
    '-pipe', '-D_GNU_SOURCE'
])

# Support cygwin:
conf.check_cygwin()
if conf.env['IS_CYGWIN']:
    conf.env.Append(CCFLAGS=['-U__STRICT_ANSI__'])
else:
    conf.env.Append(CCFLAGS=['-fPIC'])

# check _mm_crc32_u64 (SSE4.2) support:
conf.check_mm_crc32_u64()

if 'clang' in os.path.basename(conf.env['CC']):
    conf.env.Append(CCFLAGS=['-fcolor-diagnostics'])  # Colored warnings
    conf.env.Append(CCFLAGS=['-Qunused-arguments'])   # Hide wrong messages
    conf.env.Append(CCFLAGS=['-Wno-bad-function-cast'])
else:
    gcc_version = conf.check_gcc_version()
    if gcc_version >= 8:
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
conf.check_faccessat()
conf.check_btrfs_h()
conf.check_linux_fs_h()
conf.check_uname()
conf.check_sysmacro_h()

if conf.env['HAVE_LIBELF']:
    conf.env.Append(_LIBFLAGS=['-lelf'])

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
    import shlex
    print("Appending custom build flags provided on command line: " + value)
    conf.env.Append(CCFLAGS=shlex.split(value))

SConsEnvironment.Chmod = SCons.Action.ActionFactory(
    os.chmod,
    lambda dest, mode: 'Chmod("%s", 0%o)' % (dest, mode)
)


def InstallPerm(env, dest, files, perm):
    obj = env.Install(dest, files)
    for i in obj:
        env.AddPostAction(i, env.Chmod(str(i), perm))
    return dest

# put this function "in" scons
SConsEnvironment.InstallPerm = InstallPerm

# Your extra checks here
env = conf.Finish()

def get_cpu_count():
    # priority: environ('NUM_CPU'), else try to read actual cpu count, else fallback
    fallback = 4

    if 'NUM_CPU' in os.environ:
        return int(os.environ.get('NUM_CPU'))

    # try multiprocessing.cpu_count() (Python 2.6+)
    try:
        import multiprocessing
        return multiprocessing.cpu_count()
    except (ImportError, NotImplementedError):
        pass

   # try psutil.cpu_count()
    try:
        import psutil
        return psutil.cpu_count()
    except (ImportError, AttributeError):
        pass

    # default value
    return fallback


# set number of parallel jobs during build
# note: while not particularly intuitive or obvious from the documentation,
# SetOption() will *not* over-ride commandline option passed by `scons -j<n>`
# or `scons --jobs=<n>`
SetOption('num_jobs', get_cpu_count())

print ("Running with --jobs=" + repr(GetOption('num_jobs')))

library = SConscript('lib/SConscript')
programs = SConscript('src/SConscript', exports='library')
env.Default(library)

SConscript('tests/SConscript', exports='programs')
SConscript('po/SConscript')
SConscript('docs/SConscript')
SConscript('gui/SConscript')


def build_tar_gz(target=None, source=None, env=None):
    tarball = 'rmlint-{a}.{b}.{c}.tar.gz'.format(
        a=VERSION_MAJOR, b=VERSION_MINOR, c=VERSION_PATCH
    )

    subprocess.call(
        'git archive HEAD -9 --format tar.gz -o ' + tarball,
        shell=True
    )

    print('Wrote tarball to ./' + tarball)


if 'dist' in COMMAND_LINE_TARGETS:
    env.Command('dist', None, Action(build_tar_gz, "Building release tarball..."))


if 'release' in COMMAND_LINE_TARGETS:
    def replace_version_strings(target=None, source=None, env=None):
        new_version = '{a}.{b}.{c}'.format(
            a=VERSION_MAJOR, b=VERSION_MINOR, c=VERSION_PATCH
        )

        cmds = [
            'sed -i "s/2\.0\.0/{v}/g" po/rmlint.pot',
            'sed -i "s/^Version:\(\s*\)2\.0\.0/Version:\\1{v}/g" pkg/fedora/rmlint.spec'
        ]

        for cmd in cmds:
            print('Running: ' + cmd)
            subprocess.check_call(cmd.format(v=new_version), shell=True)

        if conf.env['gitrev'] is not None:
            print('Patching .version file...')
            with open('.version', 'r') as handle:
                text = handle.read().strip()

            if '@' not in text:
                with open('.version', 'w') as handle:
                    handle.write(text + '@' + conf.env['gitrev'] + '\n')

                # Commit the .version change, so git archive can see it.
                subprocess.check_call(
                    'git add .version && git commit -m \".version bump; you should not see this commit.\"',
                    shell=True
                )

            # Build the .tgz on the current state
            build_tar_gz()

            # We do not want lots of temp commits, so revert the latest one.
            if '@' not in text:
                subprocess.check_call('git reset --hard HEAD^', shell=True)
                with open('.version', 'w') as handle:
                    handle.write(text + '\n')

            return

        build_tar_gz()

    env.Command('release', None, Action(replace_version_strings, "Bumping version..."))


if 'config' in COMMAND_LINE_TARGETS:
    def print_config(target=None, source=None, env=None):
        yesno = lambda boolean: COLORS['green'] + 'yes' + COLORS['end'] if boolean else COLORS['red'] + 'no' + COLORS['end']

        sphinx_bin = find_sphinx_binary()

        print('''
{grey}rmlint will be compiled with the following features:{end}

    Find non-stripped binaries (needs libelf)             : {libelf}
    Optimize using ioctl(FS_IOC_FIEMAP) (needs linux)     : {fiemap}
    Support for SHA512 (needs glib >= 2.31)               : {sha512}
    Build manpage from docs/rmlint.1.rst                  : {sphinx}
    Support for caching checksums in file's xattr         : {xattr}
    Support for reading json caches (needs json-glib)     : {json_glib}
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

Type 'scons' to actually compile rmlint now. Good luck.
    '''.format(
            grey=COLORS['grey'], end=COLORS['end'],
            libelf=yesno(env['HAVE_LIBELF']),
            gettext=yesno(env['HAVE_GETTEXT']),
            locale=yesno(env['HAVE_LIBINTL']),
            msgfmt=yesno(env['HAVE_MSGFMT']),
            xattr=yesno(env['HAVE_XATTR']),
            json_glib=yesno(env['HAVE_JSON_GLIB']),
            nonrotational=yesno(env['HAVE_GIO_UNIX'] & env['HAVE_BLKID']),
            gio_unix=yesno(env['HAVE_GIO_UNIX']),
            blkid=yesno(env['HAVE_BLKID']),
            fiemap=yesno(env['HAVE_FIEMAP']),
            sha512=yesno(env['HAVE_SHA512']),
            bigfiles=yesno(env['HAVE_BIGFILES']),
            bigofft=yesno(env['HAVE_BIG_OFF_T']),
            bigstat=yesno(env['HAVE_BIG_STAT']),
            sphinx=COLORS['green'] + 'yes, using ' + COLORS['end'] + sphinx_bin if sphinx_bin else yesno(sphinx_bin),
            compiler=env['CC'],
            prefix=GetOption('prefix'),
            actual_prefix=GetOption('actual_prefix') or GetOption('prefix'),
            verbose=yesno(ARGUMENTS.get('VERBOSE')),
            debug=yesno(ARGUMENTS.get('DEBUG')),
            symbols=yesno(ARGUMENTS.get('SYMBOLS')),
            version='{a}.{b}.{c} "{n}" (rev {r})'.format(
                a=VERSION_MAJOR, b=VERSION_MINOR, c=VERSION_PATCH,
                n=VERSION_NAME, r=env.get('gitrev', 'unknown')
            )
        ))

    env.Command('config', None, Action(print_config, "Printing configuration..."))
