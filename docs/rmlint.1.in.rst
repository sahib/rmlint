======
rmlint
======

------------------------------------------------------
find duplicate files and other space waste efficiently
------------------------------------------------------

.. Stuff in curly braces gets replaced by SCons

:Author: sahib@online.de
:Date: {DATE}
:Copyright: public domain
:Version: {VERSION}
:Manual section: 1
:Manual group: file system

SYNOPSIS
========

rmlint [[//]TARGET_DIR ...] [FILE ...] [-] [OPTIONS]

DESCRIPTION
===========

`rmlint` finds space waste and other broken things on your filesystem and offers
to remove it. 

* Duplicate files.
* Nonstripped Binaries (Binaries with debug symbols).
* Broken links.
* Empty files and directories.
* Files with broken user or group id.
* Optionally: Double basenames.

In order to find the lint, `rmlint` is given one or more directories to traverse.
If no directory or file was given, the current working directory is assumed.
`rmlint` will take care of things like filesystem loops and symlinks during
traversing. 

Found duplicates are divided into the original and duplicates. Original
are what `rmlint` thinks to be the file that was first there. You can drive
the original detection with the `-S` option. If you know which path contains the
originals you can prefix the path with **//**, 

Quick clues for adjusting settings are available by using the `-q` option.

**Note:** `rmlint` will not delete any files. It only produces executable output
for you to remove it.

OPTIONS
=======

General Options
---------------

**-T --types="description"** (*default:* -T defaults)

    Configure the types of lint rmlint is supposed to find. The `description`
    string enumerates the types that shall be investigated, separted by a space.
    At the beginning of the string certain groups may be specified. 

    * ``all``: Enables all lint types.
    * ``defaults``: Enables all lint types, but ``namecluster`` and ``nonstripped``.
    * ``none``: Disable all lint types.

    All following lint types must be one of the following, optionally prefixed
    with a **+** or **-** to select or deselect it:

    * ``badids``, ``bi``: Find bad UID, GID or files with both.
    * ``badlinks``, ``bl``: Find bad symlinks pointing nowhere.
    * ``emptydirs``, ``ed``: Find empty directories.
    * ``emptyfiles``, ``ef``: Find empty files.
    * ``nameclusters``, ``nc``: Find files with same basename.
    * ``nonstripped``, ``ns``: Find nonstripped binaries. (**Warning:** slow)
    * ``duplicates``, ``df``: Find duplicate files.

**-o --output=formatter:file** (*default:* -o sh:rmlint.sh -o pretty:stdout -o summary:stdout)
**-O --add-output=formatter:file** 

    Configure the way rmlint ouputs it's results. You link a formatter to a
    file. A file might either be an arbitary path or ``stdout`` or ``stderr``.

    If this options is specified, rmlint's defaults are overwritten. 
    The option can be specified several times and formatters can be specified
    more than once for different files. 

    **--add-output** works the same way, but does not overwrite the defaults.
    Both **-o** and **-O** may not be specified at the same time.

    For a list of formatters and their options, look at the **Formatters**
    section below.

**-c --config=formatter:key[=value]** (*default:* none)

    Configure a formatter. This option can be used to finetune the behaviour of 
    the existing formatters. See the **Formatters** section for details on the
    available keys.

    If the value is omitted it is set to a truthy value.

**-a --algorithm=name** (*default:* spooky)

    Choose the hash algorithm to use for finding duplicate files.
    The following algorithms are available:
    **spooky**, **city**, **murmur**, **md5**. 

    If `rmlint` was compiled with `-D_RM_HASH_LEN=64` (not by default), then
    additionally the following algorithms are available:
    **sha1**, **sha256**, **sha512**.

**-v --loud / -V --quiet**

    Increase or decrease the verbosity. You can pass these options several
    times. This only affects rmlint's logging on *stderr*.

**-p --paranoid / -P --no-paranoid** (*default*)    

    Do a byte by byte comparison of each duplicate file. Use this when you do
    not trust hash functions. *Warning:* Slow.

**-w --with-color** (*default*) **/ -W --no-with-color**

    Use color escapes for pretty output or disable them. 
    If you pipe `rmlints` output to a file -W is assumed automatically.

**-q --confirm-settings / -Q --no-confirm-settings** (*default*)
    
    Print a screen of the used settings and the options that you need to change
    them. Requires confirmation before proceeding. This option has only effect
    when ``-o confirm:stdout`` is specified.

**-h --help / -H --version**

    Show this manual or print the version string.

Traversal Options
-----------------

**-t --threads=N** (*default:* 16)

    The number of threads to use during file tree traversal and hashing.
    `rmlint` probably knows better than you how to set the value.

**-s --size=range** (*default:* all)

    Only consider files in a certain size range.
    The format of `range` is `min-max`, where both ends can be specified
    as a number with an optional multiplier. The available multipliers are:

    - *C* (1^1), *W* (2^1), B (512^1), *K* (1000^1), KB (1024^1), *M* (1000^2), *MB* (1024^2), *G* (1000^3), *GB* (1024^3),
    - *T* (1000^4), *TB* (1024^4), *P* (1000^5), *PB* (1024^5), *E* (1000^6), *EB* (1024^6) 

    The size format is about the same as `dd(1)` uses. Example: **"100KB-2M"**.

    It's also possible to specify only one size. In this case the size is
    interpreted as "up to this size".

**-d --max-depth=depth** (*default:* INF) 

    Only recurse up to this depth. A depth of 1 would disable recursion and is
    equivalent to a directory listing.

**-l --hardlinked / -L --no-hardlinked** (*default*)

    By default `rmlint` will not allow several files with the same *inode* and
    therefore keep only one of them in it's internal list.
    If `-l` is specified the whole group is reported instead.

**-f --followlinks** (*default*) **/ -F --no-followlinks**

    Follow symbolic links? If file system loops occur `rmlint` will detect this.
    If `-F` is specified, symbolic links will be ignored completely.
    
    **Note:** Hardlinks are always followed, but it depends on ``-L`` how those are
    handled.

**-x --crossdev** (*default*) **/ -X --no-crossdev**
    
    Do cross over mount points (``-x``)? Or stay always on the same device
    (``-X``)?

**-r --hidden / -R --no-hidden** (*default*)

    Also traverse hidden directories? This is often not a good idea, since
    directories like `.git/` would be investigated.

Original Detection Options
--------------------------

**-k --keepall// / -K --no-keepall//** (*default*)

    Don't delete any duplicates that are in original paths.
    (Paths that were prefixed with **//**).
    
    **Note:** for lint types other than duplicates, `--keepallorig` option is ignored.

**-m --mustmatch// / -M --no-mustmatch//** (*default*)

    Only look for duplicates of which one is in original paths.
    (Paths that were prefixed with **//**).

**-i --invertorig / -I --no-invertorig** (*default*)

    Paths prefixed with **//** are non-originals and all other paths are originals.

**-S --sortcriteria=criteria** (*default*: m)

    - **m**: keep lowest mtime (oldest)  **M**: keep highest mtime (newest)
    - **a**: keep first alphabetically   **A**: keep last alphabetically
    - **p**: keep first named path       **P**: keep last named path

    One can have multiple criteria, e.g.: ``-S am`` will choose first alphabetically; if tied then by mtime.
    **Note:** original path criteria (specified using `//`) will always take first priority over `-S` options.
    
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

* ``progressbar``: Shows a progressbar. This is meant for use with **stdout** or
  **stderr**.

* ``pretty``: Shows all found items in realtimes nicely colored. This formatter
  is activated as default.

* ``summary``: Shows counts of files and their respective size after the run.
  Also list all written files.

* ``confirm``: Print a confirmation message before running. If ``-q`` is
  specified, wait till user entered his confirmation.

EXAMPLES
========

- ``rmlint``

  Check the current working directory for duplicates.

- ``find ~/pics -iname '*.png' | ./rmlint -``

  Read paths from *stdin* and check all png files for duplicates.

- ``rmlint //files files_backup --keepall// --mustmatch//``

  Check for duplicate files between the current files and the backup of it. 
  Only files in *files_backup* would be reported as duplicate. 
  Additionally, all reported duplicates must occur in both paths.

PROBLEMS
========

1. **False Positives:** Depending on the options you use, there is a very slight risk 
   of false positives (files that are erroneously detected as duplicate).
   Internally a hashfunctions is used to compute a *fingerprint* of a file. These
   hashfunctions may, in theory, map two different files to the same
   fingerprint. This happens about once in 2 ** 64 files. Since `rmlint` computes 
   at least 3 hashes per file and requires them to be the same size, it's very
   unlikely to happen. If you're really wary, try the *--paranoid* option.
2. **File modification during or after rmlint run:** It is possible that a file
   that rmlint recognized as duplicate is modified afterwards, resulting in a
   different file.  This is a general problem and cannot be solved from rmlint's
   side alone. You should **never modify the data until rmlint and the
   shellscript has been run through**. Careful persons might even consider to
   mount the filesystem you are scanning readonly.

SEE ALSO
========

* `find(1)`
* `rm(1)`

Extended documentation and an in-depth tutorial can be found at:


TODO: Actually write this tutorial.

BUGS
====

If you found a bug, have a feature requests or want to say something nice, please
visit https://github.com/sahib/rmlint/issues. 

Please make sure to describe your problem in detail. Always include the version
of `rmlint` (``--version``). If you experienced a crash, please include 
one of the following information with a debug build of `rmlint`:

    * ``gdb --ex run -ex bt --args rmlint -vvv [your_options]``
    * ``valgrind --leak-check=no rmlint -vvv [your_options]``

You can build a debug build of ``rmlint`` like this:

    * ``git clone git@github.com:sahib/rmlint.git``
    * ``cd rmlint``
    * ``scons DEBUG=1``
    * ``sudo scons install  # Optional`` 

LICENSE
=======

`rmlint` is licensed under the terms of the GPLv3.

See the COPYRIGHT file that came with the source for more information.

PROGRAM AUTHORS
===============

`rmlint` was written by:

* Christopher <sahib> Pahl 2010-2014 (https://github.com/sahib)
* Daniel <SeeSpotRun> T.   2014-2014 (https://github.com/SeeSpotRun)

Also see the THANKS file for other people that helped us.

If you consider a donation you can use *Flattr* or buy us a beer if we meet:

https://flattr.com/thing/302682/libglyr
