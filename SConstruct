#!/usr/bin/env python
# encoding: utf-8

import os
import sys
import glob
import subprocess

import SCons.Conftest as tests


def read_version():
    with open('.version', 'r') as handle:
        version_string = handle.read()

    version_numbers, release_name = version_string.split(' ', 1)
    major, minor, patch = [int(v) for v in version_numbers.split('.')]
    return major, minor, patch, release_name


VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_NAME = read_version()
Export('VERSION_MAJOR VERSION_MINOR VERSION_PATCH VERSION_NAME')

###########################################################################
#                                Utilities                                #
###########################################################################

def check_pkgconfig(context, version):
    context.Message('Checking for pkg-config... ')
    command = 'pkg-config --atleast-pkgconfig-version=%s' % version
    ret = context.TryAction(command)[0]
    context.Result(ret)
    return ret


def check_pkg(context, name, varname, required=True):
    context.Message('Checking for %s... ' % name)
    rc, text = context.TryAction('pkg-config --exists \'%s\'' % name)
    context.Result(rc)

    # 0 is defined as error by TryAction
    if rc is 0:
        print('Error: ' + name + ' not found.')
        if required:
            Exit(1)

    # Remember we have it:
    conf.env[varname] = True

    return rc, text


def check_git_rev(context):
    context.Message('Checking for git revision... ')
    rev = VERSION_NAME

    try:
        rev = subprocess.check_output('git log --pretty=format:"%h" -n 1', shell=True)
    except subprocess.CalledProcessError:
        print('Unable to find git revision.')

    conf.env['gitrev'] = rev
    context.Result(rev)
    return rev


def check_libelf(context):
    rc = 1
    if tests.CheckHeader(context, 'libelf.h'):
        rc = 0

    if tests.CheckLib(context, ['libelf']):
        rc = 0

    conf.env['HAVE_LIBELF'] = rc

    context.did_show_result = True
    context.Result(rc)
    return rc


def check_gettext(context):
    rc = 1
    if tests.CheckHeader(context, 'locale.h'):
        rc = 0

    conf.env['HAVE_LIBINTL'] = rc
    conf.env['HAVE_MSGFMT'] = int(WhereIs('msgfmt') is not None)
    conf.env['HAVE_GETTEXT'] = conf.env['HAVE_MSGFMT'] and conf.env['HAVE_LIBINTL']

    context.did_show_result = True
    context.Result(rc)
    return rc


def check_fiemap(context):
    rc = 1
    if tests.CheckType(context, 'struct fiemap', header='#include <linux/fiemap.h>\n'):
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
        header='\n'.join([
            '#include <sys/types.h>'
            '#include <sys/stat.h>'
            '#include <unistd.h>\n'
        ])
    ):
        have_stat64 = False

    rc = int(off_t_is_big_enough or have_stat64)
    conf.env['HAVE_BIG_OFF_T'] = int(off_t_is_big_enough)
    conf.env['HAVE_BIG_STAT'] = int(have_stat64)
    conf.env['HAVE_BIGFILES'] = rc

    context.did_show_result = True
    context.Result(rc)
    return rc


def check_getmntent(context):
    rc = 1
    if tests.CheckHeader(context, 'mntent.h'):
        rc = 0

    conf.env['HAVE_MNTENT'] = rc

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


def check_sse42(context):
    rc = 1
    if tests.CheckDeclaration(context, '__SSE4_2__'):
        rc = 0
    else:
        conf.env.Prepend(CFLAGS=['-msse4.2'])

    conf.env['HAVE_SSE42'] = rc

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
    for key, value in COLORS.iteritems():
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
    '--prefix', default='/usr',
    dest='prefix', type='string', nargs=1,
    action='store', metavar='DIR', help='installation prefix'
)

AddOption(
    '--actual-prefix', default=None,
    dest='actual_prefix', type='string', nargs=1,
    action='store', metavar='DIR', help='where files will eventually land'
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
    ENV={
        'PATH': os.environ['PATH'],
        'TERM': os.environ['TERM'],
        'HOME': os.environ['HOME']
    }
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
    'check_pkgconfig': check_pkgconfig,
    'check_pkg': check_pkg,
    'check_git_rev': check_git_rev,
    'check_libelf': check_libelf,
    'check_fiemap': check_fiemap,
    'check_sse42': check_sse42,
    'check_sha512': check_sha512,
    'check_getmntent': check_getmntent,
    'check_bigfiles': check_bigfiles,
    'check_gettext': check_gettext
})

if not conf.CheckCC():
    print('Error: Your compiler and/or environment is not correctly configured.')
    Exit(1)

conf.check_git_rev()
conf.check_pkgconfig('0.15.0')

# Pkg-config to internal name
DEPS = {
    'glib-2.0 >= 2.32': 'HAVE_GLIB',
    'blkid': 'HAVE_BLKID'
}

for pkg, name in DEPS.items():
    conf.check_pkg(pkg, name)

packages = []
for pkg in DEPS.keys():
    packages.append(pkg.split()[0])

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

