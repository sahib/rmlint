#!/usr/bin/env python
# encoding: utf-8


"""
This module coordinates the actual running of the `rmlint` utility.
For running GLib's GSuprocess is used, which provides a nice API.
Before running, the command will be constructed according to the
options set in prior by GSettings (e.g. from the Settings view).
The settings are described and defined in the settings schema.
"""

# Stdlib:
import os
import re
import json
import errno
import shutil
import codecs
import logging
import tempfile

from enum import Enum

# External:
from gi.repository import Gio
from gi.repository import GLib
from gi.repository import GObject


LOGGER = logging.getLogger('runner')
ASCII_COLOR_REGEX = re.compile(r'\x1B\[\d+(.*?)m')


class AlgorithmType(Enum):
    """Key: computation-algorithm"""
    SPOOKY, CITY, SHA1, SHA256, SHA512, SHA3, MD5, \
       BLAKE2B, BLAKE2S, PARANOID = range(1, 11)

    MAPPING = {
        SPOOKY:   ['--algorithm', 'spooky'],
        CITY:     ['--algorithm', 'city'],
        SHA1:     ['--algorithm', 'sha1'],
        SHA256:   ['--algorithm', 'sha256'],
        SHA512:   ['--algorithm', 'sha512'],
        SHA3:     ['--algorithm', 'sha3'],
        MD5:      ['--algorithm', 'md5'],
        BLAKE2B:  ['--algorithm', 'blake2b'],
        BLAKE2S:  ['--algorithm', 'blake2sp'],
        PARANOID: ['--algorithm', 'paranoid']
    }


class MatchType(Enum):
    """Key: traverse-match"""
    NONE, BASENAME, EXTENSION, WITHOUT_EXTENSION = range(1, 5)
    MAPPING = {
        NONE: [],
        BASENAME: ['--match-basename'],
        EXTENSION: ['--match-with-extension'],
        WITHOUT_EXTENSION: ['--match-without-extension']
    }


class SymlinkType(Enum):
    """Key: general-find-symlinks"""
    IGNORE, SEE, FOLLOW = range(1, 4)
    MAPPING = {
        IGNORE: ['--no-followlinks'],
        SEE: ['--see-symlinks'],
        FOLLOW: ['--followlinks']
    }


class HiddenType(Enum):
    """Key: traverse-hidden"""
    IGNORE, PARTIAL, FOLLOW = range(1, 4)
    MAPPING = {
        IGNORE: ['--no-hidden'],
        PARTIAL: ['--partial-hidden'],
        FOLLOW: ['--hidden']
    }


class KeepAllType(Enum):
    """Key: computation-keep-all-tagged"""
    NONE, TAGGED, UNTAGGED = range(1, 4)
    MAPPING = {
        NONE: [],
        TAGGED: ['--keep-all-tagged'],
        UNTAGGED: ['--keep-all-untagged']
    }


class MustMatchType(Enum):
    """Key: computation-must-match-tagged"""
    NONE, TAGGED, UNTAGGED = range(1, 4)
    MAPPING = {
        NONE: [],
        TAGGED: ['--must-match-tagged'],
        UNTAGGED: ['--must-match-untagged']
    }


class HardlinkType(Enum):
    """Key: "general-find-hardlinks"""
    OFF, ACTIVE = False, True
    MAPPING = {
        ACTIVE: ['--hardlinked'],
        OFF: ['--no-hardlinked']
    }


class HandlerType(Enum):
    """Key: "general-handler-type"""
    REMOVE_DUPES, LINK_DUPES, SYMLINK_DUPES, HARDLINK_DUPES = range(1, 5)
    MAPPING = {
        REMOVE_DUPES: ['-c', 'sh:handler=remove'],
        LINK_DUPES: ['-c', 'sh:handler=link'],
        SYMLINK_DUPES: ['-c', 'sh:handler=symlink'],
        HARDLINK_DUPES: ['-c', 'sh:handler=hardlink'],
    }


class CrossMountType(Enum):
    """Key: traverse-cross-mounts"""
    OFF, ACTIVE = False, True
    MAPPING = {
        ACTIVE: ['--crossdev'],
        OFF: ['--no-crossdev']
    }


def map_cfg(option, val):
    """Helper function to save some characters"""
    return option.MAPPING.value.get(val, [])


