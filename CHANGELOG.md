# Change Log

All notable changes to this project will be documented in this file.

The format follows [keepachangelog.com]. Please stick to it.

## [2.10.0 Odd Olm] -- Unreleased

### Added

* ``--replay`` works now with ``-D``: ``.json`` created without ``-D`` can now
  be packed into duplicate directories during replay. Vice versa, duplicate
  directories can be unpacked if no ``-D`` switch was given.

### Changed

* Related to the ``--replay`` feature: The ``.json`` output now contains
  ``part_of_directory`` entries if generated with ``-D``. Those are basically
  duplicates the duplicate directories are composed out of.

### Deprecated

Nothing.

### Removed

Nothing.

### Fixed

* The ``mtime`` comparison was broken when using ``--xattr``.
* Timestamps for ``xattr`` were locale-sensitive. This has been fixed.
* Intermediate checksums where written to extended attributes.
* Several Python related deprecation warnings.
* ui: Changing the size factor did not let you apply the settings.
* ``--size`` can now correctly handle very large sizes.

## [2.9.0 Odd Olm] -- 2019-08-20

### Added

* An up-to-date COPR package for Fedora (thanks eclipseo).
* Add --xattr as --xattr-write, --xattr-read & --write-unfinished to enable easy
  checksum caching for the next run.
* Unique option for json and csv formatters (-c json:unique)
* New -k option for rmlint.sh to keep directory timestamps like before deletion.

### Changed

* Warn when using -j without -D.
* The exist status is now EXIT_FAILURE when rmlint was interrupted.
* Slightly changed progressbar ETA algorithm to deliver more stable results.
* docs: added a few scripting examples to underline the possibility to use 3rd-party tools.
* --dedupe options now checks if the files are already reflinks to reduce disk thrashing.
* Made sure that -T dd is the same as specifying -D.

### Deprecated

Nothing was deprecated.

### Removed

Nothing was removed.

### Fixed

