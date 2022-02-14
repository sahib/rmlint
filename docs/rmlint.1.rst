======
rmlint
======

------------------------------------------------------
find duplicate files and other space waste efficiently
------------------------------------------------------

.. NOTE: Stuff in curly braces gets replaced by SCons
..       Use something like {{this}} to escape curly braces.

SYNOPSIS
========

rmlint [TARGET_DIR_OR_FILES ...] [//] [TAGGED_TARGET_DIR_OR_FILES ...] [-] [OPTIONS]

DESCRIPTION
===========

``rmlint`` finds space waste and other broken things on your filesystem.
Its main focus lies on finding duplicate files and directories.

It is able to find the following types of lint:

* Duplicate files and directories (and as a by-product unique files).
* Nonstripped Binaries (Binaries with debug symbols; needs to be explicitly enabled).
* Broken symbolic links.
* Empty files and directories (also nested empty directories).
* Files with broken user or group id.

``rmlint`` itself WILL NOT DELETE ANY FILES. It does however produce executable
output (for example a shell script) to help you delete the files if you want
to. Another design principle is that it should work well together with other
tools like ``find``. Therefore we do not replicate features of other well know
programs, as for example pattern matching and finding duplicate filenames.
However we provide many convenience options for common use cases that are hard
to build from scratch with standard tools.

In order to find the lint, ``rmlint`` is given one or more directories to traverse.
If no directories or files were given, the current working directory is assumed.
By default, ``rmlint`` will ignore hidden files and will not follow symlinks (see
`Traversal Options`_). ``rmlint`` will first find "other lint" and then search
the remaining files for duplicates.

``rmlint`` tries to be helpful by guessing what file of a group of duplicates
is the **original** (i.e. the file that should not be deleted). It does this by using
different sorting strategies that can be controlled via the ``-S`` option. By
default it chooses the first-named path on the commandline. If two duplicates
come from the same path, it will also apply different fallback sort strategies
(See the documentation of the ``-S`` strategy).

This behaviour can be also overwritten if you know that a certain directory
contains duplicates and another one originals. In this case you write the
original directory after specifying a single ``//`` on the commandline.
Everything that comes after is a preferred (or a "tagged") directory. If there
are duplicates from an unpreferred and from a preferred directory, the preferred
one will always count as original. Special options can also be used to always
keep files in preferred directories (``-k``) and to only find duplicates that
are present in both given directories (``-m``).

We advise new users to have a short look at all options ``rmlint`` has to
offer, and maybe test some examples before letting it run on productive data.
WRONG ASSUMPTIONS ARE THE BIGGEST ENEMY OF YOUR DATA. There are some extended
example at the end of this manual, but each option that is not self-explanatory
will also try to give examples.

OPTIONS
=======

General Options
---------------

:``-T --types="list"`` (**default\:** *defaults*):

    Configure the types of lint rmlint will look for. The `list` string is a
    comma-separated list of lint types or lint groups (other separators like
    semicolon or space also work though).

    One of the following groups can be specified at the beginning of the list:

    * ``all``: Enables all lint types.
    * ``defaults``: Enables all lint types, but ``nonstripped``.
    * ``minimal``: ``defaults`` minus ``emptyfiles`` and ``emptydirs``.
    * ``minimaldirs``: ``defaults`` minus ``emptyfiles``, ``emptydirs`` and
      ``duplicates``, but with ``duplicatedirs``.
    * ``none``: Disable all lint types [default].

    Any of the following lint types can be added individually, or deselected by
    prefixing with a **-**:

    * ``badids``, ``bi``: Find files with bad UID, GID or both.
    * ``badlinks``, ``bl``: Find bad symlinks pointing nowhere valid.
    * ``emptydirs``, ``ed``: Find empty directories.
    * ``emptyfiles``, ``ef``: Find empty files.
    * ``nonstripped``, ``ns``: Find nonstripped binaries.
    * ``duplicates``, ``df``: Find duplicate files.
    * ``duplicatedirs``, ``dd``: Find duplicate directories (This is the same ``-D``!)

    **WARNING:** It is good practice to enclose the description in single or
    double quotes. In obscure cases argument parsing might fail in weird ways,
    especially when using spaces as separator.

    Example::

    $ rmlint -T "df,dd"        # Only search for duplicate files and directories
    $ rmlint -T "all -df -dd"  # Search for all lint except duplicate files and dirs.

:``-o --output=spec`` / ``-O --add-output=spec`` (**default\:** *-o sh\:rmlint.sh -o pretty\:stdout -o summary\:stdout -o json\:rmlint.json*):

    Configure the way ``rmlint`` outputs its results. A ``spec`` is in the form
    ``format:file`` or just ``format``. A ``file`` might either be an
    arbitrary path or ``stdout`` or ``stderr``. If file is omitted, ``stdout``
    is assumed. ``format`` is the name of a formatter supported by this
    program. For a list of formatters and their options, refer to the
    **Formatters** section below.

    If ``-o`` is specified, rmlint's default outputs are overwritten. With
    ``-O`` the defaults are preserved. Either ``-o`` or ``-O`` may be
    specified multiple times to get multiple outputs, including multiple
    outputs of the same format.

    Examples::

    $ rmlint -o json                 # Stream the json output to stdout
    $ rmlint -O csv:/tmp/rmlint.csv  # Output an extra csv file to /tmp

:``-c --config=spec[=value]`` (**default\:** *none*):

    Configure a format. This option can be used to fine-tune the behaviour of
    the existing formatters. See the **Formatters** section for details on the
    available keys.

    If the value is omitted it is set to a value meaning "enabled".

    Examples::

    $ rmlint -c sh:link            # Smartly link duplicates instead of removing
    $ rmlint -c progressbar:fancy  # Use a different theme for the progressbar

:``-z --perms[=[rwx]]`` (**default\:** *no check*):

    Only look into file if it is readable, writable or executable by the current user.
    Which one of the can be given as argument as one of *"rwx"*.

    If no argument is given, *"rw"* is assumed. Note that *r* does basically
    nothing user-visible since ``rmlint`` will ignore unreadable files anyways.
    It's just there for the sake of completeness.

    By default this check is not done.

    ``$ rmlint -z rx $(echo $PATH | tr ":" " ")  # Look at all executable files in $PATH``

:``-a --algorithm=name`` (**default\:** *blake2b*):

    Choose the algorithm to use for finding duplicate files. The algorithm can be
    either **paranoid** (byte-by-byte file comparison) or use one of several file hash
    algorithms to identify duplicates. The following hash families are available (in
    approximate descending order of cryptographic strength):

    **sha3**, **blake**,

    **sha**,

    **highway**, **md**

    **metro**, **murmur**, **xxhash**

    The weaker hash functions still offer excellent distribution properties, but are potentially
    more vulnerable to *malicious* crafting of duplicate files.

    The full list of hash functions (in decreasing order of checksum length) is:

    512-bit: **blake2b**, **blake2bp**, **sha3-512**, **sha512**

    384-bit: **sha3-384**,

    256-bit: **blake2s**, **blake2sp**, **sha3-256**, **sha256**, **highway256**, **metro256**, **metrocrc256**

    160-bit: **sha1**

    128-bit: **md5**, **murmur**, **metro**, **metrocrc**

    64-bit: **highway64**, **xxhash**.

    The use of 64-bit hash length for detecting duplicate files is not recommended, due to the
    probability of a random hash collision.

:``-p --paranoid`` / ``-P --less-paranoid`` (**default**):

    Increase or decrease the paranoia of ``rmlint``'s duplicate algorithm.
    Use ``-p`` if you want byte-by-byte comparison without any hashing.

    * ``-p`` is equivalent to ``--algorithm=paranoid``

    * ``-P`` is equivalent to ``--algorithm=highway256``
    * ``-PP`` is equivalent to ``--algorithm=metro256``
    * ``-PPP`` is equivalent to ``--algorithm=metro``

:``-v --loud`` / ``-V --quiet``:

    Increase or decrease the verbosity. You can pass these options several
    times. This only affects ``rmlint``'s logging on *stderr*, but not the
    outputs defined with **-o**. Passing either option more than three times
    has no further effect.

:``-g --progress`` / ``-G --no-progress`` (**default**):

    Show a progressbar with sane defaults.

    Convenience shortcut for ``-o progressbar -o summary -o sh:rmlint.sh -o json:rmlint.json -VVV``.

    NOTE: This flag clears all previous outputs. If you want additional
    outputs, specify them after this flag using ``-O``.

:``-D --merge-directories`` (**default\:** *disabled*):

    Makes rmlint use a special mode where all found duplicates are collected and
    checked if whole directory trees are duplicates. Use with caution: You
    always should make sure that the investigated directory is not modified
    during ``rmlint``'s or its removal scripts run.

    IMPORTANT: Definition of equal: Two directories are considered equal by
    ``rmlint`` if they contain the exact same data, no matter how the files
    containing the data are named. Imagine that ``rmlint`` creates a long,
    sorted stream out of the data found in the directory and compares this in
    a magic way to another directory. This means that the layout of the
    directory is not considered to be important by default. Also empty files
    will not count as content. This might be surprising to some users, but
    remember that ``rmlint`` generally cares only about content, not about any
    other metadata or layout. If you want to only find trees with the same hierarchy
    you should use ``--honour-dir-layout / -j``.

    Output is deferred until all duplicates were found. Duplicate directories
    are printed first, followed by any remaining duplicate files that are isolated
    or inside of any original directories.

    **--rank-by** applies for directories too, but 'p' or 'P' (path index)
    has no defined (i.e. useful) meaning. Sorting takes only place when the number of
    preferred files in the directory differs.

    **NOTES:**

    * This option enables ``--partial-hidden`` and ``-@`` (``--see-symlinks``)
      for convenience. If this is not desired, you should change this after
      specifying ``-D``.
    * This feature might add some runtime for large datasets.
    * When using this option, you will not be able to use the ``-c sh:clone`` option.
      Use ``-c sh:link`` as a good alternative.

:``-j --honour-dir-layout`` (**default\:** *disabled*):

    Only recognize directories as duplicates that have the same path layout. In
    other words: All duplicates that build the duplicate directory must have
    the same path from the root of each respective directory.
    This flag makes no sense without ``--merge-directories``.

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

:``-w --with-color`` (**default**) / ``-W --no-with-color``:

    Use color escapes for pretty output or disable them.
    If you pipe `rmlints` output to a file ``-W`` is assumed automatically.

:``-h --help`` / ``-H --show-man``:

    Show a shorter reference help text (``-h``) or the full man page (``-H``).

:``--version``:

    Print the version of rmlint. Includes git revision and compile time
    features. Please include this when giving feedback to us.

Traversal Options
-----------------

:``-s --size=range`` (**default\:** "1"):

    Only consider files as duplicates in a certain size range.
    The format of `range` is `min-max`, where both ends can be specified
    as a number with an optional multiplier. The available multipliers are:

    - *C* (1^1), *W* (2^1), B (512^1), *K* (1000^1), KB (1024^1), *M* (1000^2), *MB* (1024^2), *G* (1000^3), *GB* (1024^3),
    - *T* (1000^4), *TB* (1024^4), *P* (1000^5), *PB* (1024^5), *E* (1000^6), *EB* (1024^6)

    The size format is about the same as `dd(1)` uses. A valid example would
    be: **"100KB-2M"**. This limits duplicates to a range from 100 Kilobyte to
    2 Megabyte.

    It's also possible to specify only one size. In this case the size is
    interpreted as *"bigger or equal"*. If you want to filter for files
    *up to this size* you can add a ``-`` in front (``-s -1M`` == ``-s 0-1M``).

    **Edge case:** The default excludes empty files from the duplicate search.
    Normally these are treated specially by ``rmlint`` by handling them as
    *other lint*. If you want to include empty files as duplicates you should
    lower the limit to zero:

    ``$ rmlint -T df --size 0``

:``-d --max-depth=depth`` (**default\:** *INF*):

    Only recurse up to this depth. A depth of 1 would disable recursion and is
    equivalent to a directory listing. A depth of 2 would also consider all
    children directories and so on.

:``-l --hardlinked`` (**default**) / ``--keep-hardlinked`` / ``-L --no-hardlinked``:

    Hardlinked files are treated as duplicates by default (``--hardlinked``). If
    ``--keep-hardlinked`` is given, `rmlint` will not delete any files that are
    hardlinked to an original in their respective group. Such files will be
    displayed like originals, i.e. for the default output with a "ls" in front.
    The reasoning here is to maximize the number of kept files, while maximizing
    the number of freed space: Removing hardlinks to originals will not allocate
    any free space.

    If `--no-hardlinked` is given, only one file (of a set of hardlinked files)
    is considered, all the others are ignored; this means, they are not
    deleted and also not even shown in the output. The "highest ranked" of the
    set is the one that is considered.

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
    directories like ``.git/`` would be investigated, possibly leading to the
    deletion of internal ``git`` files which in turn break a repository.
    With ``--partial-hidden`` hidden files and folders are only considered if
    they're inside duplicate directories (see ``--merge-directories``) and will
    be deleted as part of it.

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
    extension. For example: ``banana.png`` and ``Banana.jpeg`` would be considered,
    while ``apple.png`` and ``peach.png`` won't. The comparison is case-insensitive.

:``-n --newer-than-stamp=<timestamp_filename>`` / ``-N --newer-than=<iso8601_timestamp_or_unix_timestamp>``:

    Only consider files (and their size siblings for duplicates) newer than a
    certain modification time (*mtime*). The age barrier may be given as
    seconds since the epoch or as ISO8601-Timestamp like
    *2014-09-08T00:12:32+0200*.

    ``-n`` expects a file from which it can read the timestamp. After
    rmlint run, the file will be updated with the current timestamp.
    If the file does not initially exist, no filtering is done but the stampfile
    is still written.

    ``-N``, in contrast, takes the timestamp directly and will not write anything.

    Note that ``rmlint`` will find duplicates newer than ``timestamp``, even if
    the original is older. If you want only find duplicates where both
    original and duplicate are newer than ``timestamp`` you can use
    ``find(1)``:

    * ``find -mtime -1 -print0 | rmlint -0 # pass all files younger than a day to rmlint``

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

    Note that the combinations of ``-kM`` and ``-Km`` are prohibited by ``rmlint``.
    See https://github.com/sahib/rmlint/issues/244 for more information.

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
    differs from the current one). These files might have been modified and
    are silently ignored.

    By design, some options will not have any effect. Those are:

    - ``--followlinks``
    - ``--algorithm``
    - ``--paranoid``
    - ``--clamp-low``
    - ``--hardlinked``
    - ``--write-unfinished``
    - ... and all other caching options below.

    *NOTE:* In ``--replay`` mode, a new ``.json`` file will be written to
    ``rmlint.replay.json`` in order to avoid overwriting ``rmlint.json``.

