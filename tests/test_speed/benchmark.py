#!/usr/bin/env python
# encoding: utf-8

import os
import re
import sys
import time
import json
import hashlib
import subprocess

from abc import ABCMeta, abstractmethod

# Easy: Pseudorandom data generation:
from faker import Faker

faker = Faker()
faker.seed(0xDEADBEEF)


# How often to run the program on the dataset.
# The fs cache is flushed only the first time.
N_RUNS = 4

# How many times to run each individual program.
N_SUB_RUNS = 2


def flush_fs_caches():
    try:
        with open('/proc/sys/vm/drop_caches', 'w') as handle:
            handle.write('3\n')
        print('-- Flushed fs cache.')
    except IOError:
        print('!! You need to be root to flush the fs cache.')
        sys.exit(-1)


VALGRIND_MASSIF_PEAK_MEM = '''
valgrind --tool=massif --log-file=0 --massif-out-file=/tmp/massif.out \
    {interp} {command} >/dev/null 2>/dev/null && \
    cat /tmp/massif.out | grep mem_heap_B  | cut -d= -f2 | sort -rn | head -n1
'''


def measure_peak_memory(shell_command):
    try:
        # Hack to get python scripts working:
        # (normally a real binary is assumed)
        interp = ''
        if '.py' in shell_command:
            interp = '/usr/bin/python'

        peak_bytes_b = subprocess.check_output(
            VALGRIND_MASSIF_PEAK_MEM.format(
                interp=interp,
                command=shell_command
            ),
            shell=True
        )
        return int(peak_bytes_b or '0')
    except subprocess.CalledProcessError as err:
        print('!! Unable to execute massif: {err}'.format(err=err))
        return None


def read_cpu_usage():
    try:
        with open('/tmp/.cpu_usage', 'r') as handle:
            percent = handle.read().strip()
            return int(percent[:-1])
    except IOError:
        return 0


class Program:
    __metaclass__ = ABCMeta

    version = ''
    binary_path = None
    website = None
    script = None

    def __init__(self):
        # Do the path building now.
        # Later on we call chdir() which might mess this up:
        self.build_path = os.path.join(
            os.path.realpath(os.path.dirname(__file__)),
            'build_scripts',
            self.script or ''
        )

    def get_install(self):
        if self.script:
            with open(self.build_path, 'r') as fd:
                return fd.read()

    ####################
    # ABSTRACT METHODS #
    ####################

    @abstractmethod
    def get_binary(self):
        pass

    @abstractmethod
    def compute_version(self):
        pass

    def get_options(self):
        return ''

    def get_benchid(self):
        return self.get_name()

    ####################
    # COMMON FUNCTIONS #
    ####################

    def get_name(self):
        """Get a printable name of the name (e.g. "rmlint")
        """
        return os.path.basename(self.get_binary())

    def get_temp_dir(self):
        temp_hash = '-'.join((
            self.get_name(),
            hashlib.md5(self.get_binary().encode('ascii')).hexdigest()[:6]
        ))
        temp_dir = os.path.join('/tmp', 'rmlint-bench', temp_hash)

        try:
            os.makedirs(temp_dir)
        except OSError:
            pass

        return temp_dir

    def get_website(self):
        if self.website:
            return self.website
        else:
            return 'https://www.google.de/search?q=' + self.get_name()

    def run(self, dataset):
        """Run the program on a given dataset
        """
        run_benchmarks = {}
        memory_usage = -1

        bin_cmd = '' + self.binary_path + ' ' + self.get_options().format(
            path=' '.join(dataset.get_paths())
        )
        print('== Executing: {c}'.format(c=bin_cmd))

        time_cmd = '/bin/time --format "%P" --output /tmp/.cpu_usage -- ' + bin_cmd

        try:
            for _ in range(N_SUB_RUNS):
                flush_fs_caches()

                for run_idx in range(1, N_RUNS + 1):
                    start_time = time.time()
                    subprocess.check_output(
                        time_cmd,
                        shell=True,
                        stderr=subprocess.STDOUT
                    )

                    time_diff = time.time() - start_time

                    # Remember the time difference as result.
                    if run_idx not in run_benchmarks:
                        run_benchmarks[run_idx] = [0, 0]

                    run_benchmarks[run_idx][0] += time_diff / N_SUB_RUNS
                    run_benchmarks[run_idx][1] += read_cpu_usage() / N_SUB_RUNS

            # Make valgrind run a bit faster, profit from caches.
            # Also known as 'the big ball of mud'
            memory_usage = measure_peak_memory(bin_cmd) / 1024 ** 2
            print('-- Memory usage was {b} MB'.format(b=memory_usage))
        except subprocess.CalledProcessError as err:
            print('!! Execution of {n} failed: {err}'.format(
                n=self.binary_path, err=err
            ))

        avg_time = sum([v[0] for v in run_benchmarks.values()]) / N_RUNS
        avg_cpus = sum([v[1] for v in run_benchmarks.values()]) / N_RUNS

        print('== Took avg time of {t}s / {c}% cpu'.format(
            t=avg_time, c=avg_cpus
        ))
        run_benchmarks['average'] = [avg_time, avg_cpus]
        return run_benchmarks, memory_usage

    def guess_version(self):
        version = self.compute_version()
        if version is not None:
            self.version = str(version).strip()
            print('-- Guessed version: ', self.version)

    def install(self):
        """Install the program in /tmp; fetch source from internet.
        """
        temp_dir = self.get_temp_dir()
        temp_bin = os.path.join(temp_dir, self.get_binary())
        current_path = os.getcwd()

        os.chdir(temp_dir)
        self.binary_path = os.path.abspath(self.get_binary())

        if os.path.exists(temp_bin):
            print('-- Path exists: ' + temp_bin)
            print('-- Skipping install; Delete if you need an update.')
            self.guess_version()
            os.chdir(current_path)
            return

        sh_procedure = self.get_install()
        if not sh_procedure:
            return

        try:
            subprocess.check_output(
                sh_procedure, stderr=subprocess.STDOUT, shell=True
            )
            print('-- Installed {n} to {p}'.format(
                n=self.get_name(), p=temp_dir
            ))

            self.guess_version()
            return self.binary_path
        except subprocess.CalledProcessError as err:
            print('!! Failed to install {n}: {err.output}'.format(
                n=self.get_name(), err=err
            ))
            self.binary_path = None
            return None
        finally:
            os.chdir(current_path)