def _create_rmlint_process(
        cfg, cwd, untagged, tagged, replay_path=None, outputs=None
):
    """Create a correctly configured rmlint GSuprocess for gui purposes.
    If `replay_path` is not None, "--replay `replay_path`" will be appended.
    """
    try:
        launcher = Gio.SubprocessLauncher.new(
            Gio.SubprocessFlags.STDOUT_PIPE | Gio.SubprocessFlags.STDERR_PIPE
        )

        launcher.set_cwd(cwd)

        extra_options = [
            map_cfg(MatchType,
                    cfg.get_enum('traverse-match')),
            map_cfg(SymlinkType,
                    cfg.get_enum('general-find-symlinks')),
            map_cfg(HiddenType,
                    cfg.get_enum('traverse-hidden')),
            map_cfg(KeepAllType,
                    cfg.get_enum('computation-keep-all-tagged')),
            map_cfg(MustMatchType,
                    cfg.get_enum('computation-must-match-tagged')),
            map_cfg(HandlerType,
                    cfg.get_enum('general-handler-type')),
            map_cfg(HardlinkType,
                    cfg.get_boolean('general-find-hardlinks')),
            map_cfg(CrossMountType,
                    cfg.get_boolean('traverse-cross-mounts')),
            map_cfg(AlgorithmType,
                    cfg.get_enum('computation-algorithm'))
        ]

        # Flatten list:
        extra_options = [item for sublist in extra_options for item in sublist]

        min_size, max_size = cfg.get_value('traverse-size-limits')
        extra_options += [
            '--size', '{a}-{b}'.format(
                a=min_size,
                b=max_size
            )
        ]

        extra_options += [
            '--max-depth', str(cfg.get_int('traverse-max-depth'))
        ]

        if replay_path:
            extra_options += ['--replay', replay_path]

        if outputs:
            for output, path in outputs or []:
                extra_options += [
                    '-o', ':'.join([output, path])
                ]
        else:
            # Default to json parsing.
            extra_options += [
                '-o', 'json',
                '-c', 'json:oneline',
            ]

        # Get rid of empty options:
        extra_options = [opt for opt in extra_options if opt]

        cmdline = [
            'rmlint',
            '--no-with-color',
            '-T', 'duplicates'
        ] + extra_options + untagged

        if tagged:
            cmdline.append('//')
            cmdline += tagged

        LOGGER.info('Running: ' + ' '.join(cmdline))
        process = launcher.spawnv(cmdline)
    except GLib.Error as err:
        if err.code == errno.ENOEXEC:
            LOGGER.error('Error: rmlint does not seem to be installed.')
        else:
            LOGGER.exception('Process failed')

        # No point in going any further
        return None
    else:
        return process


