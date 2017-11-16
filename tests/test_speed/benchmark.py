#!/usr/bin/env python
# encoding: utf-8

import os
import re
import sys
import time
import json
import pprint
import hashlib
import argparse
import subprocess

from abc import ABCMeta, abstractmethod
from collections import namedtuple, Counter


CFG = namedtuple('Config', [
    # How often to run the program on the dataset.
    # The fs cache is flushed only the first time.
    'n_runs',

    # How many times to run each individual program.
    'n_sub_runs',

    # Output directory
    'output'
])(3, 2, 'bench-output-{stamp}'.format(
    stamp=time.strftime('%FT%T%z', time.localtime())
))


def flush_fs_caches():
    os.sync()
    try:
        with open('/proc/sys/vm/drop_caches', 'w') as handle:
            handle.write('3\n')
        print('-- Flushed fs cache.')
    except IOError:
        print('!! You need to be root to flush the fs cache.')
        sys.exit(-1)


def read_time_info():
    try:
        with open('/tmp/.cpu_usage', 'r') as handle:
            cpu_usage, peak_mem = handle.read().strip().split(' ', 1)
            return int(cpu_usage[:-1]), int(peak_mem)
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

    def get_options(self, paths):
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

        paths = dataset.get_paths()
        bin_cmd = self.binary_path + ' ' + self.get_options(paths)

        print('== Executing: {c}'.format(c=bin_cmd))

        time_cmd = '/bin/time --format "%P %M" \
            --output /tmp/.cpu_usage -- ' + bin_cmd

        try:
            for _ in range(CFG.n_sub_runs):
                flush_fs_caches()

                for run_idx in range(1, CFG.n_runs + 1):
                    print('.. Doing run #' + str(run_idx))
                    start_time = time.time()
                    data_dump = subprocess.check_output(
                        time_cmd,
                        shell=True,
                        stderr=subprocess.STDOUT
                    )

                    time_diff = time.time() - start_time

                    # Remember the time difference as result.
                    if run_idx not in run_benchmarks:
                        run_benchmarks[run_idx] = [0] * 5

                    cpu_usage, peak_mem = read_time_info()

                    # select the fastest run to record time and cpu stats:
                    if run_benchmarks[run_idx][0] > time_diff or run_benchmarks[run_idx][0] == 0:
                        run_benchmarks[run_idx][0] = time_diff
                        run_benchmarks[run_idx][1] = cpu_usage
                        run_benchmarks[run_idx][2] = peak_mem / 1024  # Megabyte

                    if data_dump:
                        stats = self.parse_statistics(data_dump)
                        if stats:
                            run_benchmarks[run_idx][3] = stats['dupes']
                            run_benchmarks[run_idx][4] = stats['sets']

        except subprocess.CalledProcessError as err:
            print('!! Execution of {n} failed: {err}'.format(
                n=self.binary_path, err=err
            ))

        avg_point = [0] * 5
        for idx in range(5):
            avg_point[idx] = sum([v[idx] for v in run_benchmarks.values()])
            avg_point[idx] /= CFG.n_runs

        print('== Took avg time of {t}s / {c}% cpu'.format(
            t=avg_point[0], c=avg_point[1]
        ))
        run_benchmarks['average'] = avg_point

        return run_benchmarks, memory_usage

    def guess_version(self):
        version = self.compute_version()
        if version is not None:
            self.version = str(version).strip()
            print('-- Guessed version: ', self.version)

    def install(self):
        """Install the program in /tmp; fetch source from internet."""
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

    def get_options(self, paths):
        return '--hidden -o summary -o json:/tmp/rmlint.json -T df ' + ' '.join(paths)

    def compute_version(self):
        version_text = subprocess.check_output(
            self.get_binary() + ' --version', shell=True, stderr=subprocess.STDOUT
        )

        match = re.search('(\d.\d.\d) .*(rev [0-9a-f]{7})', str(version_text))
        if match is not None:
            return ' '.join(match.groups())

        return ""

    def parse_statistics(self, _):
        try:
            with open('/tmp/rmlint.json', 'r') as fd:
                stats = json.loads(fd.read())

            return {
                'sets': stats[-1]['duplicate_sets'],
                'dupes': stats[-1]['duplicates']
            }
        except OSError:
            pass


