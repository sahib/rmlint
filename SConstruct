#!/usr/bin/env python
# encoding: utf-8

import os
import sys
import subprocess

VERSION_MAJOR = 2
VERSION_MINOR = 0
VERSION_PATCH = 0
Export('VERSION_MAJOR VERSION_MINOR VERSION_PATCH')

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
    try:
        rev = subprocess.check_output('git log --pretty=format:"%h" -n 1', shell=True)
    except OSError:
        rev = 'unknown'
        print('Unable to find git revision.')

    context.Result(rev)
    conf.env['gitrev'] = rev
    return rev


def create_uninstall_target(env, path):
    env.Command("uninstall-" + path, path, [
        Delete("$SOURCE"),
    ])
    env.Alias("uninstall", "uninstall-" + path)

Export('create_uninstall_target')

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
    'end': '\033[0m'
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
    'check_git_rev': check_git_rev
})

if not conf.CheckCC():
    print('Error: Your compiler and/or environment is not correctly configured.')
    Exit(1)

conf.check_git_rev()
conf.check_pkgconfig('0.15.0')

# Pkg-config to internal name
DEPS = {
    'glib-2.0 >= 2.32': 'glib',
    'blkid': 'blkid'
    # libelf has no .pc file :/
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

# Needed/Adviceable flags:
conf.env.Append(CCFLAGS=[
    '-std=c99', '-pipe', '-fPIC', '-D_GNU_SOURCE'
])

if ARGUMENTS.get('DEBUG') == "1":
    conf.env.Append(CCFLAGS=['-ggdb3'])
else:
    if conf.env['CC'] == 'gcc':
        # GCC-Specific Options.
        conf.env.Append(CCFLAGS=['-lto'])
        conf.env.Append(CCFLAGS=['-march=native', '-Os'])
    else:
        # Generic compiler:
        conf.env.Append(CCFLAGS=['-Os'])
    conf.env.Append(LINKFLAGS=['-s'])

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

# Your extra checks here
env = conf.Finish()

program = SConscript('src/SConscript')
SConscript('tests/SConscript', exports='program')
SConscript('po/SConscript')
SConscript('docs/SConscript')