:``-C --xattr``:

    Shortcut for ``--xattr-read``, ``--xattr-write``, ``--write-unfinished``.
    This will write a checksum and a timestamp to the extended attributes of each
    file that rmlint hashed. This speeds up subsequent runs on the same data set.
    Please note that not all filesystems may support extended attributes and you
    need write support to use this feature.

    See the individual options below for more details and some examples.

:``--xattr-read`` / ``--xattr-write`` / ``--xattr-clear``:

    Read or write cached checksums from the extended file attributes.
    This feature can be used to speed up consecutive runs.

    **CAUTION:** This could potentially lead to false positives if file
    contents are somehow modified without changing the file modification time.
    rmlint uses the mtime to determine the modification timestamp if a checksum
    is outdated. This is not a problem if you use the clone or reflink
    operation on a filesystem like btrfs. There an outdated checksum entry
    would simply lead to some duplicate work done in the kernel but would do no
    harm otherwise.

    **NOTE:** Many tools do not support extended file attributes properly,
    resulting in a loss of the information when copying the file or editing it.

    **NOTE:** You can specify ``--xattr-write`` and ``--xattr-read`` at the same time.
    This will read from existing checksums at the start of the run and update all hashed
    files at the end.

    Usage example::

        $ rmlint large_file_cluster/ -U --xattr-write   # first run should be slow.
        $ rmlint large_file_cluster/ --xattr-read       # second run should be faster.

        # Or do the same in just one run:
        $ rmlint large_file_cluster/ --xattr

