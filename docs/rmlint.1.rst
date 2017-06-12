======
rmlint
======

------------------------------------------------------
find duplicate files and other space waste efficiently
------------------------------------------------------

.. Stuff in curly braces gets replaced by SCons

SYNOPSIS
========

rmlint [TARGET_DIR_OR_FILES ...] [//] [TAGGED_TARGET_DIR_OR_FILES ...] [-] [OPTIONS]

DESCRIPTION
===========

``rmlint`` finds space waste and other broken things on your filesystem.

Types of waste include:

* Duplicate files and directories.
* Nonstripped Binaries (Binaries with debug symbols).
* Broken links.
* Empty files and directories.
* Files with broken user or group id.

``rmlint`` will not delete any files. It does however produce executable output
(for example a shell script) to help you delete the files if you want to.

In order to find the lint, ``rmlint`` is given one or more directories to traverse.
If no directories or files were given, the current working directory is assumed.
By default, ``rmlint`` will ignore hidden files and will not follow symlinks (see
traversal options below).  ``rmlint`` will first find "other lint" and then search
the remaining files for duplicates.

Duplicate sets will be displayed as an original and one or more duplicates.  You
can set criteria for how ``rmlint`` chooses using the `-S` option (by default it
chooses the first-named path on the command line, or if that is equal then the
oldest file based on mtime).  You can also specify that certain paths **only** contain
originals by naming the path after the special path separator **//**.

Examples are given at the end of this manual.

OPTIONS
=======

General Options
---------------

:``-T --types="list"`` (**default\:** *defaults*):

    Configure the types of lint rmlint will look for. The `list` string is a
    comma-separated list of lint types or lint groups (other separators like
    semicolon or space also work).

    One of the following groups can be specified at the beginning of the list:

    * ``all``: Enables all lint types.
    * ``defaults``: Enables all lint types, but ``nonstripped``.
    * ``minimal``: ``defaults`` minus ``emptyfiles`` and ``emptydirs``.
    * ``minimaldirs``: ``defaults`` minus ``emptyfiles``, ``emptydirs`` and
      ``duplicates``, but with ``duplicatedirs``.
    * ``none``: Disable all lint types [default].

    Any of the following lint types can be added individually, or deselected by
    prefixing with a **-**:

    * ``badids``, ``bi``: Find bad UID, GID or files with both.
    * ``badlinks``, ``bl``: Find bad symlinks pointing nowhere.
    * ``emptydirs``, ``ed``: Find empty directories.
    * ``emptyfiles``, ``ef``: Find empty files.
    * ``nonstripped``, ``ns``: Find nonstripped binaries.
    * ``duplicates``, ``df``: Find duplicate files.
    * ``duplicatedirs``, ``dd``: Find duplicate directories.

    **WARNING:** It is good practice to enclose the description in quotes. In
    obscure cases argument parsing might fail in weird ways.

:``-o --output=spec`` / ``-O --add-output=spec`` (**default\:** *-o sh\:rmlint.sh -o pretty\:stdout -o summary\:stdout*):

    Configure the way ``rmlint`` outputs its results. A ``spec`` is in the
    form ``format:file`` or just ``format``.  A file might either be an arbitrary
    path or ``stdout`` or ``stderr``.  If file is omitted, ``stdout`` is assumed.

    If ``-o`` is specified, rmlint's defaults are overwritten.  With ``--O`` the
    defaults are preserved.  Either ``-o`` or ``-O`` may be specified multiple
    times to get multiple outputs, including multiple outputs of the same format.

    For a list of formatters and their options, refer to the **Formatters**
    section below.

:``-c --config=spec[=value]`` (**default\:** *none*):

    Configure a format. This option can be used to fine-tune the behaviour of
    the existing formatters. See the **Formatters** section for details on the
    available keys.

    If the value is omitted it is set to a true value.

:``-z --perms[=[rwx]]`` (**default\:** *no check*):

    Only look into file if it is readable, writable or executable by the current user.
    Which one of the can be given as argument as one of *"rwx"*.

    If no argument is given, *"rw"* is assumed. Note that *r* does basically
    nothing user-visible since ``rmlint`` will ignore unreadable files anyways.
    It's just there for the sake of completeness.

    By default this check is not done.

:``-a --algorithm=name`` (**default\:** *blake2b*):

    Choose the algorithm to use for finding duplicate files. The algorithm can be
    either **paranoid** (byte-by-byte file comparison) or use one of several file hash
    algorithms to identify duplicates.  The following well-known algorithms are available:

    **spooky**, **city**, **murmur**, **xxhash**, **md5**, **sha1**, **sha256**,
    **sha512**, **farmhash**, **sha3**, **sha3-256**, **sha3-384**, **sha3-512**,
    **blake2s**, **blake2b**, **blake2sp**, **blake2bp**.

    There are also some compound variations of the above functions:

    * **bastard:** 256bit, combining **city**, and **murmur**.
    * **city256, city512, murmur256, murmur512:** Use multiple 128-bit hashes with different seeds.
    * **spooky32, spooky64:** Faster version of **spooky** with less bits.

:``-p --paranoid`` / ``-P --less-paranoid`` (**default**):

    Increase or decrease the paranoia of ``rmlint``'s duplicate algorithm.
    Use ``-pp`` if you want byte-by-byte comparison without any hashing.

    * **-p** is equivalent to **--algorithm=sha512**
    * **-pp** is equivalent to **--algorithm=paranoid**

    * **-P** is equivalent to **--algorithm bastard**
    * **-PP** is equivalent to **--algorithm spooky**

:``-v --loud`` / ``-V --quiet``:

    Increase or decrease the verbosity. You can pass these options several
    times. This only affects ``rmlint``'s logging on *stderr*, but not the outputs
    defined with **-o**. Passing either option more than three times has no
    effect.

:``-g --progress`` / ``-G --no-progress`` (**default**):

    Convenience shortcut for ``-o progressbar -o summary -o sh:rmlint.sh -VVV``.

    Note: This flag clears all previous outputs. Specify any additional outputs
    after this flag!

:``-D --merge-directories`` (**default\:** *disabled*):

    Makes rmlint use a special mode where all found duplicates are collected and
    checked if whole directory trees are duplicates. Use with caution: You
    always should make sure that the investigated directory is not modified
    during ``rmlint``'s or its removal scripts run.

    IMPORTANT: Definition of equal: Two directories are considered equal by
    ``rmlint`` if they contain the exact same data, no matter how are the files
    contaning the data are named. Imagine that ``rmlint`` creates a long,
    sorted stream out of the data found in the directory and compares this in
    a magic way. This means that the layout of the directory is not considered
    to be important by ``rmlint``. This might be surprising to some users, but
    remember that ``rmlint`` generally cares only about content, not about any
    other metadata or layout.

    Output is deferred until all duplicates were found. Duplicate directories
    are printed first, followed by any remaining duplicate files.

    **--rank-by** applies for directories too, but 'p' or 'P' (path index)
    has no defined (i.e. useful) meaning. Sorting takes only place when the number of
    preferred files in the directory differs.

    **NOTES:**

    * This option enables ``--partial-hidden`` and ``-@`` (``--see-symlinks``)
      for convenience. If this is not desired, you should change this after
      specifying ``-D``.
    * This feature might not deliver perfect result in corner cases, but
      should never report false positives.
    * This feature might add some runtime for large datasets.
    * When using this option, you will not be able to use the ``-c sh:clone`` option.
      Use ``-c sh:link`` as a good alternative.

:``-j --honour-dir-layout`` (**default\:** *disabled*):

    Only recognize directories as duplicates that have the same path layout. In
    other words: All duplicates that build the duplicate directory must have
    the same path from the root of the directory.
    This flag has no effect without ``--merge-directories``.

:``-y --sort-by=order`` (**default\:** *none*):

    During output, sort the found duplicate groups by criteria described by `order`.
    `order` is a string that may consist of one or more of the following letters:

    * `s`: Sort by size of group.
    * `a`: Sort alphabetically by the basename of the original.
    * `m`: Sort by mtime of the original.
    * `p`: Sort by path-index of the original.
    * `o`: Sort by natural found order (might be different on each run).
    * `n`: Sort by number of files in the group.

    The letter may also be written uppercase (similar to ``-S /
    --rank-by``) to reverse the sorting. Note that ``rmlint`` has to hold
    back all results to the end of the run before sorting and printing.

:``--gui``:

    Start the optional graphical frontend to ``rmlint`` called ``Shredder``.

    This will only work when ``Shredder`` and its dependencies were installed.
    See also: http://rmlint.readthedocs.org/en/latest/gui.html

    The gui has its own set of options, see ``--gui --help`` for a list.  These
    should be placed at the end, ie ``rmlint --gui [options]`` when calling
    it from commandline.

:``--hash [paths...]``:

    Make ``rmlint`` work as a multi-threaded file hash utility, similar to the
    popular ``md5sum`` or ``sha1sum`` utilities, but faster and with more algorithms.
    A set of paths given on the commandline or from *stdin* is hashed using one
    of the available hash algorithms.  Use ``rmlint --hash -h`` to see options.

:``--equal [paths...]``:

    Check if the paths given on the commandline all have equal content. If all
    paths are equal and no other error happened, rmlint will exit with an exit
    code 0. Otherwise it will exit with a nonzero exit code. All other options
    can be used as normal, but note that no other formatters (``sh``, ``csv``
    etc.) will be executed by default. At least two paths need to be passed.

    Note: This even works for directories and also in combination with paranoid
    mode (pass ``-pp`` for byte comparison); remember that rmlint does not care
    about the layout of the directory, but only about the content of the files
    in it. At least two paths need to be given to the commandline.

    By default this will use hashing to compare the files and/or directories.

:``-w --with-color`` (**default**) / ``-W --no-with-color``:

    Use color escapes for pretty output or disable them.
    If you pipe `rmlints` output to a file ``-W`` is assumed automatically.

:``-h --help`` / ``-H --show-man``:

    Show a shorter reference help text (``-h``) or this full man page (``-H``).

:``--version``:

    Print the version of rmlint. Includes git revision and compile time
    features.

Traversal Options
-----------------

:``-s --size=range`` (**default\:** "1"):

    Only consider files as duplicates in a certain size range.
    The format of `range` is `min-max`, where both ends can be specified
    as a number with an optional multiplier. The available multipliers are:

    - *C* (1^1), *W* (2^1), B (512^1), *K* (1000^1), KB (1024^1), *M* (1000^2), *MB* (1024^2), *G* (1000^3), *GB* (1024^3),
    - *T* (1000^4), *TB* (1024^4), *P* (1000^5), *PB* (1024^5), *E* (1000^6), *EB* (1024^6)

    The size format is about the same as `dd(1)` uses. A valid example would be: **"100KB-2M"**.
    This limits duplicates to a range from 100 Kilobyte to 2 Megabyte.

    It's also possible to specify only one size. In this case the size is
    interpreted as *"bigger or equal"*. If you want to to filter for files
    *up to this size* you can add a ``-`` in front (``-s -1M`` == ``-s 0-1M``).

    **NOTE:** The default excludes empty files from the duplicate search.
    Normally these are treated specially by ``rmlint`` by handling them as
    *other lint*. If you want to include empty files as duplicates you should
    lower the limit to zero:

    ``$ rmlint -T df --size 0``

:``-d --max-depth=depth`` (**default\:** *INF*):

    Only recurse up to this depth. A depth of 1 would disable recursion and is
    equivalent to a directory listing.

:``-l --hardlinked`` (**default**) / ``-L --no-hardlinked``:

    Whether to report hardlinked files as duplicates.

:``-f --followlinks`` / ``-F --no-followlinks`` / ``-@ --see-symlinks`` (**default**):

    ``-f`` will always follow symbolic links. If file system loops occurs
    ``rmlint`` will detect this. If `-F` is specified, symbolic links will be
    ignored completely, if ``-@`` is specified, ``rmlint`` will see symlinks and
    treats them like small files with the path to their target in them. The
    latter is the default behaviour, since it is a sensible default for
    ``--merge-directories``.

:``-x --no-crossdev`` / ``-X --crossdev`` (**default**):

    Stay always on the same device (``-x``), or allow crossing mountpoints
    (``-X``). The latter is the default.

:``-r --hidden`` / ``-R --no-hidden`` (**default**) / ``--partial-hidden``:

    Also traverse hidden directories? This is often not a good idea, since
    directories like ``.git/`` would be investigated.
    With ``--partial-hidden`` hidden files and folders are only considered if
    they're inside duplicate directories (see --merge-directories).

:``-b --match-basename``:

    Only consider those files as dupes that have the same basename. See also
    ``man 1 basename``. The comparison of the basenames is case-insensitive.

:``-B --unmatched-basename``:

    Only consider those files as dupes that do not share the same basename.
    See also ``man 1 basename``. The comparison of the basenames is case-insensitive.

:``-e --match-with-extension`` / ``-E --no-match-with-extension`` (**default**):

    Only consider those files as dupes that have the same file extension. For
    example two photos would only match if they are a ``.png``. The extension is
    compared case-insensitive, so ``.PNG`` is the same as ``.png``.

:``-i --match-without-extension`` / ``-I --no-match-without-extension`` (**default**):

    Only consider those files as dupes that have the same basename minus the file
    extension. For example: ``banana.png`` and ``banana.jpeg`` would be considered,
    while ``apple.png`` and ``peach.png`` won't. The comparison is case-insensitive.

:``-n --newer-than-stamp=<timestamp_filename>`` / ``-N --newer-than=<iso8601_timestamp_or_unix_timestamp>``:

    Only consider files (and their size siblings for duplicates) newer than a
    certain modification time (*mtime*).  The age barrier may be given as
    seconds since the epoch or as ISO8601-Timestamp like
    *2014-09-08T00:12:32+0200*.

    ``-n`` expects a file from which it can read the timestamp. After
    rmlint run, the file will be updated with the current timestamp.
    If the file does not initially exist, no filtering is done but the stampfile
    is still written.

    ``-N``, in contrast, takes the timestamp directly and will not write anything.

    Note that ``rmlint`` will find duplicates newer than ``timestamp``, even if
    the original is older.  If you want only find duplicates where both
    original and duplicate are newer than ``timestamp`` you can use
    ``find(1)``:

    * ``find -mtime -1 | rmlint - # find all files younger than a day``

    *Note:* you can make rmlint write out a compatible timestamp with:

    * ``-O stamp:stdout  # Write a seconds-since-epoch timestamp to stdout on finish.``
    * ``-O stamp:stdout -c stamp:iso8601 # Same, but write as ISO8601.``

Original Detection Options
--------------------------

:``-k --keep-all-tagged`` / ``-K --keep-all-untagged``:

    Don't delete any duplicates that are in tagged paths (``-k``) or that are
    in non-tagged paths (``-K``).
    (Tagged paths are those that were named after **//**).

:``-m --must-match-tagged`` / ``-M --must-match-untagged``:

    Only look for duplicates of which at least one is in one of the tagged paths.
    (Paths that were named after **//**).

:``-S --rank-by=criteria`` (**default\:** *pOma*):

    Sort the files in a group of duplicates into originals and duplicates by
    one or more criteria. Each criteria is defined by a single letter (except
    **r** and **x** which expect a regex pattern after the letter). Multiple
    criteria may be given as string, where the first criteria is the most
    important. If one criteria cannot decide between original and duplicate the
    next one is tried.

    - **m**: keep lowest mtime (oldest)           **M**: keep highest mtime (newest)
    - **a**: keep first alphabetically            **A**: keep last alphabetically
    - **p**: keep first named path                **P**: keep last named path
    - **d**: keep path with lowest depth          **D**: keep path with highest depth
    - **l**: keep path with shortest basename     **L**: keep path with longest basename
    - **r**: keep paths matching regex            **R**: keep path not matching regex
    - **x**: keep basenames matching regex        **X**: keep basenames not matching regex
    - **h**: keep file with lowest hardlink count **H**: keep file with highest hardlink count
    - **o**: keep file with lowest number of hardlinks outside of the paths traversed by ``rmlint``.
    - **O**: keep file with highest number of hardlinks outside of the paths traversed by ``rmlint``.

    Alphabetical sort will only use the basename of the file and ignore its case.
    One can have multiple criteria, e.g.: ``-S am`` will choose first alphabetically; if tied then by mtime.
    **Note:** original path criteria (specified using `//`) will always take first priority over `-S` options.

    For more fine grained control, it is possible to give a regular expression
    to sort by. This can be useful when you know a common fact that identifies
    original paths (like a path component being ``src`` or a certain file ending).

    To use the regular expression you simply enclose it in the criteria string
    by adding `<REGULAR_EXPRESSION>` after specifying `r` or `x`. Example: ``-S
    'r<.*\.bak$>'`` makes all files that have a ``.bak`` suffix original files.

    Warning: When using **r** or **x**, try to make your regex to be as specific
    as possible! Good practice includes adding a ``$`` anchor at the end of the regex.

    Tips:

    - **l** is useful for files like `file.mp3 vs file.1.mp3 or file.mp3.bak`.
    - **a** can be used as last criteria to assert a defined order.
    - **o/O** and **h/H** are only useful if there any hardlinks in the traversed path.
    - **o/O** takes the number of hardlinks outside the traversed paths (and
      thereby minimizes/maximizes the overall number of hardlinks). **h/H** in
      contrast only takes the number of hardlinks *inside* of the traversed
      paths. When hardlinking files, one would like to link to the original
      file with the highest outer link count (**O**) in order to maximise the
      space cleanup. **H** does not maximise the space cleanup, it just selects
      the file with the highest total hardlink count. You usually want to specify **O**.
    - **pOma** is the default since **p** ensures that first given paths rank as originals,
      **O** ensures that hardlinks are handled well, **m** ensures that the oldest file is the
      original and **a** simply ensures a defined ordering if no other criteria applies.

Caching
-------

:``--replay``:

    Read an existing json file and re-output it. When ``--replay`` is given,
    ``rmlint`` does **no input/output on the filesystem**, even if you pass
    additional paths. The paths you pass will be used for filtering the
    ``--replay`` output.

    This is very useful if you want to reformat, refilter or resort the output
    you got from a previous run. Usage is simple: Just pass ``--replay`` on the
    second run, with other changed to the new formatters or filters. Pass the
    ``.json`` files of the previous runs additionally to the paths you ran
    ``rmlint`` on. You can also merge several previous runs by specifying more
    than one ``.json`` file, in this case it will merge all files given and
    output them as one big run.

    If you want to view only the duplicates of certain subdirectories, just
    pass them on the commandline as usual.

    The usage of ``//`` has the same effect as in a normal run. It can be used
    to prefer one ``.json`` file over another. However note that running
    ``rmlint`` in ``--replay`` mode includes no real disk traversal, i.e. only
    duplicates from previous runs are printed. Therefore specifying new paths
    will simply have no effect. As a security measure, ``--replay`` will ignore
    files whose mtime changed in the meantime (i.e. mtime in the ``.json`` file
    differes from the current one). These files might have been modified and
    are silently ignored.

    By design, some options will not have any effect. Those are:

    - `--followlinks`
    - `--algorithm`
    - `--paranoid`
    - `--clamp-low`
    - `--hardlinked`
    - `--write-unfinished`
    - ... and all other caching options below.

    *NOTE:* In ``--replay`` mode, a new ``.json`` file will be written to
    ``rmlint.replay.json`` in order to avoid overwriting ``rmlint.json``.

:``--xattr-read`` / ``--xattr-write`` / ``--xattr-clear``:

    Read or write cached checksums from the extended file attributes.
    This feature can be used to speed up consecutive runs.

    **CAUTION:** This could potentially lead to false positives if file contents are
    somehow modified without changing the file mtime.

    **NOTE:** Many tools do not support extended file attributes properly,
    resulting in a loss of the information when copying the file or editing it.
    Also, this is a linux specific feature that works not on all filesystems and
    only if you have write permissions to the file.

    Usage example: ::

        $ rmlint large_file_cluster/ -U --xattr-write   # first run.
        $ rmlint large_file_cluster/ --xattr-read       # second run.

:``-U --write-unfinished``:

    Include files in output that have not been hashed fully (i.e. files that do
    not appear to have a duplicate). This is mainly useful in conjunction with
    ``--xattr-write/read``. When re-running rmlint on a large dataset this can greatly
    speed up a re-run in some cases.

Rarely used, miscellaneous options
----------------------------------

:``-t --threads=N`` (*default\:* 16):

    The number of threads to use during file tree traversal and hashing.
    ``rmlint`` probably knows better than you how to set the value.

:``-u --max-paranoid-mem=size``:

    Apply a maximum number of bytes to use for **--paranoid**.
    The ``size``-description has the same format as for **--size**.

:``-q --clamp-low=[fac.tor|percent%|offset]`` (**default\:** *0*) / ``-Q --clamp-top=[fac.tor|percent%|offset]`` (**default\:** *1.0*):

    The argument can be either passed as factor (a number with a ``.`` in it),
    a percent value (suffixed by ``%``) or as absolute number or size spec, like in ``--size``.

    Only look at the content of files in the range of from ``low`` to
    (including) ``high``. This means, if the range is less than ``-q 0%`` to
    ``-Q 100%``, than only partial duplicates are searched. If the file size is
    less than the clamp limits, the file is ignored during traversing. Be careful when
    using this function, you can easily get dangerous results for small files.

    This is useful in a few cases where a file consists of a constant sized
    header or footer. With this option you can just compare the data in between.
    Also it might be useful for approximate comparison where it suffices when
    the file is the same in the middle part.

:``-Z --mtime-window=T`` (**default\:** *-1*):

    Only consider those files as duplicates that have the same content and
    the same modification time (mtime) within a certain window of *T* seconds.
    If *T* is 0, both files need to have the same mtime. For *T=1* they may
    differ one second and so on. If the window size is negative, the mtime of
    duplicates will not be considered. *T* may be a floating point number.

    However, with three (or more) files, the mtime difference between two
    duplicates can be bigger than the mtime window *T*, i.e. several files may
    be chained together by the window. Example: If *T* is 1, the four files
    fooA (mtime: 00:00:00), fooB (00:00:01), fooC (00:00:02), fooD (00:00:03)
    would all belong to the same duplicate group, although the mtime of fooA
    and fooD differs by 3 seconds.

:``--with-fiemap`` (**default**) / ``--without-fiemap``:

    Enable or disable reading the file extents on rotational disk in order to
    optimize disk access patterns.

FORMATTERS
==========

* ``csv``: Output all found lint as comma-separated-value list.

  Available options:

  * *no_header*: Do not write a first line describing the column headers.

* ``sh``: Output all found lint as shell script This formatter is activated
    as default.

  Available options:

  * *cmd*: Specify a user defined command to run on duplicates.
    The command can be any valid ``/bin/sh``-expression. The duplicate
    path and original path can be accessed via ``"$1"`` and ``"$2"``.
    The command will be written to the ``user_command`` function in the
    ``sh``-file produced by rmlint.

  * *handler* Define a comma separated list of handlers to try on duplicate
    files in that given order until one handler succeeds. Handlers are just the
    name of a way of getting rid of the file and can be any of the following:

    * ``clone``: ``btrfs`` only. Try to clone both files with the
      BTRFS_IOC_FILE_EXTENT_SAME ``ioctl(3p)``. This will physically delete
      duplicate extents. Needs at least kernel 4.2.
    * ``reflink``: Try to reflink the duplicate file to the original. See also
      ``--reflink`` in ``man 1 cp``. Fails if the filesystem does not support
      it.
    * ``hardlink``: Replace the duplicate file with a hardlink to the original
      file. The resulting files will have  the same inode number. Fails if both files are not on the same partition.
      You can use ``ls -i`` to show the inode number of a file and ``find -samefile <path>`` to find
      all hardlinks for a certain file.
    * ``symlink``: Tries to replace the duplicate file with a symbolic link to
      the original. Never fails.
    * ``remove``: Remove the file using ``rm -rf``. (``-r`` for duplicate dirs).
      Never fails.
    * ``usercmd``: Use the provided user defined command (``-c
      sh:cmd=something``). Never fails.

    Default is ``remove``.

  * *link*: Shortcut for ``-c sh:handler=clone,reflink,hardlink,symlink``.
  * *hardlink*: Shortcut for ``-c sh:handler=hardlink,symlink``.
  * *symlink*: Shortcut for ``-c sh:handler=symlink``.

* ``json``: Print a JSON-formatted dump of all found reports.
  Outputs all finds as a json document. The document is a list of dictionaries,
  where the first and last element is the header and the footer respectively,
  everything between are data-dictionaries.

  Available options:

  - *no_header=[true|false]:* Print the header with metadata.
  - *no_footer=[true|false]:* Print the footer with statistics.
  - *oneline=[true|false]:* Print one json document per line.

* ``py``: Outputs a python script and a JSON document, just like the **json** formatter.
  The JSON document is written to ``.rmlint.json``, executing the script will
  make it read from there. This formatter is mostly intented for complex use-cases
  where the lint needs special handling. Therefore the python script can be modified
  to do things standard ``rmlint`` is not able to do easily.

* ``stamp``:

  Outputs a timestamp of the time ``rmlint`` was run.

  Available options:

  - *iso8601=[true|false]:* Write an ISO8601 formatted timestamps or seconds
    since epoch?

* ``progressbar``: Shows a progressbar. This is meant for use with **stdout** or
  **stderr** [default].

  See also: ``-g`` (``--progress``) for a convenience shortcut option.

  Available options:

  * *update_interval=number:* Number of milliseconds to wait between updates.
    Higher values use less resources (default 50).
  * *ascii:* Do not attempt to use unicode characters, which might not be
    supported by some terminals.
  * *fancy:* Use a more fancy style for the progressbar.

* ``pretty``: Shows all found items in realtime nicely colored. This formatter
  is activated as default.

* ``summary``: Shows counts of files and their respective size after the run.
  Also list all written output files.

* ``fdupes``: Prints an output similar to the popular duplicate finder
  **fdupes(1)**. At first a progressbar is printed on **stderr.** Afterwards the
  found files are printed on **stdout;** each set of duplicates gets printed as a
  block separated by newlines. Originals are highlighted in green. At the bottom
  a summary is printed on **stderr**. This is mostly useful for scripts that were
  set up for parsing fdupes output. We recommend the ``json`` formatter for every other
  scripting purpose.

  Available options:

  * *omitfirst:* Same as the ``-f / --omitfirst`` option in ``fdupes(1)``. Omits the
    first line of each set of duplicates (i.e. the original file.
  * *sameline:* Same as the ``-1 / --sameline`` option in ``fdupes(1)``. Does not
    print newlines between files, only a space. Newlines are printed only between
    sets of duplicates.

EXAMPLES
========

This is a collection of common usecases and other tricks:

* Check the current working directory for duplicates.

  ``$ rmlint``

* Show a progressbar:

  ``$ rmlint -g``

* Quick re-run on large datasets using different ranking criteria on second run:

  ``$ rmlint large_dir/ # First run; writes rmlint.json``

  ``$ rmlint --replay rmlint.json large_dir -S MaD``

* Merge together previous runs, but prefer the originals to be from ``b.json`` and
  make sure that no files are deleted from ``b.json``:

  ``$ rmlint --replay a.json // b.json -k``

* Search only for duplicates and duplicate directories

  ``$ rmlint -T "df,dd" .``

* Compare files byte-by-byte in current directory:

  ``$ rmlint -pp .``

* Find duplicates with same basename (excluding extension):

  ``$ rmlint -e``

* Do more complex traversal using ``find(1)``.

  ``$ find /usr/lib -iname '*.so' -type f | rmlint - # find all duplicate .so files``

  ``$ find ~/pics -iname '*.png' | ./rmlint - # compare png files only``

* Limit file size range to investigate:

  ``$ rmlint -s 2GB    # Find everything >= 2GB``

  ``$ rmlint -s 0-2GB  # Find everything <  2GB``

* Only find writable and executable files:

  ``$ rmlint --perms wx``

* Reflink on btrfs, else try to hardlink duplicates to original. If that does
  not work, replace duplicate with a symbolic link:

  ``$ rmlint -c sh:link``

* Inject user-defined command into shell script output:

  ``$ rmlint -o sh -c sh:cmd='echo "original:" "$2" "is the same as" "$1"'``

* Use *data* as master directory. Find **only** duplicates in *backup* that are
  also in *data*. Do not delete any files in *data*:

  ``$ rmlint backup // data --keep-all-tagged --must-match-tagged``

* Compare if the directories a b c and are equal

  ``$ rmlint --equal a b c; echo $?  # Will print 0 if they are equal``

PROBLEMS
========

1. **False Positives:** Depending on the options you use, there is a very slight risk
   of false positives (files that are erroneously detected as duplicate).
   The default hash function (SHA1) is pretty safe but in theory it is possible for
   two files to have then same hash. This happens about once in 2 ** 80 files, so
   it is very very unlikely. If you're concerned just use the ``--paranoid`` (``-pp``)
   option. This will compare all the files byte-by-byte and is not much slower than SHA1.

2. **File modification during or after rmlint run:** It is possible that a file
   that ``rmlint`` recognized as duplicate is modified afterwards, resulting in a
   different file.  If you use the rmlint-generated shell script to delete the duplicates,
   you can run it with the ``-p`` option to do a full re-check of the duplicate against
   the original before it deletes the file. When using ``-c sh:hardlink`` or ``-c sh:symlink``
   care should be taken that a modification of one file will now result in a modification of
   all files. This is not the case for ``-c sh:reflink`` or ``-c sh:clone``. Use ``-c sh:link``
   to minimise this risk.

SEE ALSO
========

* `find(1)`
* `rm(1)`
* `cp(1)`

Extended documentation and an in-depth tutorial can be found at:

* http://rmlint.rtfd.org

BUGS
====

If you found a bug, have a feature requests or want to say something nice, please
visit https://github.com/sahib/rmlint/issues.

Please make sure to describe your problem in detail. Always include the version
of ``rmlint`` (``--version``). If you experienced a crash, please include
at least one of the following information with a debug build of ``rmlint``:

* ``gdb --ex run -ex bt --args rmlint -vvv [your_options]``
* ``valgrind --leak-check=no rmlint -vvv [your_options]``

You can build a debug build of ``rmlint`` like this:

* ``git clone git@github.com:sahib/rmlint.git``
* ``cd rmlint``
* ``scons DEBUG=1``
* ``sudo scons install  # Optional``

LICENSE
=======

``rmlint`` is licensed under the terms of the GPLv3.

See the COPYRIGHT file that came with the source for more information.

PROGRAM AUTHORS
===============

``rmlint`` was written by:

* Christopher <sahib> Pahl 2010-2017 (https://github.com/sahib)
* Daniel <SeeSpotRun> T.   2014-2017 (https://github.com/SeeSpotRun)

Also see the  http://rmlint.rtfd.org for other people that helped us.

If you consider a donation you can use *Flattr* or buy us a beer if we meet:

https://flattr.com/thing/302682/libglyr
