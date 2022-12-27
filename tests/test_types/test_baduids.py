#!/usr/bin/env python3
# encoding: utf-8
from tests.utils import *

import subprocess


RMLINT_DUMMY_GROUP = '__rmlint_dummy_group'
RMLINT_DUMMY_USER = '__rmlint_dummy_user'


def exec_cmds(cmds):
    for cmd in cmds:
        fmt_cmd = cmd.format(
            u=RMLINT_DUMMY_USER,
            g=RMLINT_DUMMY_GROUP,
            t=TESTDIR_NAME
        )

        try:
            subprocess.check_call(fmt_cmd, shell=True, stderr=subprocess.PIPE)
        except subprocess.CalledProcessError as err:
            print(cmd, 'failed:', err)


def test_bad_ids(usual_setup_usual_teardown):
    if not runs_as_root():
        return

    exec_cmds([
        'groupadd {g}',
        'useradd -M -N  {u}',
    ])

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
        exec_cmds([
            'userdel -r {u}',
            'groupdel {g}'
        ])

    head, *data, footer = run_rmlint('-S a')

    x, y, z = data
    assert x['path'].endswith('1_bad_uid')
    assert y['path'].endswith('2_bad_gid')
    assert z['path'].endswith('3_bad_gid_and_uid')

    assert x['type'] == 'baduid'
    assert y['type'] == 'badgid'
    assert z['type'] == 'badugid'

    assert footer['total_files'] == 3