class RmlintSpot(Rmlint):
    website = 'https://github.com/SeeSpotRun/rmlint'
    script = 'rmlint-spot.sh'

    def get_binary(self):
        return 'rmlint/rmlint-spot'

    def get_benchid(self):
        return 'rmlint-spot'

class RmlintSpotParanoid(RmlintSpot):
    def get_options(self, paths):
        return '-p ' + RmlintSpot.get_options(self, paths)

    def get_benchid(self):
        return 'rmlint-spot-paranoid'


class Rmlint222(Rmlint):
    script = 'rmlint-v2.2.2.sh'

    def get_binary(self):
        return 'rmlint/rmlint-v2.2.2'

    def get_benchid(self):
        return 'rmlint-v2.2.2'


class Rmlint246(Rmlint):
    script = 'rmlint-v2.4.6.sh'

    def get_binary(self):
        return 'rmlint/rmlint-v2.4.6'

    def get_benchid(self):
        return 'rmlint-v2.4.6'



class Rmlint222Paranoid(Rmlint222):
    def get_options(self, paths):
        return '-p ' + Rmlint222.get_options(self, paths)

    def get_benchid(self):
        return Rmlint222.get_benchid(self) + '-paranoid'


class RmlintMaster(Rmlint):
    script = 'rmlint-master.sh'

    def get_binary(self):
        return 'rmlint/rmlint-master'

    def get_benchid(self):
        return 'rmlint-master'


class RmlintSpooky(Rmlint):
    def get_options(self, paths):
        return '-a spooky ' + Rmlint.get_options(self, paths)

    def get_benchid(self):
        return 'rmlint-spooky'


class RmlintXXHash(Rmlint):
    def get_options(self, paths):
        return '-a xxhash ' + Rmlint.get_options(self, paths)

    def get_benchid(self):
        return 'rmlint-xxhash'


class RmlintCity(Rmlint):
    def get_options(self, paths):
        return '-a city ' + Rmlint.get_options(self, paths)

    def get_benchid(self):
        return 'rmlint-city'


class RmlintMD5(Rmlint):
    def get_options(self, paths):
        return '-a md5 ' + Rmlint.get_options(self, paths)

    def get_benchid(self):
        return 'rmlint-md5'


class RmlintMurmur(Rmlint):
    def get_options(self, paths):
        return '-a murmur ' + Rmlint.get_options(self, paths)

    def get_benchid(self):
        return 'rmlint-murmur'


class RmlintParanoid(Rmlint):

    def get_options(self, paths):
        return '-p ' + Rmlint.get_options(self, paths)

    def get_benchid(self):
        return 'rmlint-paranoid'


class RmlintReplay(Rmlint):

    def get_options(self, paths):
        return '--replay /tmp/rmlint.json -T df -o summary ' + ' '.join(paths)

    def get_benchid(self):
        return 'rmlint-replay'


class OldRmlint(Program):
    website = 'https://github.com/sahib/rmlint'
    script = 'rmlint-old.sh'

    def get_binary(self):
        return 'rmlint/rmlint-old'

    def get_options(self, paths):
        return ' '.join(paths) + ' -v4'  # log output to stdout

    def compute_version(self):
        version_text = subprocess.check_output(
            'rmlint/rmlint-old --version', shell=True, stderr=subprocess.STDOUT
        ).decode('utf-8')

        match = re.search('Version (\d.\d.\d)', str(version_text))
        if match is not None:
            return ' '.join(match.groups()).strip()

        return ""

    def parse_statistics(self, dump):
        dups, sets = 0, 0
        for line in dump.splitlines():
            if line.startswith(b'ORIG'):
                sets += 1
                dups += 1

            if line.startswith(b'DUPL'):
                dups += 1

        return {
            'dupes': dups,
            'sets': sets
        }