:``-U --write-unfinished``:

    Include files in output that have not been hashed fully, i.e. files that do
    not appear to have a duplicate. Note that this will not include all files
    that ``rmlint`` traversed, but only the files that were chosen to be hashed.

    This is mainly useful in conjunction with ``--xattr-write/read``. When
    re-running rmlint on a large dataset this can greatly speed up a re-run in
    some cases. Please refer to ``--xattr-read`` for an example.

    If you want to output unique files, please look into the ``uniques`` output formatter.

Rarely used, miscellaneous options
----------------------------------

:``-t --threads=N`` (*default\:* 16):

    The number of threads to use during file tree traversal and hashing.
    ``rmlint`` probably knows better than you how to set this value, so just
    leave it as it is. Setting it to ``1`` will also not make ``rmlint``
    a single threaded program.

:``-u --limit-mem=size``:

    Apply a maximum number of memory to use for hashing and **--paranoid**.
    The total number of memory might still exceed this limit though, especially
    when setting it very low. In general ``rmlint`` will however consume about this
    amount of memory plus a more or less constant extra amount that depends on the
    data you are scanning.

    The ``size``-description has the same format as for **--size**, therefore you
    can do something like this (use this if you have 1GB of memory available):

    ``$ rmlint -u 512M  # Limit paranoid mem usage to 512 MB``

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

    Example:

    ``$ rmlint -q 10% -Q 512M  # Only read the last 90% of a file, but read at max. 512MB``

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
    optimize disk access patterns. If this feature is not available, it is
    disabled automatically.

