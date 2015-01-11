======
rmlint
======

------------------------------------------------------
find duplicate files and other space waste efficiently
------------------------------------------------------

.. Stuff in curly braces gets replaced by SCons

SYNOPSIS
========

rmlint [TARGET_DIR_OR_FILES ...] [//] [TARGET_DIR_OR_FILES ...] [-] [OPTIONS]

DESCRIPTION
===========

``rmlint`` finds space waste and other broken things on your filesystem and offers
to remove it. Types of waste include:

* Duplicate files.
* Nonstripped Binaries (Binaries with debug symbols).
* Broken links.
* Empty files and directories.
* Files with broken user or group id.

In order to find the lint, ``rmlint`` is given one or more directories to traverse.
If no directories or files were given, the current working directory is assumed.
``rmlint`` will take care of things like filesystem loops and symlinks during
traversing. 

Found duplicates are divided into the original and duplicates. Original
are what ``rmlint`` thinks to be the file that was first there. You can drive
the original detection with the `-S` option. If you know which path contains the
originals you can prefix the path with **//**, 

**Note:** ``rmlint`` will not delete any files. It only produces executable output
for you to remove it.

OPTIONS
=======

General Options
---------------

:``-T --types="description"`` (**default\:** *defaults*):

    Configure the types of lint rmlint is supposed to find. The `description`
    string enumerates the types that shall be investigated, separated by
    a space, comma or semicolon (actually more separators work). At the
    beginning of the string certain groups may be specified. 

    * ``all``: Enables all lint types.
    * ``defaults``: Enables all lint types, but ``nonstripped``.
    * ``minimal``: ``defaults`` minus ``emptyfiles`` and ``emptydirs``.
    * ``minimaldirs``: ``defaults`` minus ``emptyfiles``, ``emptydirs`` and
      ``duplicates``, but with ``duplicatedirs``.
    * ``none``: Disable all lint types.

    All following lint types must be one of the following, optionally prefixed
    with a **+** or **-** to select or deselect it:

    * ``badids``, ``bi``: Find bad UID, GID or files with both.
    * ``badlinks``, ``bl``: Find bad symlinks pointing nowhere.
    * ``emptydirs``, ``ed``: Find empty directories.
    * ``emptyfiles``, ``ef``: Find empty files.
    * ``nonstripped``, ``ns``: Find nonstripped binaries. (**Warning:** slow)
    * ``duplicates``, ``df``: Find duplicate files.
    * ``duplicatedirs``, ``dd``: Find duplicate directories. 

    **Warning:** It is good practice to enclose the description in quotes. In
    obscure cases argument parsing might fail in weird ways::

        # -ed is recognized as -e and -d here, -d takes "-s 10M" as parameter.
        # This will fail to do the supposed, finding also files smaller than 10M.
        $ rmlint -T all -ef -ed -s10M /media/music/  

:``-o --output=spec`` / ``-O --add-output=spec`` (**default\:** *-o sh\:rmlint.sh -o pretty\:stdout -o summary\:stdout*):

    Configure the way rmlint outputs it's results. You link a formatter to a
    file through ``spec``. A file might either be an arbitrary path or ``stdout`` or ``stderr``.
    If file is omitted, ``stdout`` is assumed.

    If this options is specified, rmlint's defaults are overwritten. 
    The option can be specified several times and formatters can be specified
    more than once for different files. 

    **--add-output** works the same way, but does not overwrite the defaults.
    Both **-o** and **-O** may not be specified at the same time.

    For a list of formatters and their options, look at the **Formatters**
    section below.

:``-c --config=spec[=value]`` (**default\:** *none*):

    Configure a formatter. This option can be used to fine-tune the behaviour of 
    the existing formatters. See the **Formatters** section for details on the
    available keys.

    If the value is omitted it is set to a truthy value.

:``-a --algorithm=name`` (**default\:** *spooky*) :

    Choose the hash algorithm to use for finding duplicate files.
    The following well-known algorithms are available:

    **spooky**, **city**, **murmur**, **md5**.  **sha1**, **sha256**,
    **sha512**.

    If not explicitly stated in the name the hashfunctions use 128 bit.
    There are variations of the above functions:

    * **bastard:** 256bit, half seeded **city**, half **murmur**. 
    * **city256, city512, murmur256, murmur512:** Slower variations with more bits.
    * **spooky32, spooky64:** Faster version of **spooky** with less bits.
    * **paranoid:** No hash function, compares files byte-by-byte.

:``-v --loud`` / ``-V --quiet``:
    
    Increase or decrease the verbosity. You can pass these options several
    times. This only affects rmlint's logging on *stderr*, but not the outputs
    defined with **-o**.

:``-g --progress`` / ``-G --no-progress`` (**default**):

    Convinience shortcut for ``-o progressbar -o summary -o sh:rmlint.sh``.
    It is recommended to run ``-g`` with ``-VVV`` to prevent the printing
    of warnings in between.

:``-p --paranoid`` / ``-P --less-paranoid`` (**default**):

    Increase the paranoia of rmlints internals. Both options can be specified up
    to three times. They do not do any work themselves, but set some other
    options implicitly as a shortcut. 

    * **-p** is equivalent to **--algorithm=bastard**
    * **-pp** is equivalent to **--algorithm=sha512**
    * **-ppp** is equivalent to **--algorithm=paranoid**

    The last one is not a hash function in the traditional meaning, but performs
    a byte-by-byte comparison of each file. See also **--max-paranoid-ram**.

    For the adventurous, it is also possible to decrease the default paranoia:

    * **-P** is equivalent to **--algorithm spooky64**
    * **-PP** is equivalent to **--algorithm spooky32**
    * **-PPP** is equivalent to **--algorithm debian_random**

    *This is really not recommended though.*

:``-D --merge-directories`` (**[experimental] default\:** *disabled*):

    Makes rmlint use a special mode where all found duplicates are collected and
    checked if whole directory trees are duplicates. This is an HIGHLY
    EXPERIMENTAL FEATURE and was/is tricky to implement right. Use with caution.
    You always should make sure that the investigated directory is not modified 
    during rmlint or it's removal scripts run. 

    Output is deferred until all duplicates were found.
    Sole duplicate groups are printed after the directories.

    **--sortcriteria** applies for directories too, but 'p' or 'P' (path index)
    has no defined (useful) meaning. Sorting takes only place when the number of
    preferred files in the directory differs. 

    *Notes:*

    * This option pulls in ``-r`` (``--hidden``) and ``-l`` (``--hardlinked``) for convenience.
    * This feature might not deliver perfect result in corner cases.
    * This feature might add some runtime.
    * Consider using ``-@`` together with this option (this is the default).

:``-q --clamp-low=[fac.tor|percent%|offset]`` (**default\:** *0*) / ``-Q --clamp-top=[fac.tor|percent%|offset]`` (**default\:** *1.0*):

    The argument can be either passed as factor (a number with a ``.`` in it),
    a percent value (suffixed by ``%``) or as absolute number or size spec, like in ``--size``.

    Only look at the content of files in the range of from ``low`` to
    (including) ``high``. This means, if the range is less than ``-q 0%`` to
    ``-Q 100%``, than only partial duplicates are searched. If the actual file
    size would be 0, the file is ignored during traversing. Be careful when
    using this function, you can easily get dangerous results for small files.

    This is useful in a few cases where a file consists of a constant sized
    header or footer. With this option you can just compare the data in between.
    Also it might be useful for approximate comparison where it suffices when
    the file is the same in the middle part.

    The shortcut ``-q / -Q`` can be easily remembered if you memorize the word
    ``quantile`` for it.

:``-u --max-paranoid-ram=size``:

    Apply a maximum number of bytes to use for **--paranoid**. 
    The ``size``-description has the same format as for **--size**.

:``-w --with-color`` (**default**) / ``-W --no-with-color``:

    Use color escapes for pretty output or disable them. 
    If you pipe `rmlints` output to a file -W is assumed automatically.

:``-h --help`` / ``-H --show-man``:

    Show a shorter reference help text (``-h``) or this full man page (``-H``).

:``--version``:

    Print the version of rmlint. Includes git revision and compile time
    features.

Traversal Options
-----------------

:``-t --threads=N**`` (*default\:* 16):

    The number of threads to use during file tree traversal and hashing.
    ``rmlint`` probably knows better than you how to set the value.

:``-s --size=range`` (**default\:** *all*):

    Only consider files in a certain size range.
    The format of `range` is `min-max`, where both ends can be specified
    as a number with an optional multiplier. The available multipliers are:

    - *C* (1^1), *W* (2^1), B (512^1), *K* (1000^1), KB (1024^1), *M* (1000^2), *MB* (1024^2), *G* (1000^3), *GB* (1024^3),
    - *T* (1000^4), *TB* (1024^4), *P* (1000^5), *PB* (1024^5), *E* (1000^6), *EB* (1024^6) 

    The size format is about the same as `dd(1)` uses. Example: **"100KB-2M"**.

    It's also possible to specify only one size. In this case the size is
    interpreted as "up to this size".

:``-d --max-depth=depth`` (**default\:** *INF*):

    Only recurse up to this depth. A depth of 1 would disable recursion and is
    equivalent to a directory listing.

:``-l --hardlinked`` / ``-L --no-hardlinked`` (**default**):

    By default ``rmlint`` will not allow several files with the same *inode* and
    therefore keep only one of them in it's internal list.
    If `-l` is specified the whole group is reported instead.

:``-f --followlinks`` / ``-F --no-followlinks`` / ``-@ --see-symlinks (**default**)``:

    Follow symbolic links? If file system loops occur ``rmlint`` will detect this.
    If `-F` is specified, symbolic links will be ignored completely, if the
    ``-F`` is specified once more ``rmlint`` will see symlinks an treats them
    like small files with the path to their target in them. The latter is the
    default behaviour, since it is a sensible default for ``--merge-directories``.

    **Note:** Hardlinks are always followed, but it depends on ``-L`` how those are
    handled. 

:``-x --crossdev`` (**default**) / ``-X --no-crossdev``:

    Do cross over mount points (``-x``)? Or stay always on the same device
    (``-X``)?

:``-r --hidden`` / ``-R --no-hidden`` (**default**):

    Also traverse hidden directories? This is often not a good idea, since
    directories like ``.git/`` would be investigated.

:``-b --match-basename`` / ``-B --no-match-basename`` (**default**):

    Only consider those files as dupes that have the same basename.
    See also ``man 1 basename``.

:``-e --match-with-extension`` / ``-E --no-match-with-extension`` (**default**):

    Only consider those files as dupes that have the same file extension.
    For example two photos would only match if they are a ``.png``.

:``-i --match-without-extension`` / ``-I --no-match-without-extension`` (**default**):

    Only consider those files as dupes that have the same basename minus the file
    extension. For example: ``banana.png`` and ``banana.jpeg`` would be considered,
    while ``apple.png`` and ``peach.png`` won't.

:``-n --newer-than-stamp=<timestamp_filename>`` / ``-N --newer-than=<iso8601_timestamp_or_unix_timestamp>``:

    Only consider files (and their size siblings for duplicates) newer than a
    certain modification time (*mtime*).  The age barrier may be given as
    seconds since the epoch or as ISO8601-Timestamp like
    *2014-09-08T00:12:32+0200*. 

    ``-n`` expects a file from where it can read the timestamp from. After
    rmlint run, the file will be updated with the current timestamp.
    If the file does not initially exist, no filtering is done but the stampfile
    is still written.

    ``-N`` in contrast takes the timestamp directly and will not write anything.

    If you want to take **only** the files (and not their size siblings) you can
    use ``find(1)``:

    * ``find -mtime -1 | rmlint - # find all files younger than a day``

    *Note:* you can make rmlint write out a compatible timestamp with:

    * ``-O stamp:stdout  # Write a seconds-since-epoch timestamp to stdout on finish.``
    * ``-O stamp:stdout -c stamp:iso8601 # Same, but write as ISO8601.``

Original Detection Options
--------------------------

:``-k --keep-all-tagged`` / ``-K --keep-all-untagged`` (**default**):

    Don't delete any duplicates that are in original paths.
    (Paths that were named after **//**).
    
    **Note:** for lint types other than duplicates, ``--keep-all-tagged`` option is ignored.

:``-m --must-match-tagged`` / ``-M --must-match-untagged`` (**default**):

    Only look for duplicates of which one is in original paths.
    (Paths that were named after **//**).

:``-S --sortcriteria=criteria`` (**default\:** *pm*):

    - **m**: keep lowest mtime (oldest)  **M**: keep highest mtime (newest)
    - **a**: keep first alphabetically   **A**: keep last alphabetically
    - **p**: keep first named path       **P**: keep last named path

    One can have multiple criteria, e.g.: ``-S am`` will choose first alphabetically; if tied then by mtime.
    **Note:** original path criteria (specified using `//`) will always take first priority over `-S` options.

Caching
-------

:``--xattr-read`` / ``--xattr-write`` / ``--xattr-clear``:

    Read or write cached checksums from the extended file attributes.
    This feature can be used to speed up consecutive runs.

    The same notes as in ``--cache`` apply.

    **NOTE:** Many tools do not support extended file attributes properly,
    resulting in a loss of the information when copying the file or editing it.
    Also, this is a linux specific feature that works not on all filesystems and 
    only if you write permissions to the file.

:``-C --cache file.json``:

    Read checksums from a *json* file. This *json* file is the same that is
    outputted via ``-o json``, but you can also enrich the *json* with 
    the checksums of sieved out files via ``--write-unfinished``.

    Usage example: ::

        $ rmlint large_cluster/ -O json:cache.json -U   # first run.
        $ rmlint large_cluster/ -C cache.json           # second run.

    **CAUTION:** This is a potentially unsafe feature. The cache file might be
    changed accidentally, potentially causing ``rmlint`` to report false
    positives. As a security feature the `mtime` of each cached file is checked 
    against the `mtime` of the time the checksum was created.

    **NOTE:** The speedup you may experience may vary wildly. In some cases the
    parsing of the json file might take longer than the actual hashing. Also,
    the cached json file will not be of use when doing many modifications
    between the runs, i.e. causing an update of `mtime` on most files. This
    feature is mostly intended for large datasets in order to prevent the
    re-hashing of large files. If you want to ensure this, you can use
    ``--size``.

:``-U --write-unfinished``: 

    Include files in output that have not been hashed fully (i.e. files that
    do not appear to have a duplicate). This is mainly useful in conjunction
    with ``--cache``. When re-running rmlint on a large dataset this can greatly
    speed up a re-run.

    This option also applies for ``--xattr-write``. 

    
FORMATTERS
==========

* ``csv``: Format all found lint as comma-separated-value list. 
  
  Available options:

  * *no_header*: Do not write a first line describing the column headers.

* ``sh``: Format all found lint as shellscript. Sane defaults for most
  lint-types are set. This formatter is activated as default.
  
  Available options:

  * *use_ln*: Instead of just deleting duplicates remove them and replace them
    with hardlinks (if they are on the same partition) or with symlinks if
    they're on different devices.
  * *symlinks_only*: Only relevant with *use_ln*, always use symbolic links,
    never use hardlinks.

* ``json``: Print a JSON-formatted dump of all found reports.
  Outputs all finds as a json document. The document is a list of dictionaries, 
  where the first and last element is the header and the footer respectively,
  everything between are data-dictionaries. 

  Available options:

  - *use_header=[true|false]:* Print the header with metadata.
  - *use_footer=[true|false]:* Print the footer with statistics.

* ``py``: Outputs a python script and a JSON document, just like the **json** formatter.
  The JSON document is written to ``.rmlint.json``, executing the script will
  make it read from there. This formatter is mostly intented for complex usecases
  where the lint needs special handling. Therefore the python script can be modified 
  to do things standard ``rmlint`` is not able to do easily.

* ``stamp``:

  Outputs a timestamp of the time ``rmlint`` was run.

  Available options:

  - *iso8601=[true|false]:* Write an ISO8601 formatted timestamps or seconds
    since epoch?

* ``progressbar``: Shows a progressbar. This is meant for use with **stdout** or
  **stderr**.
  
  See also: ``-g`` (``--progress``) for a convinience shortcut option.
 
  Available options:

  * *update_interval=number:* Number of files to wait between updates.
    Higher values use less resources. 
  * *use_unicode:* Print a nicer bar via the use of unicode characters.
    This may not be supported by your terminal/fonts.

* ``pretty``: Shows all found items in realtimes nicely colored. This formatter
  is activated as default.

* ``summary``: Shows counts of files and their respective size after the run.
  Also list all written files.

* ``fdupes``: Prints an output similar to the popular duplicate finder
  **fdupes(1)**. At first a progressbar is printed on **stderr.** Afterwards the
  found files are printed on **stdout;** each set of duplicates gets printed as a
  block separated by newlines. Originals are highlighted in green. At the bottom 
  a summary is printed on **stderr**. This is mostly useful for scripts that are used to
  parsing this format. We recommend the ``json`` formatter for every other
  scripting purpose.

  Available options:

  * *omitfirst:* Same as the ``-f / --omitfirst`` option in ``fdupes(1)``. Omits the
    first line of each set of duplicates (i.e. the original file.
  * *sameline:* Same as the ``-1 / --sameline`` option in ``fdupes(1)``. Does not
    print newlines between files, only a space. Newlines are printed only between
    sets of duplicates.

EXAMPLES
========

- ``rmlint``

  Check the current working directory for duplicates.

- ``find ~/pics -iname '*.png' | ./rmlint -``

  Read paths from *stdin* and check all png files for duplicates.

- ``rmlint files_backup // files --keep-all-tagged --must-match-tagged``

  Check for duplicate files between the current files and the backup of it. 
  Only files in *files_backup* would be reported as duplicate. 
  Additionally, all reported duplicates must occur in both paths.

PROBLEMS
========

1. **False Positives:** Depending on the options you use, there is a very slight risk 
   of false positives (files that are erroneously detected as duplicate).
   Internally a hashfunctions is used to compute a *fingerprint* of a file. These
   hashfunctions may, in theory, map two different files to the same
   fingerprint. This happens about once in 2 ** 64 files. Since ``rmlint`` computes 
   at least 3 hashes per file and requires them to be the same size, it's very
   unlikely to happen. If you're really wary, try the *--paranoid* option.
2. **File modification during or after rmlint run:** It is possible that a file
   that ``rmlint`` recognized as duplicate is modified afterwards, resulting in a
   different file.  This is a general problem and cannot be solved from ``rmlint's``
   side alone. You should **never modify the data until ``rmlint`` and the
   shellscript has been run through**. Careful persons might even consider to
   mount the filesystem you are scanning read-only.

SEE ALSO
========

* `find(1)`
* `rm(1)`

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

* Christopher <sahib> Pahl 2010-2014 (https://github.com/sahib)
* Daniel <SeeSpotRun> T.   2014-2014 (https://github.com/SeeSpotRun)

Also see the THANKS file for other people that helped us.

If you consider a donation you can use *Flattr* or buy us a beer if we meet:

https://flattr.com/thing/302682/libglyr