class Dupd(Program):
    website = 'https://github.com/jvirkki/dupd'
    script = 'dupd.sh'
    stats_file = '/tmp/rmlint-bench/.dupd.stats'

    def get_binary(self):
        return 'dupd/dupd'

    def get_options(self, paths):
        try:
            os.remove(self.stats_file)
        except OSError:
            pass

        args = ' '.join(['--path ' + path for path in paths])
        return 'scan ' + args + ' --nodb --stats-file ' + self.stats_file

    def compute_version(self):
        return subprocess.check_output(
            'dupd/dupd version', shell=True
        ).decode('utf-8').strip()

    def parse_statistics(self, _):
        stats = {}

        try:
            with open(self.stats_file, 'r') as fd:
                for line in fd:
                    line = line.strip()
                    if line:
                        key, value = line.split(' ', 1)
                        stats[key] = value
            return {
                'dupes': int(stats['stats_duplicate_files']),
                'sets': int(stats['stats_duplicate_sets'])
            }
        except OSError:
            pass
        except ValueError as err:
            print('doh', err)


class Rdfind(Program):
    website = 'http://rdfind.pauldreik.se'
    script = 'rdfind.sh'
    result_file = '/tmp/rmlint-bench/.rdfind.results'

    def get_binary(self):
        return 'rdfind-1.3.4/rdfind'

    def get_options(self, paths):
        return '-ignoreempty true -removeidentinode \
            false -checksum sha1 -dryrun true \
            -outputname ' + self.result_file + ' '.join(paths)

    def compute_version(self):
        words = subprocess.check_output(
            'rdfind-1.3.4/rdfind --version', shell=True
        ).decode('utf-8')

        return words.split(' ')[-1].strip()

    def parse_statistics(self, _):
        try:
            stats = Counter()
            with open(self.result_file, 'r') as fd:
                for line in fd:
                    line = line.strip()
                    if line:
                        stats[line.split()[0]] += 1

            return {
                'dupes': stats['DUPTYPE_WITHIN_SAME_TREE'],
                'sets': stats['DUPTYPE_FIRST_OCCURRENCE']
            }
        except OSError:
            pass
        except ValueError:
            pass


class Fdupes(Program):
    website = 'http://en.wikipedia.org/wiki/Fdupes'
    script = 'fdupes.sh'

    def get_binary(self):
        return 'fdupes/fdupes'

    def get_options(self, paths):
        return '-f -q -rnH -m ' + ' '.join(paths)

    def parse_statistics(self, dump):
        dump = dump.decode('utf-8')
        match = re.match(
            '(\d+) duplicate files \(in (\d+) sets\)', dump
        )

        if match:
            return {
                'dupes': int(match.group(1)),
                'sets': int(match.group(2))
            }

    def compute_version(self):
        return subprocess.check_output(
            'fdupes/fdupes --version', shell=True
        ).decode('utf-8').strip()


class Baseline(Program):
    website = 'https://github.com/sahib/rmlint/blob/develop\
/tests/test_speed/build_scripts/baseline.sh'

    script = 'baseline.sh'

    def get_binary(self):
        return 'baseline/baseline.py'

    def get_options(self, paths):
        return ' '.join(paths)

    def compute_version(self):
        return '1.0'

    def parse_statistics(self, dump):
        return json.loads(dump.decode('utf-8'))

############################
#    DATASET GENERATORS    #
############################


class Dataset:
    __metaclass__ = ABCMeta

    def __init__(self, name):
        self.name = name

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

    def __init__(self, name, paths):
        Dataset.__init__(self, name)
        self.paths = paths

    def generate(self):
        pass

    def get_paths(self):
        return self.paths