class Rmlint(Program):
    website = 'https://github.com/sahib/rmlint'
    script = 'rmlint-new.sh'

    def get_binary(self):
        return 'rmlint/rmlint'

    def get_options(self):
        return '-o summary {path} -o json:/tmp/rmlint.json -T df'

    def compute_version(self):
        version_text = subprocess.check_output(
            'rmlint/rmlint --version', shell=True, stderr=subprocess.STDOUT
        )

        match = re.search('(\d.\d.\d) .*(rev [0-9a-f]{7})', str(version_text))
        if match is not None:
            return ' '.join(match.groups())

        return ""


class RmlintSpooky(Rmlint):

    def get_options(self):
        return '-PP ' + Rmlint.get_options(self)

    def get_benchid(self):
        return 'rmlint-spooky'


class RmlintParanoid(Rmlint):

    def get_options(self):
        return '-pp ' + Rmlint.get_options(self)

    def get_benchid(self):
        return 'rmlint-paranoid'


class RmlintReplay(Rmlint):

    def get_options(self):
        return '--replay /tmp/rmlint.json ' + Rmlint.get_options(self)

    def get_benchid(self):
        return 'rmlint-cache'


class OldRmlint(Program):
    website = 'https://github.com/sahib/rmlint'
    script = 'rmlint-old.sh'

    def get_binary(self):
        return 'rmlint/rmlint-old'

    def get_options(self):
        return '{path}'

    def compute_version(self):
        version_text = subprocess.check_output(
            'rmlint/rmlint-old --version', shell=True, stderr=subprocess.STDOUT
        ).decode('utf-8')

        match = re.search('Version (\d.\d.\d)', str(version_text))
        if match is not None:
            return ' '.join(match.groups())

        return ""


class Dupd(Program):
    website = 'http://rdfind.pauldreik.se'
    script = 'dupd.sh'

    def get_binary(self):
        return 'dupd/dupd'

    def get_options(self):
        return 'scan --path {path}'

    def compute_version(self):
        return subprocess.check_output(
            'dupd/dupd version', shell=True
        ).decode('utf-8')


