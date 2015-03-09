#!/usr/bin/env python
# encoding: utf-8

# Stdlib:
import json
import errno
import tempfile

from collections import defaultdict
from functools import partial

# Internal:
from app import APP_TITLE

# External:
from gi.repository import Gio
from gi.repository import GLib
from gi.repository import GObject


# Determines the chunksize in which the json output is parsed.
# Higher values are more performant but lower ones trigger
# updates more often and leads to a more fluent user interface.
IO_BUFFER_SIZE = 1024

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
        BASENAME: '---match-basename',
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


def _create_rmlint_process(settings, paths):
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
            MatchType.MAPPING.get(
                settings.get_enum('traverse-match')
            ),
            SymlinkType.MAPPING.get(
                settings.get_enum('general-find-symlinks')
            ),
            HiddenType.MAPPING.get(
                settings.get_enum('traverse-hidden')
            ),
            KeepAllType.MAPPING.get(
                settings.get_enum('computation-keep-all-tagged')
            ),
            MustMatchType.MAPPING.get(
                settings.get_enum('computation-must-match-tagged')
            ),
            HardlinkType.MAPPING.get(
                settings.get_boolean('general-find-hardlinks')
            ),
            CrossMountType.MAPPING.get(
                settings.get_boolean('traverse-cross-mounts')
            )
        ]

        extra_options += AlgorithmType.MAPPING.get(
            settings.get_enum('computation-algorithm')
        )

        print(settings.get_int('traverse-max-depth'))
        extra_options += [
            '--max-depth', str(settings.get_int('traverse-max-depth'))
        ]

        extra_options = list(filter(None, extra_options))
        print(extra_options)

        process = launcher.spawnv([
            'rmlint',
            '-o', 'sh:rmlint.sh',
            '-o', 'json',
            '-c', 'json:oneline',
            '-T', 'duplicates'
        ] + extra_options + paths)
    except GLib.Error as err:
        if err.code == errno.ENOEXEC:
            print('rmlint does not seem to be installed.')
        else:
            print(err)

        # No point in going any further
        return None, None
    else:
        return cwd, process


def _parse_json_chunk(chunk):
    incomplete_chunk, results = None, []
    decoder = json.JSONDecoder()

    for line in chunk.splitlines():
        line = line.strip()
        if line.startswith('[') or line.startswith(']'):
            continue

        try:
            json_doc, _ = decoder.raw_decode(line)
            if isinstance(json_doc, dict):
                results.append(json_doc)
            else:
                raise ValueError('')
        except ValueError as err:
            incomplete_chunk = line
            break

    return results, incomplete_chunk


class Lint(GObject.Object):
    def __init__(self, data):
        GObject.Object.__init__(self)
        self.data, self.silbings = data, []

    def __getattr__(self, key):
        return self.data.get(key)

    def __iter__(self):
        return iter(self.silbings)


class Runner(GObject.Object):
    __gsignals__ = {
        'lint-added': (GObject.SIGNAL_RUN_FIRST, None, (Lint, )),
        'process-finished': (GObject.SIGNAL_RUN_FIRST, None, (str, ))
    }

    def __init__(self, settings, paths):
        GObject.Object.__init__(self)

        self.settings, self.paths = settings, paths

        self._incomplete_chunk = self._process = self._message = None

        # True when the underlying process is no longer alive.
        # Note: This does not mean parsing is finished yet!
        self.is_running = False

        # Working directory
        self.cwd = None

        # Metadata about the run:
        self.header, self.footer = {}, {}

        # Result list
        self.results = []

        self._last_original = None

    def _on_process_termination(self, process, result):
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

        # Remember that this process is done.
        self.is_running = False


    def _queue_read(self):
        """Schedule a async read on process's stdout"""
        if self.process is None:
            return

        stream = self.process.get_stdout_pipe()
        stream.read_bytes_async(
            IO_BUFFER_SIZE,
            io_priority=0,
            cancellable=None,
            callback=self._on_io_event
        )

    def _on_io_event(self, source, result):
        data = source.read_bytes_finish(result).get_data()

        # last block of data it seems:
        if not data:
            if self._last_original is not None:
                self.emit('lint-added', self._last_original)
            self.emit('process-finished', self._message)
            self._message = None
            return

        data = data.decode('utf-8')

        # There was some leftover data from the last read:
        if self._incomplete_chunk is not None:
            data = self._incomplete_chunk + data
            self._incomplete_chunk = None

        # Try to find sense in the individual chunks:
        results, self._incomplete_chunk = _parse_json_chunk(data)
        # print(results)

        for json_doc in results:
            if 'path' in json_doc:
                lint = Lint(json_doc)
                if lint.is_original:
                    if self._last_original is not None:
                        self.emit('lint-added', self._last_original)
                    self.results.append(lint)
                    self._last_original = lint

                if lint is not self._last_original:
                    self._last_original.silbings.append(lint)
            elif 'aborted' in json_doc:
                self.footer = json_doc
            elif 'description' in json_doc:
                self.header = json_doc

        # Schedule another read:
        self._queue_read()

    def run(self):
        self.cwd, self.process = _create_rmlint_process(self.settings, self.paths)

        # We want to get notified once the child dies
        self.process.wait_check_async(None, self._on_process_termination)

        # queue_read checks this.
        self.is_running = True

        # Schedule some reads from stdout (where the json gets written)
        self._queue_read()


if __name__ == '__main__':
    settings = Gio.Settings.new('org.gnome.Rmlint')

    runner = Runner(settings, ['/usr/bin'])
    runner.connect('lint-added', lambda _, e: print([x.path for x in e]))
    runner.connect('process-finished', lambda _, m: print(m, len(runner.results)))
    runner.run()

    loop = GLib.MainLoop()
    try:
        loop.run()
    except KeyboardInterrupt:
        pass
