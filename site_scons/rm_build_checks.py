"""Configure-time checks for the rmlint build."""
import os
import platform
import shutil
import subprocess

import SCons.Conftest as tests
from SCons.Script import ARGUMENTS, Exit, GetOption

from rm_build_support import OPTIONAL_FLAGS, PKG_CONFIG, read_version

STATIC_GIT_REV = read_version()[4]


def check_pkgconfig(context, version):
    context.Message('Checking for pkg-config... ')
    command = PKG_CONFIG + ' --atleast-pkgconfig-version=' + version
    rc, _ = context.TryAction(command)
    if not rc:
        print("Error: pkg-config not found (or too old).")
        Exit(1)
    context.Result(rc)
    return rc


def check_pkg(context, name, varname, required=True):
    rc, text = 1, ''

    package = name.split()[0]
    if package in OPTIONAL_FLAGS and GetOption('with_' + package) is False:
        context.Message(f'Explicitly disabling {name}...')
        rc = 0

    if rc != 0:
        context.Message(f'Checking for {name}... ')
        rc, text = context.TryAction(f"{PKG_CONFIG} --exists '{name}'")

    # 0 is defined as error by TryAction
    if rc == 0 and required:
        print('Error: ' + name + ' not found.')
        Exit(1)

    # Remember we have it:
    context.sconf.env[varname] = rc
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
        # Not a git checkout or git unavailable.
        # Will use STATIC_GIT_REV from read_version().
        print('Unable to call git.')

    rev = rev or 'unknown'
    context.sconf.env['gitrev'] = rev
    context.Result(rev)
    return rev


def check_sysmacro_h(context):
    rc = 1
    if rc and tests.CheckHeader(context, 'sys/sysmacros.h'):
        rc = 0

    context.sconf.env['HAVE_SYSMACROS_H'] = rc
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

    context.sconf.env['HAVE_LIBELF'] = rc

    context.did_show_result = True
    context.Result(rc)
    return rc


def check_uname(context):
    rc = 1

    if rc and tests.CheckHeader(context, 'sys/utsname.h', header=""):
        rc = 0

    context.sconf.env['HAVE_UNAME'] = rc

    context.did_show_result = True
    context.Result(rc)
    return rc


def check_gettext(context):
    env = context.sconf.env
    rc = 1

    if GetOption('with_gettext') is False:
        rc = 0

    if rc and tests.CheckHeader(context, 'locale.h'):
        rc = 0

    env['HAVE_LIBINTL'] = rc
    env['HAVE_MSGFMT'] = int(shutil.which('msgfmt') is not None)
    env['HAVE_GETTEXT'] = env['HAVE_MSGFMT'] and env['HAVE_LIBINTL']

    context.did_show_result = True
    context.Result(rc)
    return rc


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


def check_bigfiles(context):
    off_t_is_big_enough = True

    if tests.CheckTypeSize(context, 'off_t', header='#include <sys/types.h>') < 8:
        off_t_is_big_enough = False

    have_stat64 = True
    if tests.CheckFunc(context, 'stat64'):
        have_stat64 = False

    rc = int(off_t_is_big_enough or have_stat64)
    context.sconf.env['HAVE_BIG_OFF_T'] = int(off_t_is_big_enough)
    context.sconf.env['HAVE_BIG_STAT'] = int(have_stat64)
    context.sconf.env['HAVE_BIGFILES'] = rc

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
        includes='#include <blkid.h>'
    ):
        rc = 0

    context.sconf.env['HAVE_BLKID'] = rc

    context.did_show_result = True
    context.Result(rc)
    return rc


def check_sys_block(context):
    rc = 1

    context.Message('Checking for existence of /sys/block... ')
    if not os.access('/sys/block', os.R_OK):
        rc = 0

    context.sconf.env['HAVE_SYSBLOCK'] = rc

    context.Result(rc)
    return rc


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


def check_xattr(context):
    rc = 1

    for func in ['getxattr', 'setxattr', 'removexattr', 'listxattr']:
        if tests.CheckFunc(context, func):
            rc = 0
            break

    context.sconf.env['HAVE_XATTR'] = rc

    context.did_show_result = True
    context.Result(rc)
    return rc


def check_lxattr(context):
    rc = 1

    for func in ['lgetxattr', 'lsetxattr', 'lremovexattr', 'llistxattr']:
        if tests.CheckFunc(context, func):
            rc = 0
            break

    context.sconf.env['HAVE_LXATTR'] = rc

    context.did_show_result = True
    context.Result(rc)
    return rc


def check_sha512(context):
    rc = 1
    if tests.CheckDeclaration(context, 'G_CHECKSUM_SHA512', includes='#include <glib.h>'):
        rc = 0

    context.sconf.env['HAVE_SHA512'] = rc

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

    context.sconf.env['HAVE_BTRFS_H'] = rc
    context.did_show_result = True
    context.Result(rc)
    return rc


def check_linux_fs_h(context):
    rc = 1
    if tests.CheckHeader(context, 'linux/fs.h'):
        rc = 0

    context.sconf.env['HAVE_LINUX_FS_H'] = rc
    context.did_show_result = True
    context.Result(rc)
    return rc


def check_linux_limits(context):
    rc = 1
    if tests.CheckHeader(context, 'linux/limits.h'):
        rc = 0

    context.sconf.env['HAVE_LINUX_LIMITS'] = rc
    context.did_show_result = True
    context.Result(rc)
    return rc


def check_cygwin(context):
    context.Message('Checking for cygwin environment...')
    uname = platform.uname()
    context.Message('/'.join(uname))
    rc = uname[0].upper().startswith('CYGWIN')

    context.sconf.env['IS_CYGWIN'] = rc
    context.Result(rc)
    return rc


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


def check_builtin_cpu_supports(context):
    rc = 0 if tests.CheckDeclaration(
        context,
        symbol='__builtin_cpu_supports'
    ) else 1

    context.sconf.env['HAVE_BUILTIN_CPU_SUPPORTS'] = rc
    context.did_show_result = True
    context.Result(rc)
    return rc


def check_target_arch(context):
    # Determine the target CPU architecture and environment from the compiler.
    # The decision is made based on $CFLAGS and toolchain; never the build host.
    # This determines whether blake3 is built using x86_64 assembly or
    # x86 C intrinsics for different SIMD extensions; blake3's own runtime
    # dispatch then picks the widest available implementation at runtime.
    context.Message('Checking target environment and CPU architecture... ')

    def _defined(macro):
        src = '#if !defined(%s)\n#error not defined\n#endif\nint _target_arch_check;\n' % macro
        return context.TryCompile(src, '.c')

    env = context.sconf.env
    env['IS_X86_64'] = 1 if (
        _defined('__x86_64__') or _defined('__amd64__') or _defined('_M_X64')
    ) else 0
    env['IS_X86'] = 1 if (
        env['IS_X86_64'] or _defined('__i386__') or _defined('_M_IX86')
    ) else 0
    env['IS_WINDOWS'] = 1 if _defined('_WIN32') else 0

    context.Result('x86=%s x86_64=%s windows=%s' % (
        env['IS_X86'], env['IS_X86_64'], env['IS_WINDOWS']
    ))
    return True  # unused


# Passed to Configure(); the SConstruct calls these as conf.<name>().
CUSTOM_TESTS = {
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
    'check_target_arch': check_target_arch,
    'check_builtin_cpu_supports': check_builtin_cpu_supports,
    'check_sysmacro_h': check_sysmacro_h,
}
