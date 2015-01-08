Developer's Guide
=================

This guide is targeted to people that want to write new features or fix bugs in rmlint.

Bugs
----

Please use the issue tracker to post and discuss bugs and features:

    https://github.com/sahib/rmlint/issues

Philosophy
----------

We try to adhere to some principles when adding features:

* Try to stay compatible to standard unix' tools and ideas.
* Try to stay out of the users way and never be interactive.
* Try to make scripting as easy as possible.
* **Never** make ``rmlint`` modify the filesystem itself, only produce output
  to let the user easily do it.

Also keep this in mind, if you want to make a feature request.

Making contributions
--------------------

The code is hosted on GitHub, therefore our preferred way of receiving patches
is using GitHub's pull requests (normal git pull requests are okay too of course). 

.. note::

    ``origin/master`` should always contain working software. Base your patches
    and pull requests always on ``origin/develop``.

Here's a short step-by-step:

1. `Fork it`_.
2. Create a branch from develop. (``git checkout develop && git checkout -b my_feature``)
3. Commit your changes. (``git commit -am "Fixed it all."``)
4. Check if your commit message is good. (If not: ``git commit --amend``)
5. Push to the branch (``git push origin my_feature``)
6. Open a `Pull Request`_.
7. Enjoy a refreshing Tea and wait until we get back to you.

.. _`Fork it`: https://github.com/sahib/rmlint
.. _`Pull Request`: http://github.com/sahib/rmlint/pulls

Here are some other things to check before submitting your contribution:

- Does your code look alien to the other code? Is the style the same?
- Do all tests run? (Simply run ``nosetests`` to find out)
  Also after opening the pull request, your code will be checked via `TravisCI`_.
- Is your commit message descriptive?
- Is ``rmlint`` running okay inside of ``valgrind`` (i.e. no leaks and no memory violations)?

.. _`TravisCI`: https://travis-ci.org/sahib/rmlint

For language-translations/updates it is also okay to send the ``.po`` files via
mail at sahib@online.de, since not every translator is necessarily a
software developer.

Buildsystem Helpers
-------------------

Environement Variables
~~~~~~~~~~~~~~~~~~~~~~

:CFLAGS:

    Extra flags passed to the compiler.

:LDFLAGS:

    Extra flags passed to the linker.

:CC:

    Which compiler to use? 

.. code-block:: bash

   # Use clang and enable profiling, verbose build and enable debugging
   CC=clang CFLAGS='-pg' LDFLAGS='-pg' scons VERBOSE=1 DEBUG=1

Variables
~~~~~~~~~

:DEBUG:

    Enable debugging symbols for ``rmlint``. This should always be enabled during
    developement. Backtraces wouldn't be useful elsewhise.

:VERBOSE:

    Print the exact compiler and linker commands. Useful for troubleshooting
    build errors.

Arguments
~~~~~~~~~

:--prefix:

    Change the installation prefix. By default this is ``/usr``, but some users
    might prefer ``/usr/local`` or ``/opt``. 

:--actual-prefix:

    This is mainly useful for packagers. The ``rmlint`` binary knows where it
    is installed (which is needed to set e.g. the path to the gettext files).
    When installing a package, most of the time the build is installed to
    a local test environment first before being packed to ``/usr``. In this
    case the ``--prefix`` would be set to the path of the temporary build env,
    while ``--actual-prefix`` would be set to ``/usr``.

:--without-libelf:
    
    Do not link with ``libelf``, which is needed for nonstripped binary
    detection.

:--without-blkid:

    Do not link with ``libblkid``, which is needed to differentiate between
    normal rotational harddisks and non-rotational disks.

:--without-json-glib:

    Do not link with ``libjson-glib``, which is needed to load json-cache files.
    Without this library a warning is printed when using ``-C / --cache``.

:--without-fiemap:

    Do not attempt to use the ``FIEMAP ioctl(2)``. 

:--without-gettext:

    Do not link with ``libintl`` and do not compile any message catalogs.
    
All ``--without-*`` options come with a ``--with-*`` option that inverses its
effect.  By default ``rmlint`` is built with all features on the system, so you
do not need to specify any ``--with-*`` option normally.

Notable targets
~~~~~~~~~~~~~~~

:install:

    Install all program parts system-wide.

:config:

    Print a summary of all features that will be compiled and what the
    environment looks like.