FORMATTERS
==========

* ``csv``: Output all found lint as comma-separated-value list.

  Available options:

  * *no_header*: Do not write a first line describing the column headers.
  * *unique*: Include unique files in the output.

* ``sh``: Output all found lint as shell script This formatter is activated
    as default.

  available options:

  * *cmd*: Specify a user defined command to run on duplicates.
    The command can be any valid ``/bin/sh``-expression. The duplicate
    path and original path can be accessed via ``"$1"`` and ``"$2"``.
    The command will be written to the ``user_command`` function in the
    ``sh``-file produced by rmlint.

  * *handler* Define a comma separated list of handlers to try on duplicate
    files in that given order until one handler succeeds. Handlers are just the
    name of a way of getting rid of the file and can be any of the following:

    * ``clone``: For reflink-capable filesystems only. Try to clone both files with the
      FIDEDUPERANGE ``ioctl(3p)`` (or BTRFS_IOC_FILE_EXTENT_SAME on older kernels).
      This will free up duplicate extents. Needs at least kernel 4.2.
      Use this option when you only have read-only access to a btrfs filesystem but still
      want to deduplicate it. This is usually the case for snapshots.
    * ``reflink``: Try to reflink the duplicate file to the original. See also
      ``--reflink`` in ``man 1 cp``. Fails if the filesystem does not support
      it.
    * ``hardlink``: Replace the duplicate file with a hardlink to the original
      file. The resulting files will have the same inode number. Fails if both
      files are not on the same partition. You can use ``ls -i`` to show the
      inode number of a file and ``find -samefile <path>`` to find all
      hardlinks for a certain file.
    * ``symlink``: Tries to replace the duplicate file with a symbolic link to
      the original. This handler never fails.
    * ``remove``: Remove the file using ``rm -rf``. (``-r`` for duplicate dirs).
      This handler never fails.
    * ``usercmd``: Use the provided user defined command (``-c
      sh:cmd=something``). This handler never fails.

    Default is ``remove``.

  * *link*: Shortcut for ``-c sh:handler=clone,reflink,hardlink,symlink``.
    Use this if you are on a reflink-capable system.
  * *hardlink*: Shortcut for ``-c sh:handler=hardlink,symlink``.
    Use this if you want to hardlink files, but want to fallback
    for duplicates that lie on different devices.
  * *symlink*: Shortcut for ``-c sh:handler=symlink``.
    Use this as last straw.