def do_run(programs, dataset):
    results = {
        'metadata': {
            'path': dataset.get_paths(),
            'n_runs': CFG.n_runs,
            'n_sub_runs': CFG.n_sub_runs
        },
        'programs': {}
    }

    benchmark_json_path = os.path.join(
        CFG.output,
        'benchmark_{name}.json'.format(
            name=dataset.name.replace('/', '\\')
        )
    )

    for program in programs:
        program.install()

        data, memory_usage = program.run(dataset)
        print('>> Timing was #run: [time, cpu, dupes, sets]:')
        pprint.pprint(data)

        bench_id = program.get_benchid()
        results['programs'][bench_id] = {}
        results['programs'][bench_id]['version'] = program.version
        results['programs'][bench_id]['website'] = program.get_website()
        results['programs'][bench_id]['numbers'] = data
        results['programs'][bench_id]['memory'] = memory_usage

        print('-- Writing current benchmark results to', benchmark_json_path)
        with open(benchmark_json_path, 'w') as json_file:
            json_file.write(json.dumps(results, indent=4))


def parse_arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-r", "--run", help="Run the benchmarks.",
        dest='do_run', default=False, action='store_true'
    )
    parser.add_argument(
        "-i", "--install", help="Install all competitors from source.",
        dest='do_install', default=False, action='store_true'
    )
    parser.add_argument(
        "-g", "--generate", help="Generate datasets.",
        dest='do_generate', default=False, action='store_true'
    )
    parser.add_argument(
        "-v", "--print-versions", help="Print versions of programs.",
        dest='do_print_versions', default=False, action='store_true'
    )
    parser.add_argument(
        "-d", "--dataset",
        help="Dataset to run on. Can be given multiple times.",
        dest='datasets', action='append'
    )
    parser.add_argument(
        "-p", "--program",
        help="Choose program to run. Can be given multiple times.",
        dest='programs', action='append'
    )
    return parser.parse_args()


def main():
    datasets = [
        ExistingDataset('usr', ['/usr']),
        ExistingDataset('tmp', ['/tmp']),
        ExistingDataset('tmpvar', ['/tmp', '/var']),
        ExistingDataset('usrvar', ['/usr', '/var']),
        ExistingDataset('usr_music', ['/usr', '/mnt/music']),
        ExistingDataset('home', ['/home/sahib'])
    ]

    options = parse_arguments()

    # Make specifying absolute paths to -d possible.
    for dataset_name in options.datasets:
        if dataset_name.startswith('/'):
            datasets.append(ExistingDataset(
                dataset_name,
                [dataset_name]
            ))

    if options.do_generate or options.datasets:
        for dataset in datasets:
            if dataset in options.datasets or len(options.datasets) is 0:
                dataset.generate()

    programs = [
        # (hopefully) slowest:
        # Baseline(),
        # Current:
        Rmlint(),
        RmlintParanoid(),
        # RmlintSpot(),
        # RmlintSpotParanoid(),
        Rmlint222(),
        Rmlint246(),
        Rmlint222Paranoid(),
        RmlintXXHash(),
        # RmlintSpooky(),
        # RmlintCity(),
        # RmlintMD5(),
        # RmlintMurmur(),
        RmlintReplay(),
        # Old rmlint:
        OldRmlint(),
        # Actual competitors:
        Fdupes(),
        Rdfind(),
        # Dupd()
    ]

    # Filter the `programs` list if necessary.
    if options.programs:
        options.programs = set(options.programs)
        programs = [p for p in programs if p.get_benchid() in options.programs]

    if not programs:
        print('!! No valid programs given.')
        return

    # Do the install procedure only:
    if options.do_install:
        for program in programs:
            print('++ Installing ' + program.get_name() + ':')
            program.install()

    # Print informative version if needed.
    if options.do_print_versions:
        current_path = os.getcwd()
        for program in programs:
            os.chdir(program.get_temp_dir())
            print(program.get_binary() + ':', program.compute_version())

        os.chdir(current_path)

    # Execute the actual run:
    if options.do_run:
        # Make sure the output directory exists:
        try:
            os.mkdir(CFG.output)
        except OSError:
            pass

        for dataset_name in options.datasets:
            for dataset in datasets:
                if dataset.name == dataset_name:
                    try:
                        do_run(programs, dataset)
                        break
                    except KeyboardInterrupt:
                        print(' Interrupted this run. Next dataset.')


if __name__ == '__main__':
    main()