class Rdfind(Program):
    website = 'https://github.com/jvirkki/dupd'
    script = 'rdfind.sh'

    def get_binary(self):
        return 'rdfind-1.3.4/rdfind'

    def get_options(self):
        return '-ignoreempty true -removeidentinode\
            false -checksum sha1 -dryrun true {path}'

    def compute_version(self):
        words = subprocess.check_output(
            'rdfind-1.3.4/rdfind --version', shell=True
        ).decode('utf-8')

        return words.split(' ')[-1]


class Fdupes(Program):
    website = 'http://en.wikipedia.org/wiki/Fdupes'
    script = 'fdupes.sh'

    def get_binary(self):
        return 'fdupes/fdupes'

    def get_options(self):
        return '-f -q -rnH {path}'

    def parse_statistics(self, dump):
        lines = dump.splitlines()
        empty = sum(bool(line.strip()) for line in lines)
        return empty, len(lines) - empty

    def compute_version(self):
        return subprocess.check_output(
            'fdupes/fdupes --version', shell=True
        ).decode('utf-8')


class Baseline(Program):
    script = 'baseline.sh'

    def get_binary(self):
        return 'baseline/baseline.py'

    def get_options(self):
        return '{path}'

    def compute_version(self):
        return '1.0'

############################
#    DATASET GENERATORS    #
############################


class Dataset:
    __metaclass__ = ABCMeta

    def __init__(self, name, basedir=None):
        self.name = name
        if basedir is not None:
            self.workpath = os.path.join(basedir, name)

            try:
                os.makedirs(self.workpath)
            except OSError:
                pass
        else:
            self.workpath = None

    ####################
    # ABSTRACT METHODS #
    ####################

    @abstractmethod
    def generate(self):
        pass

    @abstractmethod
    def get_paths(self):
        pass


class ExistingDataset(Dataset):

    def generate(self):
        pass


class UsrDataset(ExistingDataset):

    def get_paths(self):
        return ['/usr']


class HomeDataset(ExistingDataset):

    def get_paths(self):
        # return [os.path.expanduser('~')]
        return ['/tmp']


class UsrAndHomeDataset(ExistingDataset):

    def get_paths(self):
        d1 = HomeDataset()
        d2 = UsrDataset()
        return d1.get_paths() + d2.get_paths()


class MusicDataset(ExistingDataset):

    def get_paths(self):
        return ['/run/media/sahib/35d4a401-ad4c-4221-afc0-f284808a1cdc/music']


class UniqueNamesDataset(Dataset):

    def generate(self):
        for idx in range(10 ** 4):
            name = faker.name()
            path = os.path.join(self.workpath, str(idx))
            with open(path, 'w') as handle:
                handle.write(name * 1024 * 16)

    def get_paths(self):
        return [self.workpath]


if __name__ == '__main__':
    datasets = [
        # UsrDataset('usr'),
        # UniqueNamesDataset('names', sys.argv[1])
        # MusicDataset('music')
        HomeDataset('home')
    ]

    for dataset in datasets:
        # Generate the data in the dataset:
        dataset.generate()

        results = {
            'metadata': {
                'path': dataset.get_paths(),
                'n_runs': N_RUNS,
                'n_sub_runs': N_SUB_RUNS
            },
            'programs': {}
        }

        programs = [Baseline()]
        # programs = [
        #     OldRmlint(),
        #     Fdupes(), Rdfind(), Dupd(),
        #     RmlintSpooky(), RmlintParanoid(), Rmlint(), RmlintReplay()
        # ]

        for program in programs:
            program.install()
            data, memory_usage = program.run(dataset)
            print('>> Timing was ', data)

            bench_id = program.get_benchid()
            results['programs'][bench_id] = {}
            results['programs'][bench_id]['version'] = program.version
            results['programs'][bench_id]['website'] = program.get_website()
            results['programs'][bench_id]['numbers'] = data
            results['programs'][bench_id]['memory'] = memory_usage

        benchmark_json_path = 'benchmark_{name}.json'.format(name=dataset.name)
        print('-- Writing benchmark to benchmark.json')
        with open('benchmark.json', 'w') as json_file:
            json_file.write(json.dumps(results, indent=4))