class Runner(GObject.Object):
    """Wrapper class for a process of rmlint."""
    __gsignals__ = {
        'lint-added': (GObject.SIGNAL_RUN_FIRST, None, ()),
        'replay-finished': (GObject.SIGNAL_RUN_FIRST, None, ()),
        'process-finished': (GObject.SIGNAL_RUN_FIRST, None, (str, ))
    }

    def __init__(self, settings, untagged_paths, tagged_paths):
        GObject.Object.__init__(self)

        self.settings = settings
        self.tagged_paths = tagged_paths
        self.untagged_paths = untagged_paths
        self._data_stream = self.process = self._message = None

        # Temporary directory for storing formatted files
        self._tmpdir = tempfile.TemporaryDirectory(prefix='shredder-')

        # Metadata about the run:
        self.element, self.header, self.footer = {}, {}, {}
        self.objects = []
        self.was_replayed = False

    def on_process_termination(self, process, result):
        """Called once GSuprocess sees its child die."""
        # We dont emit process-finished yet here.
        # We still might get some items from the stream.
        # Call process-finished once we hit EOF.

        try:
            process.wait_check_finish(result)
        except GLib.Error as err:
            # Try to read what went wrong from stderr.
            # Do not read everything - cut after 4096 bytes.
            bytes_ = process.get_stderr_pipe().read_bytes(4096)
            if bytes_ and bytes_.get_size():
                self._message = bytes_.get_data().decode('utf-8')
            else:
                # Nothing concrete to say it seems
                self._message = err.message

    def on_replay_finish(self, process, result):
        """Called once rmlint --replay finished running."""
        try:
            process.wait_check_finish(result)
            LOGGER.info('`rmlint --replay` finished.')
        except GLib.Error:
            LOGGER.exception('Replay process failed')
        finally:
            self.emit('replay-finished')

    def _queue_read(self):
        """Schedule an async read on process's stdout"""
        if self.process is None:
            return

        self._data_stream.read_line_async(
            io_priority=GLib.PRIORITY_HIGH,
            cancellable=None,
            callback=self.on_io_event
        )

    def on_io_event(self, source, result):
        """Called on every async io event."""
        line, _ = source.read_line_finish_utf8(result)

        # last block of data it seems:
        if not line:
            self.emit('process-finished', self._message)
            self._message = None
            self.process = None
            return

        line = line.strip(', ')
        if line in ['[', ']']:
            self._queue_read()
            return

        try:
            json_doc = json.loads(line)
        except ValueError:
            LOGGER.exception('Parsing json document failed')
        else:
            if 'path' in json_doc:
                self.element = json_doc
                self.emit('lint-added')
            elif 'description' in json_doc:
                self.header = json_doc
            elif 'aborted' in json_doc:
                self.footer = json_doc

            self.objects.append(json_doc)

        # Schedule another read:
        self._queue_read()

    def run(self):
        """Trigger the run of the rmlint process.
        Returns: a `Script` instance.
        """
        self.was_replayed = False
        self.process = _create_rmlint_process(
            self.settings, self._tmpdir.name,
            self.untagged_paths, self.tagged_paths
        )
        self._data_stream = Gio.DataInputStream.new(
            self.process.get_stdout_pipe()
        )

        # We want to get notified once the child dies
        self.process.wait_check_async(None, self.on_process_termination)

        # Schedule some reads from stdout (where the json gets written)
        self._queue_read()

    def get_json_path(self):
        """Return /tmp/.../shredder.json if replay() was called in prior."""
        return os.path.join(self._tmpdir.name, 'shredder.json')

    def get_csv_path(self):
        """Return /tmp/.../shredder.csv if replay() was called in prior."""
        return os.path.join(self._tmpdir.name, 'shredder.csv')

    def get_sh_path(self):
        """Return /tmp/.../shredder.sh if replay() was called in prior."""
        return os.path.join(self._tmpdir.name, 'shredder.sh')

    def replay(self, allowed_paths=None):
        """Replay the last run using --replay.

        Together with `allowed_paths` this allows easy filtering
        and re-formatting of the outputted files.
        `allowed_paths` is a dictionary of paths to booleans (is_original).
        """
        # Filter the data points we want in the result.

        try:
            header, *data, footer = self.objects
        except ValueError:
            # Not enough values to unpack:
            LOGGER.exception('Could not replay')
            return

        self.was_replayed = True
        results = [header]

        for point in data:
            path = point['path']
            if path is None:
                continue

            if allowed_paths is None or path in allowed_paths:
                point['is_original'] = allowed_paths[path]
                results.append(point)

        results.append(footer)

        # Write the replay script to filter the input.
        replay_path = os.path.join(self._tmpdir.name, 'shredder.replay.json')
        with open(replay_path, 'w') as handle:
            handle.write(json.dumps(results))

        # Regenerate output formatters by calling rmlint:
        process = _create_rmlint_process(
            self.settings,
            self._tmpdir.name,
            self.untagged_paths, self.tagged_paths,
            replay_path=replay_path,
            outputs=[
                ('sh', self.get_sh_path()),
                ('csv', self.get_csv_path()),
                ('json', self.get_json_path())
            ]
        )
        process.wait_check_async(None, self.on_replay_finish)

    def save(self, dest_path, file_type='sh'):
        """Save the output to `path`.
        The script can be converted to a different format if necessary.
        Valid formats are:

            - sh
            - json
            - csv
        """
        if not self.was_replayed:
            LOGGER.error('Cannot save. replay() was not called.')
            return

        source_path_func = {
            'sh': self.get_sh_path,
            'csv': self.get_csv_path,
            'json': self.get_json_path
        }.get(file_type)

        if source_path_func is None:
            LOGGER.error('No valid file type `%s`.', file_type)
            return

        try:
            source_path = source_path_func()
            shutil.copy(source_path, dest_path)
            if file_type == "sh":
                _fix_shell_auto_remove_path(dest_path, source_path)
        except OSError:
            LOGGER.exception('Could not save')


