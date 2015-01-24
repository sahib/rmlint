Frequently Asked Questions
==========================

``rmlint`` finds more/less dupes than tool **X**!
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

Can you implement feature **X**?
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
