#!/usr/bin/env python
# encoding: utf-8

import os
import sys
import shutil
import random
import subprocess
import functools


def touch(path, content=None):
    with open(path, 'wb') as handle:
        if content is not None:
            handle.write(content.encode('utf-8'))


def create_bad_link(link_path):
    fake_target = link_path + '.target'
    touch(fake_target, '.')

    try:
        os.symlink(fake_target, link_path)
    finally:
        os.remove(fake_target)


def create_empty_dirs():
    os.makedirs('recursive/empty/dirs/1/2/3/4')
    os.makedirs('recursive/almost_empty/dirs/1/2/3/4')
    touch('recursive/almost_empty/dirs/1/2/empty_file')


def create_bad_id_files(path):
    call = functools.partial(subprocess.call, shell=True)
    call('sudo useradd rmlint_tmp_user')
    call('sudo groupadd rmlint_tmp_group')
    try:
        for suffix in ('uid', 'gid', 'ugid'):
            actual_path = path + '.' + suffix
            touch(actual_path, 'I have a bad ' + suffix)

        call('sudo chown rmlint_tmp_user:rmlint_tmp_group {f}'.format(
            f=path + '.ugid')
        )

        call('sudo chown rmlint_tmp_user {f}'.format(
            f=path + '.uid')
        )

        call('sudo chgrp rmlint_tmp_group {f}'.format(
            f=path + '.gid')
        )

    finally:
        call('sudo userdel rmlint_tmp_user')
        call('sudo groupdel rmlint_tmp_group')


SOURCE = '''
#include <stdlib.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
    puts("Hello rmlint. Why were you executing this?");
    return EXIT_SUCCESS;
}
'''


def create_binary(path, stripped=False):
    path = path + '.stripped' if stripped else path + '.nonstripped'
    command = 'echo -e \'{src}\' | gcc -o {path} {option} -xc -'.format(
        src=SOURCE, path=path, option=('-s' if stripped else '-ggdb3')
    )
    subprocess.call(command, shell=True)


def create_random_file(path, size=4096, seed=0, make_fishy=False):
    random.seed(seed)
    data = [random.randint(0, 255) for _ in range(size)]

    if make_fishy:
        index = random.randint(0, size)
        data[index] = not data[index]
        path += '.herring'

    touch(path, str(data))


def create_bind_mount(name, idx, max_idx):
    dir1 = name + str(idx)
    dir2 = os.path.join(dir1, name + str(idx + 1 % max_idx) + '_')

    os.makedirs(dir2)

    subprocess.call(
        'mount -o rbind {dir1} {dir2}'.format(
            dir1=dir1, dir2=dir2),
        shell=True
    )

    return dir1, dir2


def create_double_basenames():
    os.makedirs('basenames/one')
    os.makedirs('basenames/two')
    touch('basenames/one/a', 'content-aaa')
    touch('basenames/one/b', 'content-xxx')
    touch('basenames/two/a', 'content-aaa')
    touch('basenames/two/b', 'content-bbb')


def create_dupes(N):
    os.makedirs('dupes')
    for size in [1024, 2048, 4096, 8192]:
        for idx in range(N):
            create_random_file(
                os.path.join('dupes', str(idx)) + '\"_\'' + str(size), size=size
            )

    os.makedirs('origs')
    create_random_file(os.path.join('origs', '1'), size=8192)
    create_random_file(os.path.join('origs', '2'), size=8192)


def remove_bind_mounts():
    for idx in range(0, 3):
        subprocess.call('sudo umount -r testdir/test{a}/test{b}_'.format(
            a=idx, b=idx + 1 % 3
        ), shell=True)


if __name__ == '__main__':
    if os.getuid() is not 0:
        print('You are not root. I need to be for mount --rbind.')
        sys.exit(-1)

    if 'remove' in sys.argv:
        remove_bind_mounts()
        shutil.rmtree('testdir/')
        sys.exit(0)

    if 'create' not in sys.argv:
        print('Usage: ')
        print('    maketestdir.py create  # Creates testdir/')
        print('    maketestdir.py remove  # Removes testdir/')
        sys.exit(-1)

    try:
        os.mkdir('testdir')
    except OSError:
        print('Testdir already exists. Remove it.')
        sys.exit(0)

    os.chdir('testdir')
    create_binary('bin_one')
    create_binary('bin_two', stripped=True)
    create_empty_dirs()
    os.mkdir('symlink_loop')
    os.symlink('symlink_loop', 'symlink_loop/loop')
    create_bad_id_files('bad')
    create_double_basenames()
    create_dupes(30)

    for idx in range(0, 3):
        dir1, dir2 = create_bind_mount('test', idx, 3)
        for directory in [dir1, dir2]:
            create_random_file(os.path.join(directory, 'one'))
            create_random_file(os.path.join(directory, 'two'))
            create_random_file(os.path.join(directory, 'one'), make_fishy=True)

            try:
                create_bad_link(os.path.join(directory, 'two'))
            except OSError as err:
                pass

    create_bad_link('bad_link')
