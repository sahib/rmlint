Tutorial
========

Welcome to the Tutorial of ``rmlint``.

We use a few terms that might not be obvious to you at first,
so we gonna explain them to you here. 

- *Duplicate*: A file that has the same hash as another file.
- *Original*: In a group of *duplicates*, one file is said to 
              be the original file, from which the copies
              where created. This might or might not be true,
              but is an helpful assumption when deleting files.



Beginner Examples
-----------------

Let's just dive in into some examples: 

.. code-block:: bash

   $ rmlint

This simply scans your current working directory for lint and reports them in
your terminal. Note that **nothing will be removed** (even if it prints ``rm``).  

Despite it's name, ``rmlint`` just finds suspicious files, but never modifies the
filesystem itself [*]_.  Instead it gives you detailed reports in different
formats to get rid of them yourself. These reports are called *outputs*.  By
default a shellscript will be written to ``rmlint.sh`` that contains readily
prepared shell commands to remove duplicates and other finds,

.. _[*]: You could say it should be named ``findlint``.

Filtering
---------

That was already a common usecase. What if we do not want to
check all files as dupes? ``rmlint`` has a good reportoire of 
options to select only certain files. We won't cover all options,
but the useful ones. If those options do not suffice, you can always use
external tools to feed ``rmlint's stdin``:

.. code-block:: bash

   $ find pics/ -iname '*.png' | rmlint -

Limit files by size using ``--size``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: bash

   # only check files between 20 MB und 1 Gigabyte.
   $ rmlint -s 20M-1G

If you want to limit files by their size, you can use ``--size`` (or short
``-s``). You give it  a size description, which tells ``rmlint`` the valid size
range. Either end can be given a unit (if none, ``rmlint`` assumes bytes)
similar to ``dd(2)``.

Limit files by their basename
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If only all files with the same basenames need to be checked you can use the
``-b`` (``--match-basename``)  option and their silblings ``-e``
(``--match-with-extension``) and ``-i`` (``--match-without-extension``) . 

.. code-block:: bash

   # Find all duplicate files with the same basename
   $ rmlint -b some_dir/ 
   ls some_dir/one/hello.c
   rm some_dir/two/hello.c

   # Find all duplicate files that have the same extension
   $ rmlint -e some_dir/ 
   ls some_dir/hello.c
   rm some_dir/hello_copy.c

   # Find all duplicate files that have the same basename
   # minus the extension
   $ rmlint -e some_dir/ 
   ls some_dir/hello.c
   rm some_dir/hello.cc

Limit files by their modification time
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This is an useful feature if you want to investigate only files newer than 
a certain date or if you want to progessively update the results, i.e. when you 
run ``rmlint`` in a script that wathces a directory for duplicates.

The most obvious way is using ``-N`` (``--newer-than=<timestamp>``):

.. code-block:: bash
   
   # Use a Unix-UTC Timestamp (seconds since epoch)
   $ rmlint -N 1414755960%

   # Find all files newer than file.png
   $ rmlint -N $(stat --print %Y file.png)

   # Alternatively use a ISO8601 formatted Timestamp
   $ rmlint -N 2014-09-08T00:12:32+0200

