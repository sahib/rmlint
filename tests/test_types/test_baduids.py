import subprocess
import sys

import pytest

from tests.utils import create_file, get_testdir, run_rmlint, runs_as_root

RMLINT_DUMMY_GROUP = '__rmlint_dummy_group'
RMLINT_DUMMY_USER = '__rmlint_dummy_user'

if sys.platform.startswith('linux'):
    ADD_ID_CMDS = (
        'groupadd {g}',
        'useradd -M -N {u}',
    )
    DEL_ID_CMDS = (
        'userdel -r {u}',
        'groupdel {g}',
    )
elif sys.platform.startswith('freebsd'):
    ADD_ID_CMDS = (
        'pw groupadd -n {g}',
        'pw useradd -n {u}',
    )
    DEL_ID_CMDS = (
        'pw userdel -n {u}',
        'pw groupdel -n {g}',
    )
else:
    ADD_ID_CMDS = DEL_ID_CMDS = None
    pytest.skip(f"uid/gid: {sys.platform} not implemented/supported",
                allow_module_level=True)


def exec_cmds(cmds):
    for cmd in cmds:
        fmt_cmd = cmd.format(
            u=RMLINT_DUMMY_USER,
            g=RMLINT_DUMMY_GROUP,
            t=get_testdir()
        )

        try:
            subprocess.check_call(fmt_cmd, shell=True, stderr=subprocess.PIPE)
        except subprocess.CalledProcessError as err:
            print(cmd, 'failed:', err)


def test_bad_ids():
    if not runs_as_root():
        return

    exec_cmds(ADD_ID_CMDS)

    try:
        create_file('x', '1_bad_uid')
        create_file('y', '2_bad_gid')
        create_file('z', '3_bad_gid_and_uid')

        exec_cmds([
            'chown {u} {t}/1_bad_uid',
            'chgrp {g} {t}/2_bad_gid',
            'chown {u}:{g} {t}/3_bad_gid_and_uid'
        ])
    finally:
        exec_cmds(DEL_ID_CMDS)

    _, *data, footer = run_rmlint('-S a')

    x, y, z = data
    assert x['path'].endswith('1_bad_uid')
    assert y['path'].endswith('2_bad_gid')
    assert z['path'].endswith('3_bad_gid_and_uid')

    assert x['type'] == 'baduid'
    assert y['type'] == 'badgid'
    assert z['type'] == 'badugid'

    assert footer['total_files'] == 3