* ``json``: Print a JSON-formatted dump of all found reports. Outputs all lint
  as a json document. The document is a list of dictionaries, where the first
  and last element is the header and the footer. Everything between are
  data-dictionaries.

  Available options:

  - *unique*: Include unique files in the output.
  - *no_header=[true|false]:* Print the header with metadata (default: true)
  - *no_footer=[true|false]:* Print the footer with statistics (default: true)
  - *oneline=[true|false]:* Print one json document per line (default: false)
    This is useful if you plan to parse the output line-by-line, e.g. while
    ``rmlint`` is sill running.

  This formatter is extremely useful if you're in need of scripting more complex behaviour,
  that is not directly possible with rmlint's built-in options. A very handy tool here is ``jq``.
  Here is an example to output all original files directly from a ``rmlint`` run:

  ``$ rmlint -o | json jq -r '.[1:-1][] | select(.is_original) | .path'``

* ``py``: Outputs a python script and a JSON document, just like the **json** formatter.
  The JSON document is written to ``.rmlint.json``, executing the script will
  make it read from there. This formatter is mostly intended for complex use-cases
  where the lint needs special handling that you define in the python script.
  Therefore the python script can be modified to do things standard ``rmlint``
  is not able to do easily.

* ``uniques``: Outputs all unique paths found during the run, one path per line.
  This is often useful for scripting purposes.

  Available options:

  - *print0*: Do not put newlines between paths but zero bytes.

