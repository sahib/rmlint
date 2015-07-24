#!/usr/bin/env python
# encoding: utf-8

# Stdlib:
import json
import errno
import tempfile

from collections import defaultdict, UserDict
from functools import partial

# Internal:
from app import APP_TITLE

# External:
from gi.repository import Gio
from gi.repository import GLib
from gi.repository import GObject


# TODO: Move to settings?
# TODO: Document relation to schema.xml
# TODO: --limits
# TODO: Restore defaults?

class AlgorithmType:
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

class MatchType:
    NONE, BASENAME, EXTENSION, WITHOUT_EXTENSION = range(1, 5)
    MAPPING = {
        NONE: '',
        BASENAME: '--match-basename',
        EXTENSION: '--match-with-extension',
        WITHOUT_EXTENSION: '--match-without-extension'
    }


class SymlinkType:
    IGNORE, SEE, FOLLOW = range(1, 4)
    MAPPING = {
        IGNORE: '--no-followlinks',
        SEE: '--see-symlinks',
        FOLLOW: '--followlinks'
    }


class HiddenType:
    IGNORE, PARTIAL, FOLLOW = range(1, 4)
    MAPPING = {
        IGNORE: '--no-hidden',
        PARTIAL: '--partial-hidden',
        FOLLOW: '--hidden'
    }


class KeepAllType:
    NONE, TAGGED, UNTAGGED = range(1, 4)
    MAPPING = {
        NONE: '',
        TAGGED: '--keep-all-tagged',
        UNTAGGED: '--keep-all-untagged'
    }


class MustMatchType:
    NONE, TAGGED, UNTAGGED = range(1, 4)
    MAPPING = {
        NONE: '',
        TAGGED: '--must-match-tagged',
        UNTAGGED: '--must-match-untagged'
    }


class HardlinkType:
    OFF, ON = False, True
    MAPPING = {
        ON: '--hardlinked',
        OFF: '--no-hardlinked'
    }


class CrossMountType:
    OFF, ON = False, True
    MAPPING = {
        ON: '--crossdev',
        OFF: '--no-crossdev'
    }


def map_cfg(option, val):
    return option.MAPPING.get(val)


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
            map_cfg(MatchType, cfg.get_enum('traverse-match')),
            map_cfg(SymlinkType, cfg.get_enum('general-find-symlinks')),
            map_cfg(HiddenType, cfg.get_enum('traverse-hidden')),
            map_cfg(KeepAllType, cfg.get_enum('computation-keep-all-tagged')),
            map_cfg(MustMatchType, cfg.get_enum('computation-must-match-tagged')),
            map_cfg(HardlinkType, cfg.get_boolean('general-find-hardlinks')),
            map_cfg(CrossMountType, cfg.get_boolean('traverse-cross-mounts'))
        ]

        extra_options += AlgorithmType.MAPPING.get(
            cfg.get_enum('computation-algorithm')
        )

        extra_options += [
            '--max-depth', str(cfg.get_int('traverse-max-depth'))
        ]

        extra_options = list(filter(None, extra_options))
        sh_file = tempfile.NamedTemporaryFile(
            suffix='.sh', delete=False
        )

        cmdline = [
            'rmlint',
            '--no-with-color',
            '--merge-directories',
            '-o', 'sh:' + sh_file.name,
            '-o', 'json',
            '-c', 'json:oneline',
            '-T', 'duplicates'
        ] + extra_options + paths

        print(' '.join(cmdline))
        process = launcher.spawnv(cmdline)
    except GLib.Error as err:
        if err.code == errno.ENOEXEC:
            print('Error: rmlint does not seem to be installed.')
        else:
            print(err)

        # No point in going any further
        return None, None, None
    else:
        return cwd, process, sh_file.name
    finally:
        sh_file.close()


class Runner(GObject.Object):
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

    def _on_process_termination(self, process, result):
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
            callback=self._on_io_event
        )

    def _on_io_event(self, source, result):
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
        except ValueError as err:
            print(err)
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
        self.cwd, self.process, sh_script = _create_rmlint_process(
            self.settings, self.paths
        )
        self._data_stream = Gio.DataInputStream.new(
            self.process.get_stdout_pipe()
        )

        # We want to get notified once the child dies
        self.process.wait_check_async(None, self._on_process_termination)

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
        with open(self.script_file, 'r') as f:
            return f.read()

    def run(self, dry_run=True):
        """Run the script.
        Will trigger a `line-read` signal for each line it processed
        and one `script-finished` signal once all lines are done.
        """
        self._process = Gio.Subprocess.new(
            [self.script_file, '-d', '-x', '-n' if dry_run else ''],
            Gio.SubprocessFlags.STDOUT_PIPE | Gio.SubprocessFlags.STDERR_SILENCE
        )
        self._queue_read()

    def _queue_read(self):
        stream = self._process.get_stdout_pipe()
        stream.read_bytes_async(16 * 1024, 0, callback=self._read_chunk)

    def _report_line(self, line):
        if not line:
            return

        prefix, path = line.split(':', maxsplit=1)
        self.emit('line-read', prefix, path)

    def _read_chunk(self, stdout, result):
        bytes_ = stdout.read_bytes_finish(result)
        data = bytes_.get_data()

        if not data:
            self._report_line(self._incomplete_chunk)
            self.emit('script-finished')
            return

        try:
            chunk = data.decode('utf-8')
        except UnicodeDecodeError:
            pass

        if self._incomplete_chunk:
            chunk = self._incomplete_chunk + chunk
            self._incomplete_chunk = None

        *lines, self._incomplete_chunk = chunk.splitlines()
        for line in lines:
            self._report_line(line)

        self._queue_read()


if __name__ == '__main__':
    settings = Gio.Settings.new('org.gnome.Rmlint')
    loop = GLib.MainLoop()

    runner = Runner(settings, ['/usr/'])
    runner.connect('lint-added', lambda _: print(runner.element))
    runner.connect('process-finished', lambda _, msg: print('Status:', msg))
    runner.connect('process-finished', lambda *_: loop.quit())
    runner.run()

    try:
        loop.run()
    except KeyboardInterrupt:
        pass
