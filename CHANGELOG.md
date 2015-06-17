# Change Log

All notable changes to this project will be documented in this file.

The format follows [keepachangelog.com]. Please stick to it.

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

## [2.2.1 Dreary Dropbear Bugfixes] -- [unreleased]

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
  it's paths in a sqlite3 database and selecting them when needed.
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
[2.2.0 Dreary Dropbear]: https://github.com/sahib/rmlint/compare/master...develop
[2.1.0 Malnourished Molly]: https://github.com/sahib/rmlint/releases/tag/v2.1.0
[2.0.0 Personable Pidgeon]: https://github.com/sahib/rmlint/releases/tag/v2.0.0
[keepachangelog.com]: http://keepachangelog.com/