* ``stamp``:

  Outputs a timestamp of the time ``rmlint`` was run.
  See also the ``--newer-than`` and ``--newer-than-stamp`` file option.

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

OTHER STAND-ALONE COMMANDS
==========================

:``rmlint --gui``:

    Start the optional graphical frontend to ``rmlint`` called ``Shredder``.

    This will only work when ``Shredder`` and its dependencies were installed.
    See also: http://rmlint.readthedocs.org/en/latest/gui.html

    The gui has its own set of options, see ``--gui --help`` for a list. These
    should be placed at the end, ie ``rmlint --gui [options]`` when calling
    it from commandline.

:``rmlint --hash [paths...]``:

    Make ``rmlint`` work as a multi-threaded file hash utility, similar to the
    popular ``md5sum`` or ``sha1sum`` utilities, but faster and with more algorithms.
    A set of paths given on the commandline or from *stdin* is hashed using one
    of the available hash algorithms. Use ``rmlint --hash -h`` to see options.

:``rmlint --equal [paths...]``:

    Check if the paths given on the commandline all have equal content. If all
    paths are equal and no other error happened, rmlint will exit with an exit
    code 0. Otherwise it will exit with a nonzero exit code. All other options
    can be used as normal, but note that no other formatters (``sh``, ``csv``
    etc.) will be executed by default. At least two paths need to be passed.

    Note: This even works for directories and also in combination with paranoid
    mode (pass ``-p`` for byte comparison); remember that rmlint does not care
    about the layout of the directory, but only about the content of the files
    in it. At least two paths need to be given to the commandline.

    By default this will use hashing to compare the files and/or directories.

:``rmlint --dedupe [-r] [-v|-V] <src> <dest>``:

    If the filesystem supports files sharing physical storage between multiple
    files, and if ``src`` and ``dest`` have same content, this command makes the
    data in the ``src`` file appear the ``dest`` file by sharing the
    underlying storage.

    This command is similar to ``cp --reflink=always <src> <dest>``
    except that it (a) checks that ``src`` and ``dest`` have identical data, and
    it makes no changes to ``dest``'s metadata.

    Running with ``-r`` option will enable deduplication of read-only [btrfs]
    snapshots (requires root).

:``rmlint --is-reflink [-v|-V] <file1> <file2>``:
    Tests whether ``file1`` and ``file2`` are reflinks (reference same data).
    This command makes ``rmlint`` exit with one of the following exit codes:

    * 0: files are reflinks
    * 1: files are not reflinks
    * 3: not a regular file
    * 4: file sizes differ
    * 5: fiemaps can't be read
    * 6: file1 and file2 are the same path
    * 7: file1 and file2 are the same file under different mountpoints
    * 8: files are hardlinks
    * 9: files are symlinks
    * 10: files are not on same device
    * 11: other error encountered


EXAMPLES
========

This is a collection of common use cases and other tricks:

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

  ``$ rmlint -p .``

* Find duplicates with same basename (excluding extension):

  ``$ rmlint -e``