def _fix_shell_auto_remove_path(sh_path, temp_path):
    """
    Shell scripts contain the path that they were created under.
    This is used to remove the script after a successful run.
    """
    with open(sh_path, "r") as fd:
        text = fd.read()

    with open(sh_path, "w") as fd:
        fd.write(text.replace(temp_path, sh_path))


def _strip_ascii_colors(text):
    """Strip ascii colors from `text`"""
    return ASCII_COLOR_REGEX.sub('', text)


class Script(GObject.Object):
    """Wrapper around the shell script generated by an rmlint run.
    `run()` will execute the script (either dry or for real)
    """
    __gsignals__ = {
        'line-read': (GObject.SIGNAL_RUN_FIRST, None, (str, str)),
        'script-finished': (GObject.SIGNAL_RUN_FIRST, None, ())
    }

    def __init__(self, script_file):
        GObject.Object.__init__(self)
        self._incomplete_chunk = self._process = self._stream = None
        self.script_file = script_file

    @staticmethod
    def create_dummy():
        """Create an empty dummy script for testing purpose"""
        _, path = tempfile.mkstemp(prefix='.')
        with open(path, 'w') as handle:
            handle.write('#!/bin/sh\n\nBleep!\nBlop!\nIm a bot!')

        return Script(path)

    def read(self):
        """Read the script from disk and return it as string.
        """
        # Do not reuse the file descriptor, since it is only valid once.
        # Be a bit careful, since the script might contain weird encoding,
        # since there is no path encoding guaranteed in Unix usually:
        opts = dict(encoding='utf-8', errors='ignore')
        with codecs.open(self.script_file, 'r', **opts) as handle:
            return _strip_ascii_colors(handle.read())

    def read_bytes(self):
        """Same as read() but do not not attempt conversion to string.
        Return raw bytes instead.
        """
        with open(self.script_file, 'rb') as handle:
            return handle.read()

    def run(self, dry_run=True):
        """Run the script.
        Will trigger a `line-read` signal for each line it processed
        and one `script-finished` signal once all lines are done.
        """
        flags = Gio.SubprocessFlags

        self._process = Gio.Subprocess.new(
            [self.script_file, '-d', '-x', '-q', '-p', '-n' if dry_run else ''],
            flags.STDERR_SILENCE | flags.STDOUT_PIPE
        )
        self._stream = None
        self._queue_read()

    def _queue_read(self):
        """Schedule a read from rmlint's stdout stream."""
        if self._stream is None:
            self._stream = Gio.DataInputStream.new(
                self._process.get_stdout_pipe()
            )

        self._stream.read_line_async(
            io_priority=GLib.PRIORITY_HIGH,
            cancellable=None,
            callback=self._read_chunk
        )

    def _report_line(self, line):
        """Postprocess and signal the receival of a single line."""
        if not line or line.strip().startswith("#"):
            return

        line_split = line.split(':', maxsplit=1)
        if len(line_split) < 2:
            LOGGER.warning('Invalid line fed: ' + line)
            return

        prefix, path = line_split
        prefix = _strip_ascii_colors(prefix)
        path = _strip_ascii_colors(path)

        self.emit('line-read', prefix.strip(), path.strip())

    def _read_chunk(self, source, result):
        """Called once a new line is ready to be read."""
        try:
            line, _ = source.read_line_finish_utf8(result)
        except GLib.Error:
            LOGGER.exception('Could not read line from script:')
            self.emit('script-finished')
            return

        if line:
            self._report_line(line)
            self._queue_read()
        else:
            self.emit('script-finished')


if __name__ == '__main__':
    def main():
        """Stupid test main: Run on /usr."""
        settings = Gio.Settings.new('org.gnome.Shredder')
        loop = GLib.MainLoop()

        runner = Runner(settings, ['/usr/'], [])
        runner.connect('lint-added', lambda _: print(runner.element))
        runner.connect(
            'process-finished',
            lambda _, msg: print('Status:', msg)
        )
        runner.connect('process-finished', lambda *_: loop.quit())
        runner.run()

        try:
            loop.run()
        except KeyboardInterrupt:
            pass

    main()
