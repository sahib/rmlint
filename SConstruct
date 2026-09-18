import os
import shlex
import subprocess
from pathlib import Path

import SCons.Action
import SCons.SConf
from SCons.Script import *
from SCons.Script.SConscript import SConsEnvironment
from SCons.Errors import UserError

from rm_build_checks import CUSTOM_TESTS
from rm_build_support import (
    COLORS,
    OPTIONAL_FLAGS,
    PKG_CONFIG,
    InstallPerm,
    collect_clang_flags,
    compile_shared_source_message,
    compile_source_message,
    create_uninstall_target,
    find_sphinx_binary,
    get_cpu_count,
    link_library_message,
    link_program_message,
    link_shared_library_message,
    ranlib_library_message,
    write_compile_flags,
)
from rm_version import VersionError, read_version

DEFAULT_PREFIX = '/usr/local'
PREFIX_RECORD_FILE = Path('.prefix.txt')


try:
    VERSION = read_version()
except (OSError, VersionError) as err:
    raise UserError(err) from err

Export('VERSION')
Export('create_uninstall_target')
Export('find_sphinx_binary')

# put this function "in" scons
SConsEnvironment.InstallPerm = InstallPerm

#==============================================================================#
#                                Option Parsing                                #
#==============================================================================#

AddOption(
    '--show-config', default=False,
    dest='show_config', action='store_true',
    help='print the detected feature summary before building (-n to stop there)'
)

AddOption(
    '--allow-externally-managed',
    action='store_true',
    dest='allow_externally_managed',
    help='Allow Shredder installation into an externally managed Python environment',
)

for suffix in OPTIONAL_FLAGS:
    AddOption(
        f'--without-{suffix}', action='store_false',
        dest=f'with_{suffix}', default=True,
    )

#==============================================================================#
#                                  Prefix(es)                                  #
#==============================================================================#

def get_default_prefix():
    if 'uninstall' in COMMAND_LINE_TARGETS:
        try:
            prefix = PREFIX_RECORD_FILE.read_text(encoding='utf-8')
            print(f'===> Using cached installation prefix "{prefix}"')
            return prefix
        except OSError as err:
            print(f'===> Failed to get cached installation prefix: {err}')
    return DEFAULT_PREFIX

vars = Variables()

vars.Add(PathVariable(
    'PREFIX',
    help='installation prefix',
    default=get_default_prefix(),
    validator=PathVariable.PathIsDir,
))

vars.Add(PathVariable(
    'DESTDIR',
    help='installation staging directory',
    default='',
    validator=PathVariable.PathAccept,
))

vars.Add(
    'LIBDIR',
    help='library installation directory, relative to PREFIX (lib, lib64, etc)',
    default='lib',
    validator=lambda _key, value, _env: not Path(value).is_absolute(),
)

#==============================================================================#
#                                  Variables                                   #
#==============================================================================#

vars.Add(BoolVariable(
    'VERBOSE',
    help='print compiler and linker command lines and Glib depreciations',
    default=False,
))

vars.Add(BoolVariable(
    'DEBUG',
    help='enable run-time assertions and extra checks',
    default=False,
))

vars.Add(BoolVariable(
    'SYMBOLS',
    help='compile with debugging symbols (-g3)',
    default=False,
))

vars.Add(BoolVariable(
    'STRIP',
    help='strip symbols',
    default=False,
))

O_DEBUG   = 'g' # The optimisation level for a debug build
O_RELEASE = '2' # The optimisation level for a release build
vars.Add(
    'O',
    help=f"optimisation level; special values are 'debug' (-{O_DEBUG}) "
         f"and 'release' (-{O_RELEASE}). Empty picks the default depending "
         "on DEBUG=.",
    default='',
    converter= lambda o: {'debug': O_DEBUG, 'release': O_RELEASE}.get(o, o),
)

SANITISE_TRUE=('address', 'undefined', 'leak')
def shorthand_sanitisers(value):
    match value:
        case '1' | 'yes' | 'true':
            return SANITISE_TRUE
        case '' | '0' | 'no' | 'false' | 'none':
            return ()
        case _:
            return tuple(dict.fromkeys(value.lower().replace(',', ' ').split()))

vars.Add(
    'SANITISE',
    help="sanitisers to compile with, "
         f"1 is shorthand for {','.join(SANITISE_TRUE)}",
    default='',
    converter=shorthand_sanitisers,
)

vars.Add(BoolVariable(
    'FORCE',
    help='keep building when the compiler warns (drops -Werror)',
    default=False,
))

