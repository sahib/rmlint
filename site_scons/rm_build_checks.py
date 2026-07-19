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
        # Not a git checkout (e.g. building from a release tarball, where
        # STATIC_GIT_REV from .version takes over) or git is unavailable.
        print('Unable to find git revision.')

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


def read_cpu_flags():
    """Feature flags of the build host's CPU, as a lowercase set."""
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
        context.sconf.env['HAVE_' + ext] = have_ext
        print(f'    {ext}: {have_ext}')

    context.did_show_result = True
    context.Result(1)
    return 1


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
    'check_cpu_extensions': check_cpu_extensions,
    'check_builtin_cpu_supports': check_builtin_cpu_supports,
    'check_sysmacro_h': check_sysmacro_h,
}