conf.check_sse42()
conf.env.Append(CCFLAGS=[
    '-std=c99', '-pipe', '-fPIC', '-D_GNU_SOURCE'
])


if ARGUMENTS.get('DEBUG') == "1":
    conf.env.Append(CCFLAGS=['-ggdb3'])
else:
    # Generic compiler:
    conf.env.Append(CCFLAGS=['-Os'])
    conf.env.Append(LINKFLAGS=['-s'])

if 'gcc' in os.path.basename(conf.env['CC']):
    # GCC-Specific Options.
    conf.env.Append(CCFLAGS=['-lto'])
    conf.env.Append(CCFLAGS=['-march=native'])
elif 'clang' in os.path.basename(conf.env['CC']):
    conf.env.Append(CCFLAGS=['-fcolor-diagnostics'])  # Colored warnings
    conf.env.Append(CCFLAGS=['-Qunused-arguments'])   # Hide wrong messages


# Optional flags:
conf.env.Append(CFLAGS=[
    '-Wall', '-W', '-Wextra',
    '-Winit-self',
    '-Wstrict-aliasing',
    '-Wmissing-include-dirs',
    '-Wuninitialized',
    '-Wstrict-prototypes',
])

env.ParseConfig('pkg-config --cflags --libs ' + ' '.join(packages))

conf.env.Append(_LIBFLAGS=[
    '-lm', '-lelf'
])

conf.check_libelf()
conf.check_fiemap()
conf.check_bigfiles()
conf.check_sha512()
conf.check_getmntent()
conf.check_gettext()

# Your extra checks here
env = conf.Finish()

program = SConscript('src/SConscript')
SConscript('tests/SConscript', exports='program')
SConscript('po/SConscript')
SConscript('docs/SConscript')

if 'config' in COMMAND_LINE_TARGETS:
    def print_config(target=None, source=None, env=None):
        yesno = lambda boolean: COLORS['green'] + 'yes' + COLORS['end'] if boolean else COLORS['red'] + 'no' + COLORS['end']

        sphinx_bin = find_sphinx_binary()

        print('''
{grey}rmlint will be compiled with the following features:{end}

    Find non-stripped binaries (needs libelf)         : {libelf}
    Optimize using ioctl(FS_IOC_FIEMAP) (needs linux) : {fiemap}
    Support for SHA512 (needs glib >= 2.31)           : {sha512}
    Support for SSE4.2 instructions for fast CityHash : {sse42}
    Build manpage from docs/rmlint.1.rst              : {sphinx}
    Checking for proper support of big files >= 4GB   : {bigfiles}
        (needs either sizeof(off_t) >= 8 ...)         : {bigofft}
        (... or presence of stat64)                   : {bigstat}

    Optimize non-rotational disks                     : {nonrotational}
        (needs libblkid for resolving dev_t to path)  : {blkid}
        (needs <mntent.h> for listing all mounts)     : {mntent}

    Enable gettext localization                       : {gettext}
        (needs <locale.h> for compile side support)   : {locale}
        (needs msgfmt to compile .po files)           : {msgfmt}

{grey}The following constants will be used during the build:{end}

    Version information  : {version}
    Compiler             : {compiler}
    Install prefix       : {prefix}
    Actual prefix        : {actual_prefix}
    Verbose building     : {verbose}
    Adding debug symbols : {debug}

Type 'scons' to actually compile rmlint now. Good luck.
    '''.format(
            grey=COLORS['grey'], end=COLORS['end'],
            libelf=yesno(env['HAVE_LIBELF']),
            gettext=yesno(env['HAVE_GETTEXT']),
            locale=yesno(env['HAVE_LIBINTL']),
            msgfmt=yesno(env['HAVE_MSGFMT']),
            nonrotational=yesno(env['HAVE_BLKID'] and env['HAVE_MNTENT']),
            blkid=yesno(env['HAVE_BLKID']),
            mntent=yesno(env['HAVE_MNTENT']),
            fiemap=yesno(env['HAVE_FIEMAP']),
            sha512=yesno(env['HAVE_SHA512']),
            sse42=yesno(env['HAVE_SSE42']),
            bigfiles=yesno(env['HAVE_BIGFILES']),
            bigofft=yesno(env['HAVE_BIG_OFF_T']),
            bigstat=yesno(env['HAVE_BIG_STAT']),
            sphinx=COLORS['green'] + 'yes, using ' + COLORS['end'] + sphinx_bin if sphinx_bin else yesno(sphinx_bin),
            compiler=env['CC'],
            prefix=GetOption('prefix'),
            actual_prefix=GetOption('actual_prefix') or GetOption('prefix'),
            verbose=yesno(ARGUMENTS.get('VERBOSE')),
            debug=yesno(ARGUMENTS.get('DEBUG')),
            version='{a}.{b}.{c} "{n}" (rev {r})'.format(
                a=VERSION_MAJOR, b=VERSION_MINOR, c=VERSION_PATCH,
                n=VERSION_NAME, r=env.get('gitrev', 'unknown')
            )
        ))

    env.Command('config', None, Action(print_config, "Printing configuration..."))
