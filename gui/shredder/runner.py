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
import json
import errno
import codecs
import logging
import tempfile

from enum import Enum

# Internal:
from shredder import APP_TITLE

# External:
from gi.repository import Gio
from gi.repository import GLib
from gi.repository import GObject


LOGGER = logging.getLogger('runner')


class AlgorithmType(Enum):
    """Key: computation-algorithm"""
    SPOOKY, CITY, SHA1, SHA256, SHA512, MD5, PARANOID = range(1, 8)
    MAPPING = {
        SPOOKY:   ['--algorithm', 'spooky'],
        CITY:     ['--algorithm', 'city'],
        SHA1:     ['--algorithm', 'sha1'],
        SHA256:   ['--algorithm', 'sha256'],
        SHA512:   ['--algorithm', 'sha512'],
        MD5:      ['--algorithm', 'md5'],
        PARANOID: ['--algorithm', 'paranoid']
    }


class MatchType(Enum):
    """Key: traverse-match"""
    NONE, BASENAME, EXTENSION, WITHOUT_EXTENSION = range(1, 5)
    MAPPING = {
        NONE: '',
        BASENAME: '--match-basename',
        EXTENSION: '--match-with-extension',
        WITHOUT_EXTENSION: '--match-without-extension'
    }


class SymlinkType(Enum):
    """Key: general-find-symlinks"""
    IGNORE, SEE, FOLLOW = range(1, 4)
    MAPPING = {
        IGNORE: '--no-followlinks',
        SEE: '--see-symlinks',
        FOLLOW: '--followlinks'
    }


class HiddenType(Enum):
    """Key: traverse-hidden"""
    IGNORE, PARTIAL, FOLLOW = range(1, 4)
    MAPPING = {
        IGNORE: '--no-hidden',
        PARTIAL: '--partial-hidden',
        FOLLOW: '--hidden'
    }


class KeepAllType(Enum):
    """Key: computation-keep-all-tagged"""
    NONE, TAGGED, UNTAGGED = range(1, 4)
    MAPPING = {
        NONE: '',
        TAGGED: '--keep-all-tagged',
        UNTAGGED: '--keep-all-untagged'
    }


class MustMatchType(Enum):
    """Key: computation-must-match-tagged"""
    NONE, TAGGED, UNTAGGED = range(1, 4)
    MAPPING = {
        NONE: '',
        TAGGED: '--must-match-tagged',
        UNTAGGED: '--must-match-untagged'
    }


class HardlinkType(Enum):
    """Key: "general-find-hardlinks"""
    OFF, ACTIVE = False, True
    MAPPING = {
        ACTIVE: '--hardlinked',
        OFF: '--no-hardlinked'
    }


class CrossMountType(Enum):
    """Key: traverse-cross-mounts"""
    OFF, ACTIVE = False, True
    MAPPING = {
        ACTIVE: '--crossdev',
        OFF: '--no-crossdev'
    }


def map_cfg(option, val):
    """Helper function to save some characters"""
    return option.MAPPING.value.get(val)


def _create_rmlint_process(cfg, paths):
    """Create a correctly configured rmlint GSuprocess for gui purposes.

    Returns the working directory of the run and the actual process instance.
    """
    cwd = tempfile.mkdtemp(suffix=APP_TITLE, prefix='.tmp')

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
            map_cfg(HardlinkType,
                    cfg.get_boolean('general-find-hardlinks')),
            map_cfg(CrossMountType,
                    cfg.get_boolean('traverse-cross-mounts'))
        ]

        min_size, max_size = cfg.get_value('traverse-size-limits')
        extra_options += [
            '--size', '{a}M-{b}M'.format(
                a=min_size // (1024 ** 2),
                b=max_size // (1024 ** 2)
            )
        ]

        extra_options += map_cfg(
            AlgorithmType,
            cfg.get_enum('computation-algorithm')
        )

        extra_options += [
            '--max-depth', str(cfg.get_int('traverse-max-depth'))
        ]

        # Get rid of empty options:
        extra_options = [opt for opt in extra_options if opt]

        # Find a place to put the script file:
        sh_file = tempfile.NamedTemporaryFile(
            suffix='.sh', delete=False
        )

        cmdline = [
            'rmlint',
            '--no-with-color',
            # '--merge-directories',  # TODO: Disable for now.
            '-o', 'sh:' + sh_file.name,
            '-o', 'json',
            '-c', 'json:oneline',
            '-T', 'duplicates'
        ] + extra_options + paths

        LOGGER.info('Running: ' + ' '.join(cmdline))
        process = launcher.spawnv(cmdline)
    except GLib.Error as err:
        if err.code == errno.ENOEXEC:
            LOGGER.error('Error: rmlint does not seem to be installed.')
        else:
            LOGGER.exception('Process failed')

        # No point in going any further
        return None, None, None
    else:
        return cwd, process, sh_file.name
    finally:
        sh_file.close()