vars.Add(
    'PYTEST_ARGS',
    help="extra pytest arguments",
    default='',
)

# General Environment
options = {
    'CXXCOMSTR': compile_source_message,
    'CCCOMSTR': compile_source_message,
    'SHCCCOMSTR': compile_shared_source_message,
    'SHCXXCOMSTR': compile_shared_source_message,
    'ARCOMSTR': link_library_message,
    'RANLIBCOMSTR': ranlib_library_message,
    'SHLINKCOMSTR': link_shared_library_message,
    'LINKCOMSTR': link_program_message,
    'ENV': {
        key: os.environ[key]
        for key in ('PATH', 'TERM', 'HOME', 'PKG_CONFIG_PATH',
                    'SOURCE_DATE_EPOCH')
        if key in os.environ
    } | {'TZ': 'UTC', 'LANG': 'C.UTF-8', 'LC_ALL': 'C.UTF-8'},
}

#==============================================================================#
#                                 Environment                                  #
#==============================================================================#

# Actually instance the Environment with all collected information:
env = Environment(variables=vars, **options)
Help(vars.GenerateHelpText(env))

env['PREFIX'] = Path(env['PREFIX'])
if env['DESTDIR']:
    env['DESTDIR'] = Path(env['DESTDIR'])
    if not env['PREFIX'].is_absolute():
        raise UserError('PREFIX must be an absolute path when DESTDIR is specified')

env['staged_prefix'] = (
    env['DESTDIR'] / env['PREFIX'].relative_to('/')
    if env['DESTDIR']
    else env['PREFIX']
)

if env['VERBOSE']:
    del env['CCCOMSTR']
    del env['LINKCOMSTR']

if env['STRIP'] and env['SYMBOLS']:
    raise UserError('STRIP and SYMBOLS are incompatible options')

#==============================================================================#

# XXX: install-lib is a special case.
installing = bool({'install', 'install-cli', 'install-gui'} & set(COMMAND_LINE_TARGETS))
Export('installing')

if installing and not env['DESTDIR']:
    # record the installation prefix for later uninstall
    PREFIX_RECORD_FILE.write_text(str(env['PREFIX']), encoding='utf-8')

# Configuration:
sc_noexec = None
if SCons.SConf.dryrun and GetOption('show_config'):
    # let configuration phase run with --show-config -n
    sc_noexec = SCons.SConf.dryrun, SCons.Action.execute_actions
    SCons.SConf.dryrun = 0
    SCons.Action.execute_actions = 1

conf = Configure(env, custom_tests=CUSTOM_TESTS)

#==============================================================================#
#                          Compiler Checks and Flags                           #
#==============================================================================#

if 'CC' in os.environ:
    conf.env.Replace(CC=os.environ['CC'])
    print(">> Using compiler: " + os.environ['CC'])

ENV_FLAGS = {
    'CCFLAGS': shlex.split(os.environ.get('CFLAGS', '')),
    'LINKFLAGS': shlex.split(os.environ.get('LDFLAGS', '')),
}

def merge_env_flags(msg=False):
    for key, flags in ENV_FLAGS.items():
        if flags:
            conf.env.MergeFlags({key: flags})
            if msg:
                print(f"Merging custom flags into {key}: {' '.join(flags)}")

# first pass: use environement flags for our checks
merge_env_flags(msg=True)

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
conf.check_pkg('glib-2.0 >= 2.74', 'HAVE_GLIB', required=True)

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

# TODO: migrate _GNU_SOURCE to narrower _DEFAULT_SOURCE
conf.env.Append(CCFLAGS=[
    '-std=c17', '-pipe', '-D_GNU_SOURCE', '-D_FILE_OFFSET_BITS=64'
])

conf.check_target_platform()

if conf.env['IS_CYGWIN']:
    conf.env.Append(CCFLAGS=['-U__STRICT_ANSI__'])
else:
    conf.env.Append(CCFLAGS=['-fPIC'])

# check _mm_crc32_u64 (SSE4.2) support:
conf.check_mm_crc32_u64()

#==============================================================================#
#                               Compiler options                               #
#==============================================================================#

conf.env['IS_CLANG'] = conf.CheckDeclaration("__clang__")

if conf.env['IS_CLANG']:
    conf.env.Append(CCFLAGS=['-fcolor-diagnostics'])  # Colored warnings
    conf.env.Append(CCFLAGS=['-Qunused-arguments'])   # Hide wrong messages
    conf.env.Append(CCFLAGS=[
        '-Wmost',
        '-Wunreachable-code-aggressive',
    ])
