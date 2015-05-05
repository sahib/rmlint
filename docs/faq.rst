Frequently Asked Questions
==========================

``rmlint`` finds more/less dupes than tool ``X``!
-------------------------------------------------

Make sure that *none* of the following applies to you:

Both tools might investigate a different number of files. ``rmlint`` e.g. does not
look through hidden files by default, while other tools might follow symlinks
by default. Suspicious options you should look into are:

* ``--hidden``: Disabled by default, since it might screw up ``.git/`` and similar directories.
* ``--hardlinked``: Might find larger amount files, but not more lint itself.
* ``--followlinks``: Might lead ``rmlint`` to different places on the filesystem.
* ``--merge-directories``: pulls in both ``--hidden`` and ``--hardlinked``.

If there's still a difference, check with another algorithm. In particular use
``-pp`` to enable paranoid mode. Also make sure to have ``-D``
(``--merge-directories``) disabled to see the raw number of duplicate files.

Still here? Maybe talk to us on the `issue tracker`_.

Can you implement feature ``X``?
--------------------------------

Depends. Go to to the `issue tracker`_ and open a feature request.

Here is a list of features where you probably have no chance:

- Port it to Windows.
- Find similar files like ``ssdeep`` does.

.. _`issue tracker`: https://github.com/sahib/rmlint/issues

I forgot to add some options before running on a large dataset. Do I need to re-run it?
---------------------------------------------------------------------------------------

Probably. It's not as bad as it sounds though. Your filesystem is probably very
good at caching. 

Still there are some cases where re-running might take a long time, like running
on network mounts. By default, ``rmlint`` writes a ``rmlint.json`` file along
the ``rmlint.sh``. This can be used to speed up the next run by passing it to
``--cache``. It should be noted that using the cache file for later runs is
discouraged since false positives will get likely if the data is changed in
between. Therefore there will never be an "auto-pickup" of the cache file.

I have a very large number of files and I run out of memory and patience.
-------------------------------------------------------------------------

As a rule of thumb, ``rmlint`` will allocate *~150 bytes* for every file it will
investigate. Additionally paths are stored in a patricia trie, which will
compress paths and save memory therefore.

The memory peak is usually short after it finished traversing all
files. For example, 5 million files will result in a memory footprint of roughly
1.0GB of memory in average. 

If that's still not enough read on.

*Some things to consider:*

- Use ``--with-metadata-cache`` to swap paths to disk. When needed the path is
  selected from disk instead of keeping them all in memory. This lowers the 
  memory footprint per file by a few bytes. Sometimes the difference may be
  very subtle since all paths in rmlint are stored by common prefix, i.e. for long
  but mostly identically paths the point after the difference is stored.
  
  This option will most likely only make sense if you files with long basenames.
  You might expect 10%-20% less memory as a rule of thumb.
- Use ``--without-fiemap`` on rotational disk to disable this optimization. With
  it enabled a table of the file's extents is stored to optimize disk access
  patterns. This lowers the memory footprint per file by around 50 bytes.
- Enable the progress bar with ``-g`` to keep track of how much data is left to
  scan.

*Caveats:*

- Some options like ``-D`` will not work well with ``--with-metadata-cache`` and
  use a fair bit of memory themselves. This is by the way they're working. Avoid
  them in this case. Also ``--cache`` might be not very memory efficient.
- The CPU usage might go up quite a bit, resulting longer runs.

*Also:*

``rmlint`` have been successfully used on datasets of 5 million files. See this
bug report for more information: `#109`_.

.. _`#109`: https://github.com/sahib/rmlint/issues/109