* Do more complex traversal using ``find(1)``.

  ``$ find /usr/lib -iname '*.so' -type f | rmlint - # find all duplicate .so files``

  ``$ find /usr/lib -iname '*.so' -type f -print0 | rmlint -0 # as above but handles filenames with newline character in them``

  ``$ find ~/pics -iname '*.png' | ./rmlint - # compare png files only``

* Limit file size range to investigate:

  ``$ rmlint -s 2GB    # Find everything >= 2GB``

  ``$ rmlint -s 0-2GB  # Find everything <  2GB``

* Only find writable and executable files:

  ``$ rmlint --perms wx``

* Reflink if possible, else hardlink duplicates to original if possible, else replace
  duplicate with a symbolic link:

  ``$ rmlint -c sh:link``

* Inject user-defined command into shell script output:

  ``$ rmlint -o sh -c sh:cmd='echo "original:" "$2" "is the same as" "$1"'``

* Use ``shred`` to overwrite the contents of a file fully:

  ``$ rmlint -c 'sh:cmd=shred -un 10 "$1"'``

* Use *data* as master directory. Find **only** duplicates in *backup* that are
  also in *data*. Do not delete any files in *data*:

  ``$ rmlint backup // data --keep-all-tagged --must-match-tagged``

* Compare if the directories a b c and are equal

  ``$ rmlint --equal a b c && echo "Files are equal" || echo "Files are not equal"``

* Test if two files are reflinks

  ``$ rmlint --is-reflink a b && echo "Files are reflinks" || echo "Files are not reflinks"``.

* Cache calculated checksums for next run. The checksums will be written to the extended file attributes:

  ``$ rmlint --xattr``

* Produce a list of unique files in a folder:

  ``$ rmlint -o uniques``

* Produce a list of files that are unique, including original files ("one of each"):

  ``$ rmlint t -o json -o uniques:unique_files | jq -r '.[1:-1][] | select(.is_original) | .path' | sort > original_files``
  ``$ cat unique_files original_files``

* Sort files by a user-defined regular expression

    .. code-block:: bash

      # Always keep files with ABC or DEF in their basename,
      # dismiss all duplicates with tmp, temp or cache in their names
      # and if none of those are applicable, keep the oldest files instead.
      $ ./rmlint -S 'x<.*(ABC|DEF).*>X<.*(tmp|temp|cache).*>m' /some/path

* Sort files by adding priorities to several user-defined regular expressions:

    .. code-block:: bash

      # Unlike the previous snippet, this one uses priorities:
      # Always keep files in ABC, DEF, GHI by following that particular order of
      # importance (ABC has a top priority), dismiss all duplicates with 
      # tmp, temp, cache in their paths and if none of those are applicable, 
      # keep the oldest files instead.
      $ rmlint -S 'r<.*ABC.*>r<.*DEF.*>r<.*GHI.*>R<.*(tmp|temp|cache).*>m' /some/path

PROBLEMS
========

1. **False Positives:** Depending on the options you use, there is a very slight risk
   of false positives (files that are erroneously detected as duplicate).
   The default hash function (blake2b) is very safe but in theory it is possible for
   two files to have then same hash. If you had 10^73 different files, all the same
   size, then the chance of a false positive is still less than 1 in a billion.
   If you're concerned just use the ``--paranoid`` (``-p``)
   option. This will compare all the files byte-by-byte and is not much slower than
   blake2b (it may even be faster), although it is a lot more memory-hungry.

2. **File modification during or after rmlint run:** It is possible that a file
   that ``rmlint`` recognized as duplicate is modified afterwards, resulting in
   a different file. If you use the rmlint-generated shell script to delete
   the duplicates, you can run it with the ``-p`` option to do a full re-check
   of the duplicate against the original before it deletes the file. When using
   ``-c sh:hardlink`` or ``-c sh:symlink`` care should be taken that
   a modification of one file will now result in a modification of all files.
   This is not the case for ``-c sh:reflink`` or ``-c sh:clone``. Use ``-c
   sh:link`` to minimise this risk.

SEE ALSO
========

Reading the manpages of these tools might help working with ``rmlint``:

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
* ``scons GDB=1 DEBUG=1``
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

Also see the http://rmlint.rtfd.org for other people that helped us.