else:
    conf.env.Append(CCFLAGS=[
        '-Wduplicated-cond',
        '-Wduplicated-branches',
        '-Wlogical-op',
    ])

# Optional flags:
conf.env.Append(CCFLAGS=[
    '-Wall', '-Wextra',
    '-Winit-self',
    '-Wmissing-include-dirs',
    '-Wuninitialized',
    '-Wstrict-prototypes',
    '-Wnull-dereference',
    '-Wformat-security',
    '-Wformat-y2k',
    '-Wmissing-noreturn',
    '-Wno-implicit-fallthrough',
    '-Wdeprecated-declarations',
    '-Wundef',
])


conf.env.ParseConfig(PKG_CONFIG + ' --cflags --libs ' + ' '.join(packages))
conf.env.Append(_LIBFLAGS=['-lm'])

conf.check_builtin_cpu_supports()
conf.check_blkid()
conf.check_sys_block()
conf.check_libelf()
conf.check_fiemap()
conf.check_xattr()
conf.check_lxattr()
conf.check_extattr()
conf.check_gettext()
conf.check_linux_limits()
conf.check_posix_fadvise()
conf.check_btrfs_h()
conf.check_linux_fs_h()
conf.check_uname()
conf.check_sysmacro_h()
conf.check_c23_embed('lib/formats/sh.sh')

if conf.env['HAVE_LIBELF']:
    conf.env.Append(_LIBFLAGS=['-lelf'])

# NB: After checks so they don't fail
if not conf.env['FORCE']:
    conf.env.Append(CCFLAGS=['-Werror'])

# XXX: after -Werror
if not conf.env['VERBOSE']:
    conf.env.Append(CCFLAGS=[
        '-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_74',
        '-DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_74',
    ])
else:
    conf.env.Append(CCFLAGS=['-Wno-error=deprecated-declarations'])


# build modes
if conf.env['DEBUG']:
    print("Compiling in debug mode")
    conf.env.Append(CCFLAGS=['-DRM_DEBUG', '-fno-inline'])
else:
    conf.env.Append(CCFLAGS=['-DG_DISABLE_ASSERT', '-DNDEBUG'])

cc_O_option = '-O' + (conf.env['O'] or
                      (O_DEBUG if conf.env['DEBUG'] else O_RELEASE))

print(f"Using compiler optimisation {cc_O_option} "
      f"(to change, run scons with O=<level>, or O=(release|debug)")
conf.env.Append(CCFLAGS=[cc_O_option])

if conf.env['SYMBOLS']:
    print("Compiling with debugging symbols")
    conf.env.Append(CCFLAGS='-g3')

if sanitisers := conf.env['SANITISE']:
    fsan = '-fsanitize=' + ','.join(sanitisers)
    print('Compiling with sanitisers: ' + ', '.join(sanitisers))
    conf.env.Append(CCFLAGS=[fsan, '-fno-omit-frame-pointer'])
    conf.env.Append(LINKFLAGS=[fsan])
    if not conf.env['SYMBOLS']:  # SYMBOLS=1 already added -g3
        conf.env.Append(CCFLAGS=['-g'])

# symbol stripping
if conf.env['STRIP'] and not conf.env['IS_APPLE']:
    conf.env.Append(LINKFLAGS=['-s'])

# second pass: move the environment flags after ours
merge_env_flags()

value = ARGUMENTS.get('CCFLAGS')
if value:
    print("Appending custom build flags provided on command line: " + value)
    conf.env.Append(CCFLAGS=shlex.split(value))

# Your extra checks here
env = conf.Finish()

# restore -n after config
if sc_noexec:
    SCons.SConf.dryrun, SCons.Action.execute_actions = sc_noexec

Export('env')

# snapshot the compile flags before we add host-specific flags
# for vendored libraries, as well as the compilation database records.
CLANG_FLAGS = collect_clang_flags(env)
env.Tool('compilation_db')

# set number of parallel jobs during build
# note: while not particularly intuitive or obvious from the documentation,
# SetOption() will *not* over-ride commandline option passed by `scons -j<n>`
# or `scons --jobs=<n>`
SetOption('num_jobs', get_cpu_count())

print(f"Running with --jobs={GetOption('num_jobs')}")

library = SConscript('lib/SConscript')
program = SConscript('src/SConscript', exports='library')
env.Default(library)

if conf.env['STRIP'] and conf.env['IS_APPLE']:
    env.AddPostAction(program, Action('strip $TARGET', 'Stripping $TARGET'))

