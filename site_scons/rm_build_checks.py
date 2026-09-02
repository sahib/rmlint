"""Configure-time checks for the rmlint build."""
import os
import platform
import shutil
import subprocess
from textwrap import dedent

import SCons.Conftest as tests
from SCons.Script import ARGUMENTS, Exit, GetOption

from rm_build_support import OPTIONAL_FLAGS, PKG_CONFIG, read_version

STATIC_GIT_REV = read_version()[4]
CUSTOM_TESTS = {}  # Passed to Configure(); the SConstruct calls these as conf.<name>().


def custom_test(func):
    CUSTOM_TESTS[func.__name__] = func
    return func


@custom_test
def check_pkgconfig(context, version):
    context.Message('Checking for pkg-config... ')
    command = PKG_CONFIG + ' --atleast-pkgconfig-version=' + version
    rc, _ = context.TryAction(command)
    if not rc:
        print("Error: pkg-config not found (or too old).")
        Exit(1)
    context.Result(rc)
    return rc


@custom_test
def check_pkg(context, name, varname, required=True):
    package = name.split()[0]
    disabled = package in OPTIONAL_FLAGS and GetOption(f'with_{package}') is False

    if disabled:
        context.Message(f'Explicitly disabling {name}... ')
        rc, text = False, ''
    else:
        context.Message(f'Checking for {name}... ')
        # NOTE: TryAction returns 1 on success, 0 on failure
        rc, text = context.TryAction(f"{PKG_CONFIG} --exists '{name}'")

    context.sconf.env[varname] = int(rc)
    context.Result(rc)

    if required and not rc:
        Exit(f'Error: {name} is required but {"disabled" if disabled else "not found"}.')

    return rc, text


@custom_test
def check_git_rev(context):
    context.Message('Checking for git revision... ')
    rev = STATIC_GIT_REV

    try:
        rev = subprocess.check_output(
            ['git', 'log', '--pretty=format:%h', '-n', '1'],
            stderr=subprocess.DEVNULL,
        ).decode('ascii').strip()
    except (OSError, subprocess.CalledProcessError):
        # Not a git checkout or git unavailable.
        # Will use STATIC_GIT_REV from read_version().
        print('Unable to call git.')

    rev = rev or 'unknown'
    context.sconf.env['gitrev'] = rev
    context.Result(rev)
    return rev


@custom_test
def check_sysmacro_h(context):
    rc = 1
    if rc and tests.CheckHeader(context, 'sys/sysmacros.h'):
        rc = 0

    context.sconf.env['HAVE_SYSMACROS_H'] = rc
    context.did_show_result = True
    context.Result(rc)
    return rc


@custom_test
def check_libelf(context):
    rc = 1

    if GetOption('with_libelf') is False:
        rc = 0

    if rc and tests.CheckHeader(context, 'libelf.h', header="#include <stdlib.h>"):
        rc = 0

    if rc and tests.CheckLib(context, ['libelf']):
        rc = 0

    context.sconf.env['HAVE_LIBELF'] = rc

    context.did_show_result = True
    context.Result(rc)
    return rc


@custom_test
def check_uname(context):
    rc = 1

    if rc and tests.CheckHeader(context, 'sys/utsname.h', header=""):
        rc = 0

    context.sconf.env['HAVE_UNAME'] = rc

    context.did_show_result = True
    context.Result(rc)
    return rc


@custom_test
def check_gettext(context):
    env = context.sconf.env
    rc = 1

    if GetOption('with_gettext') is False:
        rc = 0

    if rc and tests.CheckHeader(context, 'locale.h'):
        rc = 0

    if rc and tests.CheckHeader(context, 'libintl.h'):
        rc = 0

    env['HAVE_LIBINTL'] = rc
    env['HAVE_MSGFMT'] = int(shutil.which('msgfmt') is not None)
    env['HAVE_GETTEXT'] = env['HAVE_MSGFMT'] and env['HAVE_LIBINTL']

    context.did_show_result = True
    context.Result(rc)
    return rc


