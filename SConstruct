import os
import shlex
import subprocess

from SCons.Script import *
from SCons.Script.SConscript import SConsEnvironment

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
    read_version,
    write_compile_flags,
)

DEFAULT_PREFIX = '/usr'
PREFIX_RECORD_FILE = '.prefix.txt'

VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_NAME, _ = read_version()
Export('VERSION_MAJOR VERSION_MINOR VERSION_PATCH VERSION_NAME')
Export('create_uninstall_target')
Export('find_sphinx_binary')

# put this function "in" scons
SConsEnvironment.InstallPerm = InstallPerm

###########################################################################
#                            Option Parsing                               #
###########################################################################

def get_default_prefix():
    if 'uninstall' in COMMAND_LINE_TARGETS:
        try:
            with open(PREFIX_RECORD_FILE, 'r', encoding='utf-8') as handle:
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
    with open(PREFIX_RECORD_FILE, 'w', encoding='utf-8') as f:
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

###########################################################################
#                           Dependency Checks                             #
###########################################################################

# Configuration:
conf = Configure(env, custom_tests=CUSTOM_TESTS)

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

if ARGUMENTS.get('VERBOSE') != "1":
    conf.env.Append(CCFLAGS=[
        '-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_64',
        '-DGLIB_VERSION_MAX_REQUIRED=GLIB_VERSION_2_64',
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

conf.env.Append(CCFLAGS=[
    '-std=c17', '-pipe', '-D_GNU_SOURCE'
])

# Support cygwin:
conf.check_cygwin()
if conf.env['IS_CYGWIN']:
    conf.env.Append(CCFLAGS=['-U__STRICT_ANSI__'])
else:
    conf.env.Append(CCFLAGS=['-fPIC'])

# check _mm_crc32_u64 (SSE4.2) support:
conf.check_mm_crc32_u64()

if IS_CLANG := conf.CheckDeclaration("__clang__"):
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
    '-Wdeprecated-declarations',
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
conf.check_bigfiles()
conf.check_gettext()
conf.check_linux_limits()
conf.check_posix_fadvise()
conf.check_btrfs_h()
conf.check_linux_fs_h()
conf.check_uname()
conf.check_sysmacro_h()
conf.check_target_arch()
conf.check_c23_embed('lib/formats/sh.sh')

if conf.env['HAVE_LIBELF']:
    conf.env.Append(_LIBFLAGS=['-lelf'])

# NB: After checks so they don't fail
conf.env.Append(CCFLAGS=['-Werror=undef'])


if ARGUMENTS.get('GDB') == '1':
    ARGUMENTS['DEBUG'] = '1'
    ARGUMENTS['SYMBOLS'] = '1'

# sanitisers
SANITISERS_EXCLUSIVE  = ['address', 'thread', 'memory']
SANITISERS_CLANG_ONLY = ['memory']

sanitise_arg = ARGUMENTS.get('SANITISE', '')
if sanitise_arg == '1':
    sanitisers = ['address', 'undefined']
else:
    sanitisers = [t.strip().lower()
                  for t in sanitise_arg.replace(',', ' ').split() if t.strip()]

deduped = []
for s in sanitisers:
    if s not in deduped:
        deduped.append(s)
sanitisers = deduped

needs_clang = [s for s in sanitisers if s in SANITISERS_CLANG_ONLY]
if needs_clang and not IS_CLANG:
    print(f"Error: sanitiser(s) {', '.join(needs_clang)} require clang; "
          f"re-run with CC=clang.")
    Exit(1)

exclusive = [s for s in sanitisers if s in SANITISERS_EXCLUSIVE]
if len(exclusive) > 1:
    print(f"Error: sanitisers {', '.join(exclusive)} cannot be combined; "
          f"pick one of address/thread/memory.")
    Exit(1)

O_DEBUG   = 'g' # The optimisation level for a debug   build
O_RELEASE = '2' # The optimisation level for a release build

# build modes
if ARGUMENTS.get('DEBUG') == "1":
    print("Compiling in debug mode")
    conf.env.Append(CCFLAGS=['-DRM_DEBUG', '-fno-inline'])
    O_value = ARGUMENTS.get('O', O_DEBUG)
else:
    conf.env.Append(CCFLAGS=['-DG_DISABLE_ASSERT', '-DNDEBUG'])
    O_value = ARGUMENTS.get('O', O_RELEASE)

if O_value == 'debug':
    O_value = O_DEBUG
elif O_value == 'release':
    O_value = O_RELEASE

cc_O_option = '-O' + O_value

print(f"Using compiler optimisation {cc_O_option} (to change, run scons with O=[0|1|2|3|s|fast])")
conf.env.Append(CCFLAGS=[cc_O_option])

if ARGUMENTS.get('SYMBOLS') == '1':
    print("Compiling with debugging symbols")
    conf.env.Append(CCFLAGS='-g3')

if sanitisers:
    fsan = '-fsanitize=' + ','.join(sanitisers)
    print('Compiling with sanitisers: ' + ', '.join(sanitisers))
    conf.env.Append(CCFLAGS=[fsan, '-fno-omit-frame-pointer'])
    conf.env.Append(LINKFLAGS=[fsan])
    if ARGUMENTS.get('SYMBOLS') != '1':   # SYMBOLS=1 already added -g3
        conf.env.Append(CCFLAGS=['-g'])

# symbol stripping
# Release strips by default, use STRIP=0 to ship a separate debuginfo package.
if (strip_arg := ARGUMENTS.get('STRIP')) is not None:
    if strip_arg not in ('0', '1'):
        print(f"Error: STRIP must be 0 or 1, got '{strip_arg}'.")
        Exit(1)
    strip = strip_arg == '1'
else:
    strip = ARGUMENTS.get('DEBUG') != '1' and not sanitisers

if strip:
    conf.env.Append(LINKFLAGS=['-s'])

value = ARGUMENTS.get('CCFLAGS')
if value:
    print("Appending custom build flags provided on command line: " + value)
    conf.env.Append(CCFLAGS=shlex.split(value))

# Your extra checks here
env = conf.Finish()
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
programs = SConscript('src/SConscript', exports='library')
env.Default(library)

SConscript('tests/SConscript', exports='programs')
SConscript('po/SConscript')
SConscript('docs/SConscript')
SConscript('gui/SConscript')


# clang tooling
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


def build_tar_gz(target=None, source=None, env=None):
    tarball = f'rmlint-{VERSION_MAJOR}.{VERSION_MINOR}.{VERSION_PATCH}.tar.gz'
    subprocess.call(['git', 'archive', 'HEAD', '-9', '--format', 'tar.gz', '-o', tarball])
    print('Wrote tarball to ./' + tarball)


if 'dist' in COMMAND_LINE_TARGETS:
    env.Command('dist', None, Action(build_tar_gz, "Building release tarball..."))


if 'release' in COMMAND_LINE_TARGETS:
    def replace_version_strings(target=None, source=None, env=None):
        print('Patching .version file...')
        with open('.version', 'r', encoding='utf-8') as handle:
            text = handle.read().strip()

        if '@' not in text:
            with open('.version', 'w', encoding='utf-8') as handle:
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
            with open('.version', 'w', encoding='utf-8') as handle:
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
    Metro SSE4.2 dispatch                                 : {crc_dispatch}
    blake3 uses x86 SIMD...
        ...assembly (x86_64 only)                         : {blake3_simd_asm}
        ...C intrinsics                                   : {blake3_simd_c}
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
    Active sanitisers    : {sanitisers}
    Stripping symbols    : {strip}
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
            crc_dispatch=yesno(env['HAVE_BUILTIN_CPU_SUPPORTS'] & env['HAVE_MM_CRC32_U64']),
            blake3_simd_asm=yesno(env['IS_X86_64']),
            blake3_simd_c=yesno(env['IS_X86'] and not env['IS_X86_64']),
            bigfiles=yesno(env['HAVE_BIGFILES']),
            bigofft=yesno(env['HAVE_BIG_OFF_T']),
            bigstat=yesno(env['HAVE_STAT64']),
            sphinx=COLORS['green'] + 'yes, using ' + COLORS['end'] + sphinx_bin if sphinx_bin else yesno(sphinx_bin),
            compiler=env['CC'],
            prefix=GetOption('prefix'),
            actual_prefix=GetOption('actual_prefix') or GetOption('prefix'),
            compile_glib_schemas=yesno(GetOption('with_compile-glib-schemas')),
            verbose=yesno(ARGUMENTS.get('VERBOSE') == '1'),
            debug=yesno(ARGUMENTS.get('DEBUG') == '1'),
            symbols=yesno(ARGUMENTS.get('SYMBOLS') == '1'),
            sanitisers=(COLORS['green'] + ', '.join(sanitisers) + COLORS['end'])
                       if sanitisers else (COLORS['red'] + 'none' + COLORS['end']),
            strip=yesno(strip),
            version=f'{VERSION_MAJOR}.{VERSION_MINOR}.{VERSION_PATCH} '
                    f'"{VERSION_NAME}" (rev {env.get("gitrev", "unknown")})'
        ))

    env.Command('config', None, Action(print_config, "Printing configuration..."))