class Runner(GObject.Object):
    """Wrapper class for a process of rmlint."""
    __gsignals__ = {
        'lint-added': (GObject.SIGNAL_RUN_FIRST, None, ()),
        'process-finished': (GObject.SIGNAL_RUN_FIRST, None, (str, ))
    }

    def __init__(self, settings, paths):
        GObject.Object.__init__(self)

        self.settings, self.paths = settings, paths
        self._data_stream = self.process = self._message = None

        # Working directory
        self.cwd = None

        # Metadata about the run:
        self.element, self.header, self.footer = {}, {}, {}

    def on_process_termination(self, process, result):
        """Called once GSuprocess sees it's child die."""
        # We dont emit process-finished yet here.
        # We still might get some items from the stream.
        # Call process-finished once we hit EOF.

        try:
            process.wait_check_finish(result)
        except GLib.Error as err:
            # Try to read what went wrong from stderr.
            # Do not read everything - cut after 4096 bytes.
            bytes_ = self.process.get_stderr_pipe().read_bytes(4096)
            if bytes_ and bytes_.get_size():
                self._message = bytes_.get_data().decode('utf-8')
            else:
                # Nothing concrete to say it seems
                self._message = err.message

    def _queue_read(self):
        """Schedule a async read on process's stdout"""
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

        # Schedule another read:
        self._queue_read()

    def run(self):
        """Trigger the run of the rmlint process.
        Returns: a `Script` instance.
        """
        self.cwd, self.process, sh_script = _create_rmlint_process(
            self.settings, self.paths
        )
        self._data_stream = Gio.DataInputStream.new(
            self.process.get_stdout_pipe()
        )

        # We want to get notified once the child dies
        self.process.wait_check_async(None, self.on_process_termination)

        # Schedule some reads from stdout (where the json gets written)
        self._queue_read()

        # Return the script associated with the run:
        return Script(sh_script)


class Script(GObject.Object):
    """Wrapper around the shell script generated by a rmlint run.
    `run()` will execute the script (either dry or for real)
    """
    __gsignals__ = {
        'line-read': (GObject.SIGNAL_RUN_FIRST, None, (str, str)),
        'script-finished': (GObject.SIGNAL_RUN_FIRST, None, ())
    }

    def __init__(self, script_file):
        GObject.Object.__init__(self)
        self._incomplete_chunk = self._process = None
        self.script_file = script_file

    def read(self):
        """Read the script from disk and return it as string.
        """
        # Do not reuse the file descriptor, since it is only valid once.
        # Be a bit careful, since the script might contain weird encoding,
        # since there is no path encoding guaranteed in Unix usually:
        opts = dict(encoding='utf-8', errors='ignore')
        with codecs.open(self.script_file, 'r', **opts) as handle:
            return handle.read()

    def run(self, dry_run=True):
        """Run the script.
        Will trigger a `line-read` signal for each line it processed
        and one `script-finished` signal once all lines are done.
        """
        flags = Gio.SubprocessFlags

        self._process = Gio.Subprocess.new(
            [self.script_file, '-d', '-x', '-n' if dry_run else ''],
            flags.STDERR_SILENCE | flags.STDOUT_PIPE
        )
        self._queue_read()

    def _queue_read(self):
        """Schedule a read from rmlint's stdout stream."""
        stream = Gio.DataInputStream.new(
            self._process.get_stdout_pipe()
        )

        stream.read_line_async(
            io_priority=GLib.PRIORITY_HIGH,
            cancellable=None,
            callback=self._read_chunk
        )

    def _report_line(self, line):
        """Postprocess and signal the receival of a single line."""
        if not line:
            return

        line_split = line.split(':', maxsplit=1)
        if len(line_split) < 2:
            LOGGER.warning('Invalid line fed: ' + line)
            return

        prefix, path = line_split
        self.emit('line-read', prefix.strip(), path.strip())

    def _read_chunk(self, source, result):
        """Called once a new line is ready to be read."""
        try:
            line, _ = source.read_line_finish_utf8(result)
        except GLib.Error:
            LOGGER.exception('Could not read line from script:')
            return

        if not line:
            self.emit('script-finished')
            return

        self._report_line(line)
        self._queue_read()


if __name__ == '__main__':
    def main():
        """Stupid test main."""
        settings = Gio.Settings.new('org.gnome.Shredder')
        loop = GLib.MainLoop()

        runner = Runner(settings, ['/usr/'])
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