@custom_test
def check_fiemap(context):
    rc = 1

    if GetOption('with_fiemap') is False:
        rc = 0

    if rc and tests.CheckType(context, 'struct fiemap', header='#include <linux/fiemap.h>'):
        rc = 0

    context.sconf.env['HAVE_FIEMAP'] = rc

    context.did_show_result = True
    context.Result(rc)
    return rc


@custom_test
def check_bigfiles(context):
    off_t_is_big_enough = True

    if tests.CheckTypeSize(context, 'off_t', header='#include <sys/types.h>') < 8:
        off_t_is_big_enough = False

    have_stat64 = True
    if tests.CheckFunc(context, 'stat64'):
        have_stat64 = False

    rc = int(off_t_is_big_enough or have_stat64)
    context.sconf.env['HAVE_BIG_OFF_T'] = int(off_t_is_big_enough)
    context.sconf.env['HAVE_STAT64'] = int(have_stat64)
    context.sconf.env['HAVE_BIGFILES'] = rc

    context.did_show_result = True
    context.Result(rc)
    return rc


@custom_test
def check_blkid(context):
    rc = 1

    if GetOption('with_blkid') is False:
        rc = 0

    if rc == 1 and tests.CheckDeclaration(
        context,
        symbol='blkid_devno_to_wholedisk',
        includes='#include <blkid.h>'
    ):
        rc = 0

    context.sconf.env['HAVE_BLKID'] = rc

    context.did_show_result = True
    context.Result(rc)
    return rc


@custom_test
def check_sys_block(context):
    rc = 1

    context.Message('Checking for existence of /sys/block... ')
    if not os.access('/sys/block', os.R_OK):
        rc = 0

    context.sconf.env['HAVE_SYSBLOCK'] = rc

    context.Result(rc)
    return rc


@custom_test
def check_posix_fadvise(context):
    rc = 1

    if tests.CheckDeclaration(
        context, 'posix_fadvise',
        includes='#include <fcntl.h>'
    ):
        rc = 0

    context.sconf.env['HAVE_POSIX_FADVISE'] = rc

    context.did_show_result = True
    context.Result(rc)
    return rc


@custom_test
def check_xattr(context):
    rc = 1

    if tests.CheckHeader(context, 'sys/xattr.h', header='#include <sys/types.h>'):
        rc = 0
    else:
        for func in ('getxattr', 'setxattr', 'removexattr', 'listxattr'):
            if tests.CheckFunc(context, func):
                rc = 0
                break

    context.sconf.env['HAVE_XATTR'] = rc

    context.did_show_result = True
    context.Result(rc)
    return rc


@custom_test
def check_lxattr(context):
    rc = 1

    if tests.CheckHeader(context, 'sys/xattr.h', header='#include <sys/types.h>'):
        rc = 0
    else:
        for func in ('lgetxattr', 'lsetxattr', 'lremovexattr', 'llistxattr'):
            if tests.CheckFunc(context, func):
                rc = 0
                break

    context.sconf.env['HAVE_LXATTR'] = rc

    context.did_show_result = True
    context.Result(rc)
    return rc


@custom_test
def check_extattr(context):
    """Check for BSD extended attributes support"""
    rc = 1

    if tests.CheckHeader(context, 'sys/extattr.h', header='#include <sys/types.h>'):
        rc = 0
    else:
        for func in ('extattr_get_file', 'extattr_set_file',
                     'extattr_list_file', 'extattr_delete_file',
                     'extattr_get_link', 'extattr_set_link',
                     'extattr_list_link', 'extattr_delete_link'):
            if tests.CheckFunc(context, func):
                rc = 0
                break

    context.sconf.env['HAVE_EXTATTR'] = rc

    context.did_show_result = True
    context.Result(rc)
    return rc


@custom_test
def check_btrfs_h(context):
    rc = 1
    if tests.CheckHeader(
        context, 'linux/btrfs.h',
        header='#include <stdlib.h>\n#include <sys/ioctl.h>'
    ):
        rc = 0

    context.sconf.env['HAVE_BTRFS_H'] = rc
    context.did_show_result = True
    context.Result(rc)
    return rc


