========
Tutorial
========

Welcome to the Tutorial of ``rmlint``.

We use a few terms that might not be obvious to you at first,
so we gonna explain them to you here. 

:*Duplicate*:
             
    A file that has the same hash as another file.

:*Original*: 

    In a group of *duplicates*, one file is said to 
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

.. [*] You could say it should be named ``findlint``.

So for the above example, the full process would be:

.. code-block:: bash

   $ rmlint
   # (wait for rmlint to finish running)
   $ gedit rmlint.sh
   # (or any editor you prefer... review the content of rmlint.sh to
   #  check what it plans to delete; make any edits as necessary)
   $ ./rmlint.sh
   # (the rmlint.sh script will ask for confirmation, then delete the
   #  appropriate lint, then delete itself)


Filtering
---------

What if we do not want to check all files as dupes? ``rmlint`` has a
good reportoire of options to select only certain files. We won't cover
all options, but the useful ones. If those options do not suffice, you
can always use external tools to feed ``rmlint's stdin``:

.. code-block:: bash

   $ find pics/ -iname '*.png' | rmlint -

Limit files by size using ``--size``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: bash

   # only check files between 20 MB and 1 Gigabyte:
   $ rmlint --size 20M-1G
   # short form (-s) works just as well:
   $ rmlint -s 20M-1G
   # only check files bigger than 4 kB:
   $ rmlint -s 4K
   # only check files smaller than 1234 bytes:
   $ rmlint -s 0-1234
   
Valid units include:

|  K,M,G,T,P for powers of 1000
|  KB, MB, GB etc for powers of 1024
  
If no units are given, ``rmlint`` assumes bytes.


Limit files by their basename
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

By default, ``rmlint`` compares file contents, regardless of file name.
So if afile.jpg has the same content as bfile.txt (which is unlikely!),
then rmlint will find and report this as a duplicate.
You can speed things up a little bit by telling rmlint not to try to
match files unless they have the same or similar file names.  The three
options here are:

|  ``-b`` (``--match-basename``)  
|  ``-e`` (``--match-extension``)
|  ``-i`` (``--match-without-extension``) . 
  
Examples:

.. code-block:: bash

   # Find all duplicate files with the same basename:
   $ rmlint -b some_dir/ 
   ls some_dir/one/hello.c
   rm some_dir/two/hello.c
   # Find all duplicate files that have the same extension:
   $ rmlint -e some_dir/ 
   ls some_dir/hello.c
   rm some_dir/hello_copy.c
   # Find all duplicate files that have the same basename:
   # minus the extension
   $ rmlint -e some_dir/ 
   ls some_dir/hello.c
   rm some_dir/hello.bak

Limit files by their modification time
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This is an useful feature if you want to investigate only files newer than 
a certain date or if you want to progessively update the results, i.e. when you 
run ``rmlint`` in a script that watches a directory for duplicates.

The most obvious way is using ``-N`` (``--newer-than=<timestamp>``):

.. code-block:: bash
   
   # Use a Unix-UTC Timestamp (seconds since epoch)
   $ rmlint -N 1414755960%

   # Find all files newer than file.png
   $ rmlint -N $(stat --print %Y file.png)

   # Alternatively use a ISO8601 formatted Timestamp
   $ rmlint -N 2014-09-08T00:12:32+0200