:man:

    Build the manpage.

:docs:

    Build the onlice html docs (which you are reading now).

:test:

    Build the tests (requires ``python`` and ``nosetest`` installed).
    Optionally ``valgrind`` can be installed to run the tests through 
    valgrind:

    .. code-block:: bash

        $ USE_VALGRIND=1 nosetests  # or nosetests-3.3, python3 needed.

:xgettext:

    Extract a gettext ``.pot`` template from the source.

:dist: 

    Build a tarball suitable for release. Save it under
    ``rmlint-$major-$minor-$patch.tar.gz``. 

:release:

    Same as ``dist``, but reads the ``.version`` file and replaces the current
    version in the files that are not built by *scons*.


Sourcecode layout
-----------------

- All C-source lives in ``src``, the file names should be self explanatory.
- All documentation is inside ``docs``. 
- All translation stuff should go to ``po``.
- All packaging should be done in ``pkg/<distribution>``.
- Tests are written in Python and live in ``tests``.


Hashfunctions
-------------

Here is a short comparasion of the existing hashfunctions_ in ``rmlint`` (linear_ scale).
For reference: Those plots were rendered with these_ sources - which are very ugly, sorry.

If you want to add new hashfunctions, you should have some arguments why it is valueable and possiblye
even benchmark it with the above scripts to see if it's really that much faster.

Also keep in mind that most of the time the hashfunction is not the bottleneck.

.. _these: https://github.com/sahib/rmlint/tree/gh-pages/plots
.. _linear: https://raw.githubusercontent.com/sahib/rmlint/gh-pages/plots/hash_comparasion_lin.png
.. _hashfunctions: https://raw.githubusercontent.com/sahib/rmlint/gh-pages/plots/hash_comparasion_log.png

Optimizations
-------------

For sake of overview, here is a short list of optimizations implemented in ``rmlint``:

Obvious ones
~~~~~~~~~~~~

- Do not compare each file with each other by content, use a hashfunction to reduce
  comparison overhead drastically (introduces possibility of collisions though).
- Only compare files of same size with each other. 
- Use incremental hashing, i.e. hash block-wise each size group and stop 
  as soon a difference occurs or the file is read fully.
- Create one hashing thread for each physical disk.  This gives a big speedup if
  files are roughly evenly spread over multiple physical disks.

Subtle ones
~~~~~~~~~~~

- Check only executable files to be non-stripped binaries.
- Use ``preadv(2)`` based reading for small speeedups.
- Every thread in rmlint is shared, so only few calls to ``pthread_create`` are made.

Insane ones
~~~~~~~~~~~

- Check the device ID of each file to see if it on a rotational (normal hard
  disks) or on a non-rotational device (like a SSD). On the latter the file
  might be processed by several threads.
- Use ``fiemap ioctl(2)`` to analyze the harddisk layout of each file, so each
  block can read it in *perfect* order on a rotational device.
- Use a common buffer pool for IO buffers.
- Use only one hashsum per group of same-sized files.
- Implement paranoia check using the same algorithm as the incremental hash.  The
  difference is that large chunks of the file are read and kept in memory instead
  of just keeping the hash in memory.  This avoids the need for a two-pass algorithm
  (find matches using hashes then confirm via bytewise comparison).  Each file is
  read once only.  To our knowledge this is the first dupefinder which achieves
  bytewise comparison in O(N) time, even if there are large clusters of same-size
  files.  The downside is that it is somewhat memory-intensive (the total memory used
  is set to 256 MB by default but can be configured by ``--max-paranoid-ram`` option.

Future development ideas (and who is working on them)
~~~~~~~~~~~~~~~~~~~~~~~~

- GUI front-end (unclaimed)
  A user-friendly front-end to select command-line options instead of having to read
  the manpages.  Then launches rmlint in terminal with appropriate options.
- GUI wrapper (unclaimed)
  As above but add a GUI back-end to review results and decide actions for found lint.
- Support reflinks (SeeSpotRun)
  Implement a cp --reflink=always option for shell script outputs.
- Hashes in xattr (sahib)
  Store hashes in file xattr metadata to speed up subsequent dupe searches.
- GOptionEntry (unclaimed)
  Use GLib's GOptionEntry instead of getopts.  Requires a shift from repeated
  args (eg -vvv for max verbosity) to something else (eg -v5).
  
