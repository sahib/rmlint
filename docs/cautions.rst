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
trash-cli utility (http://github.com/andreafrancia/trash-cli) is one option for this.  Alternatively
if using the ``rmlint`` shell script you can replace the ``remove_cmd`` section as follows to move
the files to /tmp:

.. code-block:: bash

   remove_cmd() {
       echo 'Deleting:' "$1"
       if original_check "$1" "$2"; then
           if [ -z "$DO_DRY_RUN" ]; then
               # was: rm -rf "$1"
               mv -p "$1" "/tmp$1"
           fi
       fi
   }

Another safe alternative, if your files are on a btrfs filesystem and you have linux
kernel 4.2 or higher, is to reflink the duplicate to the original.  You can do this via
``cp --reflink`` or using ``rmlint --btrfs-clone``:

.. code-block:: bash

   $ cp --reflink=always original duplicate  # deletes dupicate and replaces it with reflink copy of original
   $ rmlint --btrfs-clone original duplicate  # does and in-place clone

The second option is actually safer because it verifies (via the kernel) that the files
are identical before creating the reflink.  Also it does not change the mtime or other
metadata of the duplicate.

You might think hardlinking as a safe alternative to deletion, but in fact hardlinking
first deletes the duplicate and then creates a hardlink to the original in its place.
If your duplicate finder has found a false positive, it is possible that you may lose
your data.


2. Attributes of a Robust Duplicate Finder
------------------------------------------
(also known as "Traps for young dupe finders")

Traversal Robustness
--------------------
(Path doubles, symlinks, and filesystem loops)

One simple trap for a dupe finder is to not realise that it has reached the same file
via two different paths.  This can happen due to user inputting overlapping paths to
traverse, or due to symlinks or other filesystem loops such as bind mounts.
Here's a simple "path double" example trying to trick a duplicate file finder
by "accidentally" feeding it the same path twice.  We'll use
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

"Ah but I'm not silly enough to enter the same path twice" you say.  Well maybe so, but
there are other ways that folder traversal can reach the same path twice, for example
via symlinks:

.. code-block:: bash

   $ mkdir dir
   $ echo "important" > dir/file
   $ ln -s dir link
   $ fdupes -r --delete --noprompt .
   $ ls -l dir/
   total 0

For a duplicate finder to be able to find hardlinked duplicates, without also inadvertently
identifying a file as a duplicate or itself, a more sophisticated test is required.  Path
doubles will always have:

- matching device and inode
- matching basename
- parent directories also have matching device and inode.

That **seems** pretty fool-proof (see rmlint example below) but please file an issue
at https://github.com/sahib/rmlint/issues if you find an exception.

.. code-block:: bash

   $ echo "data" > dir/file
   $ # rmlint with default settings:
   $  rmlint dir dir
   ==> In total 2 files, whereof 0 are duplicates in 0 groups.
   ==> This equals 0 B of duplicates which could be removed.
   $
   $ # rmlint with hardlink duplicate detection enabled:
   $  rmlint --hardlinked dir dir
   ==> In total 2 files, whereof 0 are duplicates in 0 groups.
   ==> This equals 0 B of duplicates which could be removed.
   $ ls dir
   file

Also as noted above, replacing duplicates with hardlinks can still end badly if there are
false positives.  For example, using ``rdfind``'s  the ``-makehardlinks`` option:

.. code-block:: bash

   $ echo "data" > dir/file
   $ rdfind -removeidentinode false -makehardlinks true dir dir
   [snip]
   It seems like you have 2 files that are not unique
   Totally, 5 b can be reduced.
   Now making results file results.txt
   Now making hard links.
   failed to make hardlink dir/file to dir/file
   $ ls -l dir
   total 0


Collision Robustness
--------------------

If a duplicate finder uses file hashes to identify duplicates, there is a very small
risk that two different files have the same hash value.  This is called a "hash collision"
and can result in the two files being falsely flagged as duplicates.

Several duplicate finders use the popular md5 hash, which is 128 bits
long.  With a 128-bit hash, if you have a million different files of the same file
size, the chance of a hash collision is about 0.000 000 000

If someone had access to your files, and wanted to create a malicious duplicate, they
could do this (based on http://web.archive.org/web/20071226014140/http://www.cits.rub.de/MD5Collisions/):

.. code-block:: bash

   $ # get two different files with same md5 hash:
   $ wget http://web.archive.org/web/20071226014140/http://www.cits.rub.de/imperia/md/content/magnus/order.ps
   $ wget http://web.archive.org/web/20071226014140/http://www.cits.rub.de/imperia/md/content/magnus/letter_of_rec.ps
   $ # verify that they have the same md5sum
   $ md5sum *
   XXXX  order.ps
   XXXX  letter_of_rec.ps
   $ rmlint .
   ==> In total 6 files, whereof 0 are duplicates in 0 groups.
   [daniel@johnny fdupes]$ rmlint -a md5 .
   # Duplicate(s):
       ls /home/foo/order.ps
       rm /home/foo/letter_of_rec.ps

If your intention was to free up space by hardlinking the duplicate to the original, you would end up with two
hardlinked copies of order.ps

``fdupes`` eliminates this risk by

The default

Unusual characters in file name
--------------------------------


Disk Thrash
------------