A little more scriptable solution is using ``-n`` (``--newer-than-stamp``) and 
``-O stamp:stamp.file`` (we'll come to outputs in a minute):

.. code-block:: bash
   
   # First run of rmlint:
   $ rmlint some_dir/ -O stamp:stamp.file
   ls some_dir/a.file
   rm some_dir/b.file

   # Second run, no changes:
   $ rmlint some_dir/ -n stamp.file
   <nothing>

   # Second run, new file copied:
   $ cp some_dir/a.file some_dir/c.file
   $ rmlint some_dir/ -n stamp.file
   ls some_dir/a.file
   rm some_dir/b.file
   rm some_dir/c.file

Outputs
-------

``rmlint`` is capable to create it's reports in several output-formats. 
Actually if you run it with the default options you already see two of those
formatters: Namely ``pretty`` and ``summary``.

Formatters can be added via the ``-O`` (``--add-output``) switch. 
The ``-o`` (``--output``) instead clears all defaults first and 
does the same as ``-O`` afterwards. 

Here's an example:

.. code-block:: bash

   $ rmlint -o json:stderr

Here you would get this output printed on ``stderr``:

.. code-block:: javascript

    [{
      "description": "rmlint json-dump of lint files",
      "cwd": "/home/user/",
      "args": "rmlint -o json:stderr"
    },
    {
      "type": "duplicate_file",
      "path": "/home/user/test/b/one",
      "size": 2,
      "inode": 2492950,
      "disk_id": 64771,
      "is_original": true,
      "mtime": 1414587002
    },
    ... snip ...
    {
      "aborted": false,
      "total_files": 145,
      "ignored_files": 9,
      "ignored_folders": 4,
      "duplicates": 11,
      "duplicate_sets": 2,
      "total_lint_size": 38
    }]

You probably noticed the colon in the commandline above. Everything before it is
the name of the output-format, everything behind is the path where the output
should land. Instead of an path you can also use ``stdout`` and ``stderr``, as
we did above.

Some formatters might be configured to generate subtly different output using
the ``-c`` (``--config``) command.  Here's the list of currently available
formatters and their config options:

:json:

    Outputs all finds as a json document. The document is a list of dictionaries, 
    where the first and last element is the header and the footer respectively,
    everything between are data-dictionaries. This format was chosen to allow
    application to parse the output in realtime while ``rmlint`` is still running. 

    The header contains information about the proram invocation, while the footer
    contains statistics about the program-run. Every data element has a type which
    identifies it's lint type (you can lookup all types here_).

    **Config values:**

    - *use_header=[true|false]:* Print the header with metadata.
    - *use_footer=[true|false]:* Print the footer with statistics.

.. _here: https://github.com/sahib/rmlint/blob/develop/src/file.c#L95

:sh: 

    Outputs a shell script that has default commands for all lint types.
    The script can be executed (it is already `chmod +x``'d by ``rmlint``).
    By default it will ask you if you really want to proceed. If you 
    do not want that you can pass the ``-d``. Addionally it will 
    delete itself after it ran, except you passed the ``-x`` switch.

    It is enabled by default and writes to ``rmlint.sh``. 

    Example output:

    .. code-block:: bash

      $ rmlint -o sh:stdout
      #!/bin/sh                                           
      # This file was autowritten by rmlint               
      # rmlint was executed from: /home/user/                      
      # You command line was: ./rmlint -o sh:rmlint.sh
       
      # ... snip ...

      echo  '/home/user/test/b/one' # original
      rm -f '/home/user/test/b/file' # duplicate
      rm -f '/home/user/test/a/two' # duplicate
      rm -f '/home/user/test/a/file' # duplicate
                       
      if [ -z $DO_REMOVE ]  
      then                  
        rm -f 'rmlint.sh';            
      fi                    

    **Config values:**

    - *use_ln=[true|false]:* Replace duplicate files with symbolic links (if on different
      device as original) or with hardlinks (if on same device as original).
    - *symlinks_only=[true|false]:* Always use symbolic links with *use_ln*, never
      hardlinks.

    **Example:**

    .. code-block:: bash

      $ rmlint -o sh:stdout -o sh:rmlint.sh -c sh:use_ln=true -c sh:symlinks_only=true
      ...
      echo  '/home/user/test/b/one' # original
      ln -s '/home/user/test/b/file' # duplicate
      $ ./rmlint.sh -d
      /home/user/test/b/one

:py: 

    Outputs a python script and a JSON document, just like the **json** formatter.
    The JSON document is written to ``.rmlint.json``, executing the script will
    make it read from there. This formatter is mostly intented for complex usecases
    where the lint needs special handling. Therefore the python script can be modified 
    to do things standard ``rmlint`` is not able to do easily. You have the full power of
    the Python language for your task, use it! 

    **Example:**

    .. cod-block:: bash

       $ rmlint -o py:remover.py 
       $ ./remover.py --dry-run    # Needs Python3
       Deleting twins of /home/user/sub2/a
       Handling (duplicate_file): /home/user/sub1/a
       Handling (duplicate_file): /home/user/a

       Deleting twins of /home/user/sub2/b
       Handling (duplicate_file): /home/user/sub1/b
       

:csv: 

    Outputs a csv formatted dump of all lint files. 
    It looks like this:

    .. code-block:: bash

      $ rmlint -o csv -D
      type,path,size,checksum
      emptydir,"/home/user/tree2/b",0,00000000000000000000000000000000
      duplicate_dir,"/home/user/test/b",4,f8772f6fda08bbc826543334663d6f13
      duplicate_dir,"/home/user/test/a",4,f8772f6fda08bbc826543334663d6f13
      duplicate_dir,"/home/user/tree/b",8,62202a79add28a72209b41b6c8f43400
      duplicate_dir,"/home/user/tree/a",8,62202a79add28a72209b41b6c8f43400
      duplicate_dir,"/home/user/tree2/a",4,311095bc5669453990cd205b647a1a00

    **Config values:**

    - *use_header=[true|false]:* Print the column name headers. 
  
:stamp:

    Outputs a timestamp of the time ``rmlint`` was run.

    **Config values:**

    - *iso8601=[true|false]:* Write an ISO8601 formatted timestamps or seconds
      since epoch?

:pretty: 

    Prettyprints the finds in a colorful output supposed to be printed on
    *stdout* or *stderr.* This is what you see by default.

:summary:

    Sums up the run in a few lines with some statistics. This enabled by default
    too. 

:progressbar: 

    Prints a progressbar during the run of ``rmlint``. This is recommended for
    large runs where the ``pretty`` formatter would print thousands of lines.

    **Config values:**

    - *update_interval=number:* Number of files to wait between updates.
      Higher values use less resources. 
  
Paranoia
--------

Let's face it, why should you trust ``rmlint``? 

Technically it only computes a hash of your file which might, by it's nature,
collide with the hash of a totally different file. If we assume a *perfect* hash
function (i.e. one that distributes it's hash values perfectly even over all
possible values), the probablilty of having a hash-collision is
:math:`\frac{1}{2^{128}}` for the default 128-bit hash. In practice most hash
functions have of course a much higher collision probablilty, since they trade
speed against accuracy. 

If you're wary you might want to make a bit more paranoid than it's default. 
By default the ``spooky`` hash algorithm is used, which we consider a good
tradeoff of speed and accuracy. ``rmlint's`` paranoia level can be easily 
inc/decreased using the ``-p`` (``--paranoid``)/ ``-P`` (``--less-paranoid``)
option (which might be given up to three times each).

Here's what they do in detail:

    * ``-p`` is equivalent to ``--flock-files --algorithm=bastard``
    * ``-pp`` is equivalent to ``--flock-files --algorithm=sha512``
    * ``-ppp`` is equivalent to ``--flock-files --algorithm=paranoid``

As you see, it just enables a bunch of other options. ``--flock-files`` will
lock all files found during traversal using ``flock(2)`` to be sure that no
modifications will be done during it's run. ``--algorithm`` changes the hash
algorithm to someting more secure. ``bastard`` is a 256bit hash that consists
of two 128bit subhashes (``murmur3`` and ``city`` if you're curious). 
One level up the well-known ``sha512`` (with 512bits obviously) is used.
Another level up, no real hash function is used. Instead, files are compared
byte-by-byte (which guarantees collision free output).

There is a bunch of other hash functions you can lookup in the manpage.
We recommend never to use the ``-P`` option.

.. note::

   Bugs in ``rmlint`` are sadly (or happily?) more likely than hash collisions.

Original detection
------------------

As mentioned before, ``rmlint`` divides a group of dupes in one original and
clones of that one. While the chosen original might not be the one that was
there first, it is a good thing to keep one file of a group to prevent dataloss.

The way ``rmlint`` chooses the original can be driven by the ``-S``
(``--sortcriteria``) option. 

Here's an example:

.. code-block:: bash
   
   # Normal run:
   $ rmlint  
   ls c
   rm a
   rm b

   # Use alphabetically first one as original
   $ rmlint -S a
   ls a
   rm b
   rm c

Arguably, using the alphabetically first one does not make much sense, except
for showing the feature. Therefore the default is **-S m** -- which takes the
oldest file, determined by it's modification time. 

Here's a table of letters you can supply to the ``-S`` option:

===== =========================== ===== ===========================
**m** keep lowest mtime (oldest)  **M** keep highest mtime (newest)
**a** keep first alphabetically   **A** keep last alphabetically
**p** keep first named path       **P** keep last named path
===== =========================== ===== ===========================

With time, new letters might be implemented. 

Flagging original directories
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

But what if you know better than ``rmlint``? What if your originals are in some
specific path, while you know that the files in it are copied over and over?
In this case you can flag directories on the commandline to be original:

.. code-block:: bash

   # Every path behind the // is considered to be an original
   $ rmlint a // b
   ls b/file
   rm a/file

There are two slightly esoteric related options to this:
``-k`` (``--keep-all-tagged``) and ``-m`` (``--must-match-tagged``).
``-k`` tells ``rmlint`` to never delete any duplicates that are in original
paths. Even if this means to ignore them. ``-m`` only accepts duplicates if they
have at least one related original in an original path.


Misc options
------------

If you read so far, you know ``rmlint`` pretty well by now. 
Here's just a list of options that are nice to know, but not essential:

- ``-r`` (``--hidden``): Include hidden files and directories - this is to save
  you from destroying git repositories (or similar programs) that save their
  information in a ``.git`` directory where ``rmlint`` often finds duplicates. 

  If you want to be safe you can do something like this:

  .. code-block:: bash
  
      $ find . | grep -v '\(.git\|.svn\)' | rmlint -

  But you would have checked the output anyways?

- If something ever goes wrong, it might help to increase the verbosity with
  ``-v`` (up to ``-vvv``).
- Usually the commandline output is colored, but you can disable it explicitly
  with ``-w`` (``--with-color``). If *stdout* or *stderr* is not an terminal
  anyways, ``rmlint`` will disable colors itself.
- You can limit the traversal depth with ``-d`` (``--max-depth``):

  .. code-block

      $ rmlint d 0 
      <finds everything in the same working directory>

- The still experimental ``-D`` (``--merge-directories``) option is able to
  merge found duplicates into duplicate directories. Use with care!

- If you want to prevent ``rmlint`` from crossing mountpoints (e.g. scan a home
  directory, but no the HD mounted in there), you can use the ``-X``
  (``--no-crossdev``) option.