@custom_test
def check_linux_fs_h(context):
    rc = 1
    if tests.CheckHeader(context, 'linux/fs.h'):
        rc = 0

    context.sconf.env['HAVE_LINUX_FS_H'] = rc
    context.did_show_result = True
    context.Result(rc)
    return rc


@custom_test
def check_linux_limits(context):
    rc = 1
    if tests.CheckHeader(context, 'linux/limits.h'):
        rc = 0

    context.sconf.env['HAVE_LINUX_LIMITS'] = rc
    context.did_show_result = True
    context.Result(rc)
    return rc


@custom_test
def check_c23_embed(context, payload):
    """Probe whether the compiler can #embed a repo-relative file.

    TryCompile builds inside .sconf_temp/, so the path must be made
    absolute. -pedantic-errors is to force GCC and Clang to reject
    #embed if c23 is not specified.
    """
    context.Message('Checking for C23 #embed support...')

    C23_EMBED_PROBE = dedent('''\
    #if !defined(__has_embed)
    #error "no __has_embed"
    #endif
    static const char src[] = {{
    #embed "{payload}"
    	, 0x00
    }};
    const char *get_src(void) {{ return src; }}
    ''')

    rc = 0
    if ARGUMENTS.get('C23_EMBED') != '0':
        saved = context.env['CCFLAGS']
        context.env.Replace(CCFLAGS=[
            flag for flag in saved if not str(flag).startswith('-std=')
        ] + ['-std=c23', '-pedantic-errors'])
        rc = int(bool(context.TryCompile(
            C23_EMBED_PROBE.format(
                payload=context.env.File(f'#{payload}').abspath
            ), '.c'
        )))
        context.env.Replace(CCFLAGS=saved)

    context.sconf.env['HAVE_C23_EMBED'] = rc
    context.Result(rc)
    return rc


@custom_test
def check_mm_crc32_u64(context):
    rc = 0 if tests.CheckDeclaration(
        context,
        symbol='_mm_crc32_u64',
        includes='#include <nmmintrin.h>'
    ) else 1

    context.sconf.env['HAVE_MM_CRC32_U64'] = rc
    context.did_show_result = True
    context.Result(rc)
    return rc


@custom_test
def check_builtin_cpu_supports(context):
    context.Message('Checking whether __builtin_cpu_supports is available... ')

    BUILTIN_CPU_SUPPORTS_PROBE = dedent('''\
    #if !defined(__has_builtin)
    #error "no __has_builtin"
    #endif
    #if !__has_builtin(__builtin_cpu_supports)
    #error "no __builtin_cpu_supports"
    #endif
    int sse42(void) { return __builtin_cpu_supports("sse4.2"); };
    ''')

    rc = int(bool(context.TryCompile(BUILTIN_CPU_SUPPORTS_PROBE, '.c')))

    context.sconf.env['HAVE_BUILTIN_CPU_SUPPORTS'] = rc
    context.Result(rc)
    return rc


@custom_test
def check_target_platform(context):
    """Determine the target CPU architecture and OS from the compiler."""

    def _defined(macro):
        src = f'#ifdef {macro}\nint _check;\n#else\n#error not defined\n#endif'
        return context.TryCompile(src, '.c')

    env = context.sconf.env

    context.Message('Checking target arch... ')
    env['IS_X86_64'] = _defined('__x86_64__') or _defined('__amd64__') or _defined('_M_X64')
    env.Replace(
        IS_X86=env['IS_X86_64'] or _defined('__i386__') or _defined('_M_IX86'),
        IS_AARCH64_LE=int(((_defined('__aarch64__') or _defined('_M_ARM64') or _defined('_M_ARM64EC'))
                          and not _defined('__ARM_BIG_ENDIAN'))),
    )

    context.Message('and OS... ')
    env.Replace(
        IS_APPLE=_defined('__APPLE__'),
        IS_WINDOWS=_defined('_WIN32'),
        IS_CYGWIN=_defined('__CYGWIN__'),
    )

    def _result(targets):
        return ' '.join('%s=%s' % (t, env['IS_' + t.upper()]) for t in targets)

    context.Result(_result(('x86', 'x86_64', 'aarch64_le', 'apple', 'windows', 'cygwin')))
    return True
