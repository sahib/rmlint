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
``-p`` to enable paranoid mode. Also make sure to have ``-D``
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

Probably not. Since ``rmlint 2.3.0`` there is ``--replay`` which can be used to 
to re-output a json file of a prior run.

If you have changed the filesystem that might not be a good idea of course. In
this case you'll have to re-run, but it's not as bad as it sounds though. Your
filesystem is probably very good at caching. 

If you only want to see the difference to what changed since last time you can
look into ``-n --newer-than-stamp / -N --newer-than``.

In some cases you might really need to re-run, but if that happens often, you
might look into ``--xattr-write`` and ``--xattr-read`` which is capable 
of writing finished checksums to extended attributes of each processed file.

I have a very large number of files and I run out of memory and patience.
-------------------------------------------------------------------------

As a rule of thumb, ``rmlint`` will allocate *~150 bytes* for every file it will
investigate. Additionally paths are stored in a patricia trie, which will
compress paths and save memory therefore.

The memory peak is usually shortly after it finished traversing all
files. For example, 5 million files will result in a memory footprint of roughly
1.0GB of memory in average. 

*Some things to consider:*

- Enable the progress bar with ``-g`` to keep track of how much data is left to
  scan.

*Also:*

``rmlint`` have been successfully used on datasets of 5 million files. See this
bug report for more information: `#109`_.

.. _`#109`: https://github.com/sahib/rmlint/issues/109
