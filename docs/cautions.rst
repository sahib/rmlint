========
Cautions
========

This section covers good practice for safe duplicate removal.  It is not intended to
be specifically related to ``rmlint``.  It includes general discussion on duplicate
detection and shows, by example, some of the traps that duplicate finders can fall into.

1.  Good Practice when Deleting Duplicates
------------------------------------------

Backup your data
----------------

There is a wise adage, "if it's not backed up, it's not important".  It's just good
practice to keep your important data backed up.  In particular, any time you are
contemplating doing major file reorganisations or deletions, that's a good time to
make sure that your backups are up to date.

What about when you want to clean up your backups by deleting duplicate files from your
backup drive?  Well as long as your duplicate file finder is reliable, you shouldn't have
any problems.  Consider replacing the duplicate with a link (hardlink, symlink or reflink)
to the original data.  This still frees up the space, but makes it easier to find the file
if and when it comes time to restore files from backup.

Measure twice cut once
----------------------

This is a popular saying amongst builders; the same goes for your files.  Do at least some
sort of sanity check on which files are going to be deleted.  All duplicate file finders
(including rmlint) are capable of identifying false positives or more serious bugs.

Beware of unusual filename characters
-------------------------------------

Even a space in a filename is capable of causing grief.  Make sure your file deletion command
(or the one used by your duplicate finder) has the filename properly escaped.

Consider safe removal options
-----------------------------

Rather than deleting duplicates, consider moving them to a holding area or trash folder.  The
trash-cli utility (http://github.com/andreafrancia/trash-cli) is one option for this.

Another safe alternative, if your files are on a btrfs filesystem and you have linux
kernel 4.2 or higher, is to reflink the duplicate to the original.  You can do this via
``cp`` or using ``rmlint``:

.. code-block:: bash

   $ cp --reflink=always original duplicate
   $ rmlint --btrfs-clone original duplicate

The second option is actually safer because it verifies (via the kernel) that the files
are identical before creating the reflink.  Also it does not change the mtime or other
metadata of the duplicate, it only replaces the data with a link to original's data.


2. Attributes of a Robust Duplicate Finder
------------------------------------------
(also known as "Traps for young dupe finders")

Traversal Robustness
--------------------
(Path doubles, symlinks, and filesystem loops)

One simple trap for a dupe finder is to not realise that it has reached the same file
via two different paths.  Here's a simple "path double" example trying to trick a
duplicate file finder by "accidentally" feeding it the same path twice.  We'll use
fdupes (https://github.com/adrianlopezroche/fdupes) for this example:

.. code-block:: bash

   $ mkdir dir
   $ echo "important" > dir/file
   $ fdupes -r --delete --noprompt dir dir
   [no output]
   $ ls dir
   file

So far so good, fdupes didn't fall for the trick.  It has a check built-in which looks at
the files' device and inode numbers, which automatically filters out path doubles.

Let's try again using the -H option to find hardlinked duplicates:

.. code-block:: bash

   $ fdupes -r -H --delete --noprompt dir dir
      [+] dir/file
      [-] dir/file
   $ ls -l dir/
   total 0

Oh dear, our file is gone!  The problem is that hardlinks share the same device and inode numbers,
so the inode check is turned off for this option.

Dupe finders rdfind and dupd can also be tricked with the right combination of settings:

.. code-block:: bash

   $ # rdfind:
   $ rdfind -removeidentinode false -deleteduplicates true a a
   [snip]
   Now deleting duplicates:
   Deleted 1 files.
   $ ls -l dir/
   total 0

   $ # dupd:
   $ dupd scan --path /home/foo/a --path /home/foo/a
   Files scanned: 2
   Total duplicates: 2
   Run 'dupd report' to list duplicates.
   $ dupd report
   Duplicate report from database /home/daniel/.dupd_sqlite:
   20 total bytes used by duplicates:
     /home/foo/a/data
     /home/foo/a/data

"Ah but I'm not silly enough to enter the wrong path twice" you say.  Well maybe so, but
there are other ways that folder traversal can reach the same path twice, for example
via symlinks:

.. code-block:: bash

   $ mkdir dir
   $ echo "important" > dir/file
   $ ln -s dir link
   $ fdupes -r --delete --noprompt .
   $ ls -l dir/
   total 0

The filter used by rmlint to detect path doubles is:
matching device and inode and basename, and their parent directories also have matching device and inode.

That **seems** pretty fool-proof (see below) but please file an issue at https://github.com/sahib/rmlint/issues if you find
an exception.

.. code-block:: bash

   $ # default settings:
   $  rmlint a a
   ==> In total 2 files, whereof 0 are duplicates in 0 groups.
   ==> This equals 0 B of duplicates which could be removed.
   $
   $ # with hardlink duplicate detection enabled:   
   $  rmlint --hardlinked a a
   ==> In total 2 files, whereof 0 are duplicates in 0 groups.
   ==> This equals 0 B of duplicates which could be removed.

   
What if you're not deleting duplicates, just replacing them with hardlinks to the
original?  For example, ``rdfind`` has the ``-makehardlinks`` option to do this
for you.  Well surprisingly even this can end badly:

.. code-block:: bash

   $ echo data > file
   $ rdfind -makehardlinks true -removeidentinode false file file
   [snip]
   It seems like you have 2 files that are not unique
   Totally, 5 b can be reduced.
   Now making results file results.txt
   Now making hard links.
   failed to make hardlink file to file
   $ ls -l
   total 0


Checksum Collisions
-------------------


Unusual characters in file name
--------------------------------


Disk Thrash
------------



