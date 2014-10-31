Tutorial
========

.. todo:: Write it.

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

.. code-block:: json

    [{
      "description": "rmlint json-dump of lint files",
      "cwd": "/home/sahib/dev/rmlint/",
      "args": "rmlint -o json:stderr"
    },
    {
      "type": "duplicate_file",
      "path": "/home/sahib/dev/rmlint/test/b/one",
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
  identifies it's lint type (you can lookup all types here: ... TOOD ...).

:sh: 

  Outputs a shell script that has default commands for all lint types.
  The script can be executed (it is already `chmod +x``'d by ``rmlint``).
  By default it will ask you if you really want to proceed. If you 
  do not want that you can pass the ``-d``. Addionally it will 
  delete itself after it ran, except you passed the ``-x`` switch.

  Example output:

  .. code-block:: bash

    #!/bin/sh                                           
    # This file was autowritten by rmlint               
    # rmlint was executed from: /home/sahib/dev/rmlint/                      
    # You command line was: ./rmlint -o sh:rmlint.sh
     
    # ... snip ...

    echo  '/home/sahib/dev/rmlint/test/b/one' # original
    rm -f '/home/sahib/dev/rmlint/test/b/file' # duplicate
    rm -f '/home/sahib/dev/rmlint/test/a/two' # duplicate
    rm -f '/home/sahib/dev/rmlint/test/a/file' # duplicate
                     
    if [ -z $DO_REMOVE ]  
    then                  
      rm -f 'rmlint.sh';            
    fi                    


  
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