If you are checking a large directory tree for duplicates, you can get
a supstantial speedup by creating a timestamp file each time you run
rmlint.  To do this, use command line options:
``-n`` (``--newer-than-stamp``) and 
``-O stamp:stamp.file`` (we'll come to outputs in a minute):
Here's an example for incrementally scanning your home folder:

.. code-block:: bash
   
   # First run of rmlint:
   $ rmlint /home/foobar -O stamp:/home/foobar/.rmlint.stamp
   ls /home/foobar/a.file
   rm /home/foobar/b.file

   # Second run, no changes:
   $ rmlint /home/foobar -n /home/foobar/.rmlint.stamp
   <nothing>

   # Second run, new file copied:
   $ cp /home/foobar/a.file /home/foobar/c.file
   $ rmlint /home/foobar -n /home/foobar/.rmlint.stamp
   ls /home/foobar/a.file
   rm /home/foobar/b.file
   rm /home/foobar/c.file
   
Note that ``-n`` updates the timestamp file each time it is run.

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

    .. code-block:: bash

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
:math:`\frac{1}{2^{128}}` for the default 128-bit hash.  Of course hash
functions are not totally random, so the collision probability is slightly higher.

If you're wary you might want to make a bit more paranoid than it's default. 
By default the ``spooky`` hash algorithm is used, which we consider a good
tradeoff of speed and accuracy. ``rmlint``'s paranoia level can be easily 
inc/decreased using the ``-p`` (``--paranoid``)/ ``-P`` (``--less-paranoid``)
option (which might be given up to three times each).

Here's what they do in detail:

    * ``-p`` is equivalent to ``--algorithm=bastard``
    * ``-pp`` is equivalent to ``--algorithm=sha512``
    * ``-ppp`` is equivalent to ``--algorithm=paranoid``

As you see, it just enables a certain hash algorithm. ``--algorithm`` changes
the hash algorithm to someting more secure. ``bastard`` is a 256bit hash that
consists of two 128bit subhashes (``murmur3`` and ``city`` if you're curious).
One level up the well-known ``sha512`` (with 512bits obviously) is used.
Another level up, no hash function is used. Instead, files are compared
byte-by-byte (which guarantees collision free output).

There is a bunch of other hash functions you can lookup in the manpage.
We recommend never to use the ``-P`` option.

.. note::

   Even with the default options, the probability of a false positive doesn't
   really start to get significant until you have around 1,000,000,000,000,000,000
   files all of the same file size.  Bugs in ``rmlint`` are sadly (or happily?)
   more likely than hash collisions.

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

Alphabetically first makes sense in the case of
backup files, ie **a.txt.bak** comes after **a.txt**.

Here's a table of letters you can supply to the ``-S`` option:

===== =========================== ===== ===========================
**m** keep lowest mtime (oldest)  **M** keep highest mtime (newest)
**a** keep first alphabetically   **A** keep last alphabetically
**p** keep first named path       **P** keep last named path
===== =========================== ===== ===========================

The default setting is ``-S m`` -- which takes the
oldest file, determined by it's modification time.
Multiple sort criteria can be specified, eg ``-S mpa`` will sort first by
mtime, then (if tied), based on which path you specified first in the
rmlint command, then finally based on alphabetical order of file name.
Note that "original directory" criteria (see below) take precedence over
the ``-S`` options.

Flagging original directories
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

But what if you know better than ``rmlint``? What if your originals are in some
specific path, while you know that the files in it are copied over and over?
In this case you can flag directories on the commandline to be original, by using
a special separator (//) between the duplicate and original paths.  Every path
after the // separator is considered to be "tagged" and will be treated as an
original where possible.  Tagging always takes precedence over the ``-S`` options above.

.. code-block:: bash

   $ rmlint a // b
   ls b/file
   rm a/file

If there are more than one tagged files in a duplicate group then the highest
ranked (per ``-S`` options) will be kept.  In order to never delete any tagged files,
there is the ``-k`` (``--keep-all-tagged``) option.  A slightly more esoteric option
is ``-m`` (``--must-match-tagged``), which only looks for duplicates where there is
an original in a tagged path.

Here's a real world example using these features:  I have an portable backup drive with some
old backups on it.  I have just backed up my home folder to a new backup drive.  I want
to reformat the old backup drive and use it for something else.  But first I want to
check that there are no "originals" on the drive.  The drive is mounted at /media/portable.  

.. code-block:: bash

   # Find all files on /media/portable that can be safely deleted:
   $ rmlint -km /media/portable // ~
   # check the shellscript looks ok:
   $ less ./rmlint.sh
   # run the shellscript to delete the redundant backups
   $ ./rmlint.sh
   # run again (to delete empty dirs)
   $ rmlint -km /media/portable // ~
   $ ./rmlint.sh   
   # see what files are left:
   $ tree /media/portable
   # recover any files that you want to save, then you can safely reformat the drive

In the case of nested mountpoints, it may sometimes makes sense to use the 
opposite variations, ``-K`` (``--keep-all-untagged``) and ``-M`` (``--must-match-untagged``).


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

  .. code-block:: bash

      $ rmlint -d 0 
      <finds everything in the same working directory>

- The still experimental ``-D`` (``--merge-directories``) option is able to
  merge found duplicates into duplicate directories. Use with care!

- If you want to prevent ``rmlint`` from crossing mountpoints (e.g. scan a home
  directory, but no the HD mounted in there), you can use the ``-X``
  (``--no-crossdev``) option.

- It is possible to tell ``rmlint`` that it should not scan the whole file.
  With ``-q`` (``--clamp-low``) / ``-Q`` (``--clamp-top``) it is possible to
  limit the range to a starting point (``-q``) and end point (``-Q``). 
  The point where to start might be either given as percent value, factor (percent / 100)
  or as an absolute offset. 

  If the file size is lower than the absolute offset, the file is simply ignored.

  This feature might prove useful if you want to examine files with a constant header.
  The constant header might be different, i.e. by a different ID, but the content might be still
  the same. In any case it is advisable to use this option with care.

  Example:

  .. code-block:: bash

    # Start hashing at byte 100, but not more than 90% of the filesize.
    $ rmlint -q 100 -Q.9 
