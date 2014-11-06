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

    # Remember we have it:
    conf.env[varname] = True

    # 0 is defined as error by TryAction
    if rc is 0:
        print('Error: ' + name + ' not found.')
        if required:
            Exit(1)
    return rc, text


def check_git_rev(context):
    context.Message('Checking for git revision... ')
    rev = subprocess.check_output('git log --pretty=format:"%h" -n 1', shell=True)
    context.Result(rev)
    conf.env['gitrev'] = rev
    return rev


def build_config_template(target, source, env):
    with codecs.open(str(source[0]), 'r') as handle:
        text = handle.read()

    with codecs.open(str(target[0]), 'w') as handle:
        handle.write(text.format(
            INSTALL_PREFIX=GetOption('prefix'),
            HAVE_GLIB=int(conf.env['glib']),
            HAVE_BLKID=int(conf.env['blkid']),
            VERSION_MAJOR=VERSION_MAJOR,
            VERSION_MINOR=VERSION_MINOR,
            VERSION_PATCH=VERSION_PATCH,
            VERSION_GIT_REVISION=env['gitrev']
        ))

def build_python_formatter(target, source, env):
    with codecs.open(str(source[0]), 'r') as handle:
        text = handle.read()

    with codecs.open('src/formats/py.py', 'r') as handle:
        py_source = handle.read()

    # Prepare the Python source to be compatible with C-strings
    py_source = py_source.replace('"', '\\"')
    py_source = '\\n"\n"'.join(py_source.splitlines())

    with codecs.open(str(target[0]), 'w') as handle:
        handle.write(text.replace('<<PYTHON_SOURCE>>', py_source))


def create_uninstall_target(env, path):
    env.Command("uninstall-" + path, path, [
        Delete("$SOURCE"),
    ])
    env.Alias("uninstall", "uninstall-" + path)

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
    '-std=c99', '-pipe', '-fPIC', '-g', '-D_GNU_SOURCE'
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
    '-lm', '-lelf'
])

# Your extra checks here
env = conf.Finish()

###########################################################################
#                          Template Building                              #
###########################################################################

env.AlwaysBuild(
    env.Command(
        'src/config.h', 'src/config.h.in', build_config_template
    )
)

env.AlwaysBuild(
    env.Command(
        'src/formats/py.c', 'src/formats/py.c.in', build_python_formatter
    )
)


def tar_file(target, source, env):
    import tarfile
    tar = tarfile.open(str(target[0]), "w:gz")
    for item in source:
        tar.add(str(item))

    tar.close()


def build_man(target, source, env):
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
            'docs/rmlint.1', 'docs/rmlint.1.in.rst', build_man
        )
    )
)

manpage = env.Command(
    'docs/rmlint.1.gz', 'docs/rmlint.1', tar_file
)

env.AlwaysBuild(manpage)

###########################################################################
#                       Build of the actual Programs                      #
###########################################################################

program = env.Program(
    'rmlint',
    Glob('src/*.c') +
    Glob('src/checksums/*.c') +
    Glob('src/formats/*.c') +
    Glob('src/libart/*.c')
)

def run_tests(target = None, source = None, env = None) :
    Exit(subprocess.call('nosetests', env=env['ENV'], shell=True))


if 'test' in COMMAND_LINE_TARGETS:
    test_cmd = env.Command('run_tests', None, Action(run_tests, "Running tests"))
    env.Depends(test_cmd, [program])
    env.AlwaysBuild(test_cmd)
    env.Alias('test', test_cmd)


def xgettext(target=None, source=None, env=None):
    Exit(subprocess.call(
        'xgettext --package-name rmlint -k_ -kN_' \
        '--package-version 2.0.0 --default-domain rmlint' \
        '--output po/rmlint.pot' \
        '$(find src -iname "*.[ch]")',
        shell=True
    ))

if 'xgettext' in COMMAND_LINE_TARGETS:
    cmd = env.Command('xgettext', None, Action(xgettext, "Running xgettext"))

# gettext handling:
languages = []
install_paths = []
for src in env.Glob('po/*.po'):
    lng = os.path.basename(str(src)[:-3])
    dst = lng + '.mo'
    env.AlwaysBuild(
        env.Command(dst, src, 'msgfmt $SOURCE -o po/$TARGET')
    )

    path = '$PREFIX/share/locale/%s/LC_MESSAGES/rmlint.mo' % lng
    install_paths.append(path)
    env.InstallAs(path, os.path.join('po', dst))

if 'install' in COMMAND_LINE_TARGETS:
    env.Install('$PREFIX/bin', [program])
    env.Install('$PREFIX/share/man/man1', [manpage])
    env.Alias('install', ['$PREFIX/bin', '$PREFIX/share/man/man1'] + install_paths)

if 'uninstall' in COMMAND_LINE_TARGETS:
    create_uninstall_target(env, "$PREFIX/bin/rmlint")
    create_uninstall_target(env, '$PREFIX/share/man/man1/rmlint.1.gz')

    for lang_path in install_paths:
        create_uninstall_target(env, lang_path)