SConscript('tests/SConscript', exports='program')
SConscript('po/SConscript')
SConscript('docs/SConscript')
if GetOption('with_gui'):
    SConscript('gui/SConscript')

#==============================================================================#
#                                Clang tooling                                 #
#==============================================================================#

cdb = env.CompilationDatabase()
env.Depends(cdb, 'lib/config.h')
env.Alias('cdb', cdb)

compile_flags = env.Command(
    'compile_flags.txt', env.Value(CLANG_FLAGS),
    Action(write_compile_flags, "Generating $TARGET and .clang_complete")
)
env.Depends(compile_flags, 'lib/config.h')
env.Alias('compile-flags', compile_flags)
env.Clean(compile_flags, '.clang_complete')

env.Clean(library, ('compile_commands.json', 'compile_flags.txt', '.clang_complete'))


def build_tar_gz(target, source, env):
    tarball = f'rmlint-{VERSION}.tar.gz'
    subprocess.call(['git', 'archive', 'HEAD', '-9', '--format', 'tar.gz', '-o', tarball])
    print('Wrote tarball to ./' + tarball)

if 'dist' in COMMAND_LINE_TARGETS:
    env.Command('dist', None, Action(build_tar_gz, "Building release tarball..."))


if GetOption('show_config'):
    def color(text, colour):
        return COLORS[colour] + text + COLORS['end']

    yesno = lambda b: color('yes' if b else 'no', 'green' if b else 'red')

    sphinx_bin = find_sphinx_binary()

    print('''
{grey}rmlint will be compiled with the following features:{end}

    Find non-stripped binaries (needs libelf)             : {libelf}
    Optimize using ioctl(FS_IOC_FIEMAP) (needs linux)     : {fiemap}
    Metro SSE4.2 dispatch                                 : {crc_dispatch}
    BLAKE3 uses SIMD...
        ...x86_64 assembly                                : {blake3_simd_asm}
        ...x86 C intrinsics                               : {blake3_simd_c}
        ...AArch64 NEON                                   : {blake3_simd_neon}
    Build manpage from docs/rmlint.1.rst                  : {sphinx}
    Support for caching checksums in file's xattr         : {xattr}

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
    Staging directory    : {dest_dir}
    Staged prefix        : {staged_prefix}
    Optimisation level   : {optimisation}
    Verbose building     : {verbose}
    Adding debug checks  : {debug}
    Adding debug symbols : {symbols}
    Active sanitisers    : {sanitisers}
    Stripping symbols    : {strip}
    Compile Glib schemas : {compile_glib_schemas}
{trailer}    '''.format(
        grey=COLORS['grey'], end=COLORS['end'],

        libelf=yesno(env['HAVE_LIBELF']),
        fiemap=yesno(env['HAVE_FIEMAP']),
        crc_dispatch=yesno(env['HAVE_BUILTIN_CPU_SUPPORTS'] & env['HAVE_MM_CRC32_U64']),

        blake3_simd_asm=yesno(env['IS_X86_64']),
        blake3_simd_c=yesno(env['IS_X86'] and not env['IS_X86_64']),
        blake3_simd_neon=yesno(env['IS_AARCH64_LE']),

        sphinx = f"{color('yes, using', 'green')}, {sphinx_bin}" if sphinx_bin else yesno(sphinx_bin),
        xattr=yesno(env['HAVE_XATTR'] or env['HAVE_EXTATTR']),

        nonrotational=yesno(env['HAVE_GIO_UNIX'] & env['HAVE_BLKID']),
        blkid=yesno(env['HAVE_BLKID']),
        gio_unix=yesno(env['HAVE_GIO_UNIX']),

        gettext=yesno(env['HAVE_GETTEXT']),
        locale=yesno(env['HAVE_LIBINTL']),
        msgfmt=yesno(env['HAVE_MSGFMT']),

        version=f'{VERSION.with_rev(env['gitrev'])} "{VERSION.name}"',
        compiler=env['CC'],
        prefix=env['PREFIX'],
        dest_dir=env['DESTDIR'],
        staged_prefix=env['staged_prefix'],
        optimisation=cc_O_option,
        verbose=yesno(env['VERBOSE']),
        debug=yesno(env['DEBUG']),
        symbols=yesno(env['SYMBOLS']),
        sanitisers = color(', '.join(sanitisers), 'green') if sanitisers else color('none', 'red'),
        strip=yesno(env['STRIP']),
        compile_glib_schemas=yesno(GetOption('with_compile-glib-schemas')),

        trailer="\nType 'scons' to actually compile rmlint now. Good luck.\n"
                if GetOption('no_exec') else ''
    ))
