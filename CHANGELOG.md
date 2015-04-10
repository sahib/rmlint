# Change Log

All notable changes to this project will be documented in this file.

The format follows [keepachangelog.com]. Please stick to it.

## [2.2.0 Dreary Dropbear] [unreleased]

### Fixed

- Make rmlint usable for very hight amounts of files (works for 5M):
  See also: https://github.com/sahib/rmlint/issues/109
  A compression path trie is used as data structures for paths now (credits to Daniel)
- Problems and crashes on 32bit with large files and normal files.

### Added

- A primitive benchmark suite.
- A GUI sketch that can be shipped along rmlint.

### Changed

- Most internal filesystems like `proc` are ignored now.

## [2.1.0 Malnourished Molly] [beta-release]

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

## [2.0.0 Personable Pidgeon] - 2014-01-23

Initial release of the rewrite.

[unreleased]: https://github.com/sahib/rmlint/compare/master...develop
[2.1.0 Malnourished Molly]: https://github.com/sahib/rmlint/compare/master...develop
[2.0.0 Personable Pidgeon]: https://github.com/sahib/rmlint/releases/tag/v2.0.0
[keepachangelog.com]: http://keepachangelog.com/