* Dry-run was not honoured when a user-defined command was used.
* Fix crash when compiled with -msse4.2
* Relative symbolic links were compared wrong when using -f (See issue #333)
* gui: The size options wer enot correctly passed to rmlint (only Megabytes)
* xattr: timestamps where often compared wrongly.
* Fiemap (and thus reflink detection) code was improved.
* --replay did not replay duplicate directories correctly.
* --replay did not honour --hidden and --size correctly.
* Various build issues.
* gui: fix "render from selected" feature.
* gui: generated script now correctly removes itself.
* gui: fix display of paths with ampersands in them.

## [2.8.0 Maidenly Moose] -- 2018-10-30

Mostly a bugfix release with smaller functional changes.

### Added

- Support for reflinks on XFS (48c2e5b1)
- Option "--no-backup" to overwrite output files as before (see "Changed")
- New build system option called "gettext" that eases the maintenance of translations.
  (thanks Michael Witten)
- gui: Add config option to choose how to handle the found duplicates

### Changed

- -pp is now silently permitted, but is the same as -p
- Old output files get backed up now to a new timestamped name.
  This was done to not to overwrite the results of long runs.
  You can use --no-backup to disable this behaviour.
- Several internal cleanups and potential bug fixes (thanks to Michael Witten)
- Change the default optimization level for a build to -O2.

### Deprecated

Nothing was deprecated.

### Removed

Nothing was removed.

### Fixed

- Several small fixes in the documentation.
- Compiler warnings that may happen with clang and on 32 bit.
- "-g" now does not overwrite other formatters anymore.
- Make the progressbar less flickering.
- Startup error when the permissions to a location where bad.
- Endless loop that happens on btrfs due to the fiemap optimization.
- Fix a bug when doing "rmlint --replay x.json" without an explicit path.
- Fix -f that did not really follow symbolic links.
- gui: locations are now stored persistently and survive restarts.
- scons should work now with both python2 and python3.
- extensive memory allocation with slow CPUs.
- Do not use --remove-destination of cp, but use "rm + ln" to support non-GNU systems.

## [2.7.0 Toothless Taipan] -- 2017-04-25

### Added

* New checksum types metro and highway
* New option --keep-hardlinked
* --dedupe option can deduplicate twins on any reflick-capable filesystems
* --dedupe-readonly option can dedupe files on read-only btrfs snapshots

### Changed

* Checksum types for -P... options (see https://github.com/sahib/rmlint/issues/261)

### Deprecated

* Option --btrfs-clone (use --dedupe)
* Paranoia option -pp (use -p)

### Removed

* Checksum types bastard, spooky, city & farmhash
* Multihash output option

### Fixed

* Fix scons 3 compatibility issue (https://github.com/sahib/rmlint/issues/258)
* Fix compile error on systems with no FIEMAP (https://github.com/sahib/rmlint/issues/252)
* Fix handling of bad uids/gids in python output formatter (https://github.com/sahib/rmlint/issues/239)
* Fix escaping of dirnames in rmlint.sh test for new emptydirs (https://github.com/sahib/rmlint/issues/241)
* Fix another quoting issue (https://github.com/sahib/rmlint/issues/272)

## [2.6.1 Penetrating Pineapple] -- 2017-06-13

### Fixed

* Fix build-error on macOS due to wrong usage of #ifdef.
* Fix a bug in treemerge that sometimes counted hardlinks more than once.
* Fix copyright year notice all over the source code.
* Fix --equal to allow hidden files, files counting as other lint and empty files.
* Fix --equal to allow path doubles so passing symlinks to the same file still yields a 0.
  Due to those fixes the paranoia mode of ``rmlint.sh`` should run smoother (again) now.
* Use a more sophisticated strategy to find the path to the own binary and fallback to ``$(which rmlint)``.
* Workaround the bash bug that did not allow huge functions by bringing back the old approach without a function.

### Changed

* It is now possible to treat empty files as duplicates when specifying "--size 0".
* Use the `cmp` builtin to compare regular in the shell script files again, since it's faster.
* Abort early in ``--equal`` if an mismatch has been encountered.
* Test the shell script more thoroughly to avoid release troubles.

### Added

* Add a new ``-c`` switch to ``rmlint.sh`` that cleans up empty directories while running the script.

## [2.6.0 Penetrating Pineapple] -- 2017-06-03

### Fixed

* Do not chmod +x files that exist already (previous versions tried to +x e.g. /dev/null)
* Support anonymous pipes in the sh formatter (Supported now: rmlint -o sh:>(cat))
* Documentation should look a bit cleaner now.
* Fixed a crash when a file was clamped to a size of 0.
* Fixed a rounding error bug that can happen while sorting mtimes.
* Fixed the slightly broken --mtime-window option (was too permissive in rare cases)
* Force permissions (0755 for binary, 0644 for manpage) when installing files in the filesystem.
* Fix wrong ordering when using -D without -k or -K.
* Fix a space wasting bug that happened with -D and -c sh:link.
* Fix a bug in treemerge that sometimes lead to directories not being detected (should be also slightly faster now)
* Fix inconsistent handling of duplicate directories when using hardlinks, symlinks and reflinks.
* Abort early if an invalid path has been given via commandline or stdin.
* Do not silence errors while commandline parsing when using ``-g``.

### Added

* Added new checksums: blake2{s,sp,b,bp} and sha3{-256,-384,-512}. sha3 is an alias to sha3-256.
* Support for GNU Hurd (well, at least it compiles :)
* New "-o stats" formatter that prints some statistics about the run.
* Progressbar now additionally shows an ETA time (how long rmlint will probably run)
* New option ``--honour-dir-layout (-j)`` that makes ``--merge-directories (-D)``
  also check if the directory layout is the same.
* A new ``--equal`` option that works like the ``cmp`` utility, but supports directories.
* Add a progress percentage to ``rmlint.sh`` for big runs.

### Changed

* Changed the default hash function to blake2b.
* The default formatter (pretty) now also outputs the time used for the run.
* Updated existing translations.

## [2.4.6 Myopic Micrathene Bugfixes] -- 2017-01-16

### Fixed

* Update translation template.
* Various fix to make rmlint ready to be packaged in debian.

### Added

* Output of rmlint.sh uses a colored prefix now.

## [2.4.5 Myopic Micrathene Bugfixes] -- 2016-12-12

### Fixed

* Make ``--replay`` truly merge different sets of duplicates.
* Call ``exit(1)`` when getting a fatal signal (somehow was missing)
* ``scons test`` now executes only the sane part of the testsuite.
* Be more friendly when no manpage was found (and show --help)
* Handle readonly btrfs subvolumes well.
  See also: https://github.com/sahib/rmlint/issues/195
* Various build errors fixed for old/rare systems.
* Various fixes in the gui, mostly related to old GTK versions.

### Added

* New option ``--mtime-window``: Only consider files as duplicates that
  share a mtime in a certain time-window.
  See also: https://github.com/sahib/rmlint/issues/197
* New sortcriteria ``O`` (maximize outside hardlinks) and ``H`` (maximize total hardlinks)
  See also: https://github.com/sahib/rmlint/issues/196
* Proper installation instructions for macOS.

### Changed

* Re-Design ``--replay`` to accept ``//`` like the normal commandline does.
* New default sortcriteria is ``pOMa`` to maximize the chance of deleting
  the most bytes from the storage.

## [2.4.4 Myopic Micrathene Bugfixes] -- 2016-04-01

### Fixed

* Makefile related compile troubles on BSD for the GUI>
* Check for accessat and ATR_FDCWD explicitly and try to work around them when missing.
* Clearup installation instructions

## [2.4.3 Myopic Micrathene Bugfixes] -- 2016-03-11

### Fixed

- Fix symbolic link emitting in sh script (sometimes files were omitted from rmlint.sh)
- Fix compile stop on BSD systems in utilities.c (thanks f99aq8ove)
- Fix some compiler warnings and typos.

### Added

- Add basic spanish translation.
- Add basic compile support on cygwin.

## [2.4.2 Myopic Micrathene Bugfixes] -- 2015-12-14

### Fixed

- Fix bad size stats using ``--replay`` with hardlinks.
- Fix unicode emission of ``json`` formatter.
- Fix broken ``gui/SConscript`` (was looking for python4...)

### Added

- Add ``unique`` formatter to print unique files. (See https://github.com/sahib/rmlint/issues/161)

### Changed

- Removed ``--with-metadata-cache`` and ``--cache`` since both introduced lots
  of code without giving many benefits. In total about 1000 lines of code were
  removed in this process.
- For limiting memory usage there is just ``--limit-mem`` now.


## [2.4.1 Myopic Micrathene Bugfixes] -- 2015-11-01

### Fixed

- ``btrfs``:  Do not try to clone files on different subvolumes.
- ``gui``: Tie down version for gobject-introspection.
- ``freebsd``: Fix very bad performance due to mounttable and some compile
  issues.

### Added

- ``-S`` now understands two new letters to match via regular expressions:

    - ``r``: Select the path as original that matches the pattern given in ``<PATTERN>`` after this letter.
    - ``x``: Same as ``r`` but match only basename.


## [2.4.0 Myopic Micrathene] -- 2015-10-25

### Fixed

- ``rmlint`` should compile on Mac OSX now.
- Bugfix: Broken ``chown`` calls in sh script (thanks Shukrat Mukimov)
- Bugfix: memory corruption when specifying ``-T dd`` alone.
- Bugfix: Make ``-D`` and ``-k / -K`` play together nicely (thanks phiresky).
- Smaller compile time troubles fixed.
- Progressbar uses timeout-based redraws which leads to much smoother drawing
  and less cpu footprint.
- ``pretty`` formatter (default) produces now valid escaped commands.
  It is still intented for visual output only. That's why a note for this was
  added.

### Added

- A fully working graphical user interface which is installed as a python module
  by default (can be disabled via compile option ie ``scons --without-gui``).
  It can be started via  ``rmlint --gui``.
- Support for automatic deduplication on btrfs using  ``BTRFS_IOC_FILE_EXTENT_SAME``.
  The Shellscript now will contain calls to  ``rmlint  --btrfs $source $dest``
  for duplicates on ``btrfs`` filesystems if  the user specified ``-c sh:clone``.
- Benchmark suite that will track the performance of rmlint from release to release.
  This helps developers detect any speed regressions or improvements and is a tool
  to help develop and validate optimization strategies.
- Shell/Python-script now does more sanity checks before removing and can be told to
  re-compare files byte-by-byte before removing them (``-p`` option when running
  the ``.sh`` file).
- Add a new ``--hash`` option so rmlint can be used as a very fast file hashing
  utility, eg ``rmlint --hash`` works like ``sha1sum``, or ``rmlint --hash -d md5``
  works like ``md5sum``.  Also does sha256, sha512, murmur{128}, spooky{32,64,128},
  city{128}.
- ``--sort-by`` learned new keys: ``l`` (path length) and ``d`` (path depth).
- New ``--unmatched-basename`` option only finds twins with differing basenames.
- Smaller performance and memory optimisations in shredder.

### Changed

- ``-g`` now checks if there is already a ``sh`` and ``json`` formatter before
  it adds one.
- ``-PP`` now defaults to ``xxhash`` as hashing algorithm.
- ``-o / --output`` learned to guess the formatter you want to use from the file ending.
  For example ``-o /tmp/test.json`` will work like ``-o json:/tmp/test.json``.
- JSON output contains ``rmlint`` version and revision now.
- ``--replay`` learned to merge several json files.
- Internal refactoring (credits go to Daniel) of the scheduler and hashing
  library. The duplicate finding process has be split in separate modules.

## [2.3.0 Ominous Oscar] -- 2015-06-15

### Fixed

- Compiles on Mac OSX now. See also: https://github.com/sahib/rmlint/issues/139
- Fix a crash that happened with ``-e``.
- Protect other lint than duplicates by ``-k`` or ``-K``.
- ``chown`` in sh script fixed (was ``chmod`` by accident).

### Added

- ``--replay``: Re-output a previously written json file. Allow filtering
  by using all other standard options (like size or directory filtering).
- ``--sort-by``: Similar to ``-S``, but sorts groups of files. So showing
  the group with the biggest size sucker is as easy as ``-y s``.

### Changed

- ``-S``'s long options is ``--rank-by`` now (prior ``--sortcriteria``).
- ``-o`` can guess the formatter from the filename if given.
- Remove some optimisations that gave no visible effect.
- Simplified FIEMAP optimisation to reduce initial delay and reduce memory overhead
- Improved hashing strategy for large disks (do repeated smaller sweeps across
  the disk instead of incrementally hashing every file on the disk)

## [2.2.1 Dreary Dropbear Bugfixes]

### Fixed

- Incorrect handling of -W, --no-with-color option
- Handling of $PKG_CONFIG in SConstruct
- Failure to build manpage
- Various BSD compatibility issues
- Nonstandard header sequence in modules using fts
- Removed some unnecessary warnings


## [2.2.0 Dreary Dropbear] -- 2015-05-09

### Fixed

- Issue with excessive memory usage and processing delays with
  very large file counts (>5M files)
- Problems and crashes on 32bit with large files and normal files.
- Bug in memory manager for "paranoid" file comparison method which
  could lead to OOM error in some cases and infinite looping in others.
- Fixed bug which prevented option --max-paranoid-mem working.
- Note: much kudos to our user "vvs-" who provided many useful testcases
  and was prepared to re-run a 10-hour duplicate search after each effort
  to fix the underlying issues.
- Handling of json formatter on invalid utf8, which fixed ``--cache`` in return.
- Bug during file traversal when encountering symlinks to empty folders

### Added

- More aggressive test suite, leading to higher coverage rates (90% of lines,
  almost 100% functions at least). Let's not speak of branch coverage for now. 😄
- A primitive benchmark suite.
- A GUI sketch that can be shipped along rmlint.

### Changed

- Most internal filesystems like `proc` are ignored now.
- Improved progressbar
- Memory footprint reduced to enable larger filesets to be processed. See
  discussion at https://github.com/sahib/rmlint/issues/109.  Improvements
  include a Pat(h)ricia-Trie used as data structure to efficiently map
  file paths with much less memory consumption.  Also the file preprocessing
  strategy (eg to find path doubles) has been improved to avoid having
  several large hashtables active at the same time.
- Improved threading strategy which increases speed of duplicate
  matching.  As before, the threading strategy uses just one thread per
  physical disk to enable fast reading without disk thrash.  The improved
  algorithm now increases the number of cpu threads used to hash the data
  as it is read in.  Also an improved mutex strategy reduces the wait time
  before the hash results can be processed.
  Note the new threading strategy is particularly effective on the
  "paranoid" (byte-by-byte) file comparison method (option -pp), which is
  now almost as fast as the default (SHA1 hash) method.
- The optimisation in 2.1.0 which detects existing reflinks has been
  reverted for now due to conflicts between shredder and treemerge.


## [2.1.0 Malnourished Molly] -- beta-release 2015-04-13

### Fixed

- performance regression: When having many pairs of duplicates,
  the core got slower very fast due to linear lookups. Fixed.
- performance regression: No SSDs were detected due to two bugs.
- commandline aborts also on non-fatal option misuses.
- Some statistic counts were updated wrong sometimes.
- Fixes in treemerge to respect directories tagges as originals.
- Ignore "evil" fs types like bindfs, nullfs completely.
- Fix race in file tree traversal.
- Various smaller bugfixes.

### Added

- ``--with-metadata-cache`` makes ``rmlint`` less memory hungry by storing
  its paths in a sqlite3 database and selecting them when needed.
- ``--without-fiemap`` disables the ``fiemap`` optimization when focus is on
  memory footprint.
- ``--perms`` can check if a file should be readable/writable or executable.
- Json output is enabled by default and is written to ``rmlint.json``.
- ``--partial-hidden`` does only see hidden files in duplicate directories.
- ``--cache/--write-unfinished`` can be used to speedup re-runs drastically.
- Checksums can be stored in the xattr of files with ``--xattr-read/write/clear``.
- New progressbar output inspired by ``journalctl --verfiy``.
- Better support for reflink-capable filesystems (e.g. *btrfs*):
  - detect existing reflinks using ``fiemap`` data (significant speedup)
  - support replacing files by a reflink if the filesystem supports it.

### Changed

- Optional dependency for *sqlite3* for ``--with-metadata-cache``.
- ``--hardlinked`` is enabled by default.
- Support -n (dry-run) for rmlint.sh; require user input on ask.
- Default digest is now *sha1* instead of *spooky*.
- updated ``.pot`` template with help strings.
- updated german translation accordingly.
- -T supports arguments like df,dd properly now.
- New --help text that shows a short reference only.
- sahib made his 1000th commit on rmlint with this text
  and wonders where all the time has gone and why he isn't rich yet.

## [2.0.0 Personable Pidgeon] -- 2014-01-23

Initial release of the rewrite.

[unreleased]: https://github.com/sahib/rmlint/compare/master...develop
[2.2.1 Dreary Dropbear Bugfixes]: https://github.com/sahib/rmlint/compare/v2.2.0..v2.2.1
[2.2.0 Dreary Dropbear]: https://github.com/sahib/rmlint/releases/tag/v2.2.0
[2.1.0 Malnourished Molly]: https://github.com/sahib/rmlint/releases/tag/v2.1.0
[2.0.0 Personable Pidgeon]: https://github.com/sahib/rmlint/releases/tag/v2.0.0
[2.3.0 Ominous Oscar]: https://github.com/sahib/rmlint/compare/v2.2.2..v2.4.0
[2.4.0 Myopic Micrathene]: https://github.com/sahib/rmlint/releases/tag/v2.4.0
[2.4.1 Myopic Micrathene Bugfixes]: https://github.com/sahib/rmlint/releases/tag/v2.4.1
[keepachangelog.com]: http://keepachangelog.com/
