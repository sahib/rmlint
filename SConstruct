#!/usr/bin/env python
# encoding: utf-8

import os
import sys
import time
import codecs
import subprocess

VERSION_MAJOR = 2
VERSION_MINOR = 0
VERSION_PATCH = 0


def CheckPKGConfig(context, version):
    context.Message('Checking for pkg-config... ')
    command = 'pkg-config --atleast-pkgconfig-version=%s' % version
    ret = context.TryAction(command)[0]
    context.Result(ret)
    return ret


def CheckPKG(context, name, varname, required=True):
    context.Message('Checking for %s... ' % name)
    rc, text = context.TryAction('pkg-config --exists \'%s\'' % name)
    context.Result(rc)

    # Remember we have it:
    conf.env[varname] = True

    # 0 is defined as error by TryAction
    if rc is 0:
        print('Error: ' + name + ' not found.')
        if required:
            Exit(1)
    return rc, text


def CheckGitRev(context):
    context.Message('Checking for git revision... ')
    rev = subprocess.check_output('git log --pretty=format:"%h" -n 1', shell=True)
    context.Result(rev)
    conf.env['gitrev'] = rev
    return rev


def BuildConfigTemplate(target, source, env):
    with codecs.open(str(source[0]), 'r') as handle:
        text = handle.read()

    with codecs.open(str(target[0]), 'w') as handle:
        handle.write(text.format(
            HAVE_GLIB=int(conf.env['glib']),
            HAVE_BLKID=int(conf.env['blkid']),
            HAVE_MOUNTLIST=int(conf.env['mountlist']),
            VERSION_MAJOR=VERSION_MAJOR,
            VERSION_MINOR=VERSION_MINOR,
            VERSION_PATCH=VERSION_PATCH,
            VERSION_GIT_REVISION=env['gitrev']
        ))


def create_uninstall_target(env, path):
    env.Command("uninstall-" + path, path, [
        Delete("$SOURCE"),
    ])
    env.Alias("uninstall", "uninstall-" + path)

###########################################################################
#                                 Colors!                                 #
###########################################################################

colors = {
    'cyan': '\033[96m',
    'purple': '\033[95m',
    'blue': '\033[94m',
    'green': '\033[92m',
    'yellow': '\033[93m',
    'red': '\033[91m',
    'end': '\033[0m'
}

# If the output is not a terminal, remove the colors
if not sys.stdout.isatty():
    for key, value in colors.iteritems():
        colors[key] = ''

compile_source_message = '%sCompiling %s==> %s$SOURCE%s' % \
    (colors['blue'], colors['purple'], colors['yellow'], colors['end'])

compile_shared_source_message = '%sCompiling shared %s==> %s$SOURCE%s' % \
    (colors['blue'], colors['purple'], colors['yellow'], colors['end'])

link_program_message = '%sLinking Program %s==> %s$TARGET%s' % \
    (colors['red'], colors['purple'], colors['yellow'], colors['end'])

link_library_message = '%sLinking Static Library %s==> %s$TARGET%s' % \
    (colors['red'], colors['purple'], colors['yellow'], colors['end'])

ranlib_library_message = '%sRanlib Library %s==> %s$TARGET%s' % \
    (colors['red'], colors['purple'], colors['yellow'], colors['end'])

link_shared_library_message = '%sLinking Shared Library %s==> %s$TARGET%s' % \
    (colors['red'], colors['purple'], colors['yellow'], colors['end'])


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
    ENV={
        'PATH': os.environ['PATH'],
        'TERM': os.environ['TERM'],
        'HOME': os.environ['HOME']
    }
)

if ARGUMENTS.get('VERBOSE') == "1":
    del options['CCCOMSTR']
    del options['LINKCOMSTR']

env = Environment(**options)

###########################################################################
#                              Actual Script                              #
###########################################################################

# Configuration:
conf = Configure(env, custom_tests={
    'CheckPKGConfig': CheckPKGConfig,
    'CheckPKG': CheckPKG,
    'CheckGitRev': CheckGitRev
})

if not conf.CheckCC():
    print('Error: Your compiler and/or environment is not correctly configured.')
    Exit(1)

conf.CheckGitRev()
conf.CheckPKGConfig('0.15.0')

# Pkg-config to internal name
DEPS = {
    'glib-2.0 >= 2.32': 'glib',
    'blkid': 'blkid',
    'libgtop-2.0': 'mountlist'
    # libelf has no .pc file :/
}

for pkg, name in DEPS.items():
    conf.CheckPKG(pkg, name)

packages = []
for pkg in DEPS.keys():
    packages.append(pkg.split()[0])


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
    '-std=c99', '-pipe', '-fPIC', '-g', '-D_GNU_SOURCE', '-pthread'
])

if ARGUMENTS.get('DEBUG') == "1":
    conf.env.Append(CCFLAGS=['-ggdb3'])
else:
    if conf.env['CC'] == 'gcc':
        # GCC-Specific Options.
        conf.env.Append(CCFLAGS=['-lto'])
        conf.env.Append(CCFLAGS=['-march=native', '-O3'])
    else:
        # Generic compiler:
        conf.env.Append(CCFLAGS=['-O3'])
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
    '-pthread', '-lm', '-lelf'
])

# Your extra checks here
env = conf.Finish()

env.AlwaysBuild(
    env.Command(
        'src/config.h', 'src/config.h.in', BuildConfigTemplate
    )
)


def TarFile(target, source, env):
    import tarfile
    tar = tarfile.open(str(target[0]), "w:gz")
    for item in source:
        tar.add(str(item))

    tar.close()


def BuildMan(target, source, env):
    rst_in_path = str(source[0])
    man_out_path = str(target[0])

    with open(rst_in_path, 'r') as handle:
        text = handle.read()
        text = text.format(
            VERSION='{}.{}.{} ({})'.format(
                VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, env['gitrev']
            ),
            DATE=time.strftime("%d-%m-%Y")
        )

    rst_meta_path = rst_in_path[:-4]
    with open(rst_meta_path, 'w') as handle:
        handle.write(text)

    os.system('rst2man "{s}" > "{t}"'.format(t=man_out_path, s=rst_meta_path))


env.AlwaysBuild(
    env.Alias('man',
        env.Command(
            'docs/rmlint.1', 'docs/rmlint.1.in.rst', BuildMan
        )
    )
)

manpage = env.Command(
    'docs/rmlint.1.gz', 'docs/rmlint.1', TarFile
)

env.AlwaysBuild(manpage)
program = env.Program('rmlint', Glob('src/*.c') + Glob('src/checksums/*.c'))


if 'install' in COMMAND_LINE_TARGETS:
    env.Install('/usr/bin', [program])
    env.Install('/usr/share/man/man1', [manpage])
    env.Alias('install', ['/usr/bin', '/usr/share/man/man1'])

if 'uninstall' in COMMAND_LINE_TARGETS:
    create_uninstall_target(env, "/usr/bin/rmlint")
    create_uninstall_target(env, '/usr/share/man/man1/rmlint.1.gz')
