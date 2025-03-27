===================================================
Cautions (or *why it's hard to write a dupefinder*)
===================================================

This section covers good practice for safe duplicate removal.  It is not intended to
be specifically related to ``rmlint``.  It includes general discussion on duplicate
detection and shows, by example, some of the traps that duplicate finders can fall into.
This section might not only be useful for developers of dupe finders, but also
educational for users that strive for best practices regarding deduplication.

Good Practice when Deleting Duplicates
--------------------------------------

Backup your data
~~~~~~~~~~~~~~~~

There is a wise adage, *"if it's not backed up, it's not important"*.  It's just good
practice to keep your important data backed up.  In particular, any time you are
contemplating doing major file reorganisations or deletions, that's a good time to
make sure that your backups are up to date.

What about when you want to clean up your backups by deleting duplicate files from your
backup drive?  Well as long as your duplicate file finder is reliable, you shouldn't have
any problems.  Consider replacing the duplicate with a link (hardlink, symlink or reflink)
to the original data.  This still frees up the space, but makes it easier to find the file
if and when it comes time to restore files from backup.

Measure twice, cut once
~~~~~~~~~~~~~~~~~~~~~~~

This is a popular saying amongst builders; the same goes for your files.  Do at least some
sort of sanity check on which files are going to be deleted.  All duplicate file finders
(including ``rmlint``) are capable of identifying false positives or more serious bugs.

Beware of unusual filename characters
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Even a space in a filename is capable of `causing grief`_.  Make sure your file deletion command
(or the one used by your duplicate finder) has the filename properly escaped.

.. _`causing grief`: https://github.com/MrMEEE/bumblebee-Old-and-abbandoned/issues/123

Consider safe removal options
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Rather than deleting duplicates, consider moving them to a holding area or trash
folder.  The `trash-cli utility`_ is one option for this.  Alternatively if
using the ``rmlint`` shell script you can replace the ``remove_cmd`` section as
follows to move the files to */tmp*:

.. _`trash-cli utility`: http://github.com/andreafrancia/trash-cli

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

Another safe alternative, if your files are on a copy-on-write filesystem such
as ``btrfs``, and you have linux kernel 4.2 or higher, is to use a deduplication
utility such as ``duperemove`` or ``rmlint --dedupe``:

.. code-block:: bash

   $ duperemove -dh original duplicate
   $ rmlint --dedupe original duplicate

Both of the above first verify (via the kernel)  that ``original`` and
``duplicate`` are identical, then modifies ``duplicate`` to reference
``original``'s data extents.  Note they do not change the mtime or other
metadata of the duplicate (unlike hardlinks).

If you pass ``-c sh:link`` to ``rmlint``, it will even check for you if your
filesystem is capable of reflinks and emit the correct command conveniently.

You might think hardlinking as a safe alternative to deletion, but in fact hardlinking
first deletes the duplicate and then creates a hardlink to the original in its place.
If your duplicate finder has found a false positive, it is possible that you may lose
your data.


Attributes of a Robust Duplicate Finder
---------------------------------------

(also known as *"Traps for young dupe finders"*)

Traversal Robustness
~~~~~~~~~~~~~~~~~~~~

**Path Doubles**


One simple trap for a dupe finder is to not realise that it has reached the same file
via two different paths.  This can happen due to user inputting overlapping paths to
traverse, or due to symlinks or other filesystem loops such as bind mounts.
Here's a simple "path double" example trying to trick a duplicate file finder
by "accidentally" feeding it the same path twice.  We'll use
fdupes_ for this example:

.. _fdupes: https://github.com/adrianlopezroche/fdupes

.. code-block:: bash

   $ mkdir dir
   $ echo "important" > dir/file
   $ fdupes -r --delete --noprompt dir dir
   [no output]
   $ ls dir
   file

So far so good, ``fdupes`` didn't fall for the trick.  It has a check built-in which looks at
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

Dupe finders ``rdfind`` and ``dupd`` can also be tricked with the right combination of settings:

.. code-block:: bash

   $ rdfind -removeidentinode false -deleteduplicates true a a
   [snip]
   Now deleting duplicates:
   Deleted 1 files.
   $ ls -l dir/
   total 0

   $ dupd scan --path /home/foo/a --path /home/foo/a
   Files scanned: 2
   Total duplicates: 2
   Run 'dupd report' to list duplicates.
   $ dupd report
   Duplicate report from database /home/foo/.dupd_sqlite:
   20 total bytes used by duplicates:
     /home/foo/a/data
     /home/foo/a/data

*Solution:*

For a duplicate finder to be able to find hardlinked duplicates, without also inadvertently
identifying a file as a duplicate or itself, a more sophisticated test is required.  Path
doubles will always have:

- matching device and inode.
- matching basename.
- parent directories also have matching device and inode.

That **seems** pretty fool-proof (see ``rmlint`` example below) but please file an issue
on our `Issue Tracker`_ if you find an exception.

.. _`Issue Tracker`: https://github.com/sahib/rmlint/issues

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

**Symlinks:**

*"Ah but I'm not silly enough to enter the same path twice"* you say.  Well maybe so, but
there are other ways that folder traversal can reach the same path twice, for example
via symbolic links:

.. code-block:: bash

   $ mkdir dir
   $ echo "important" > dir/file
   $ ln -s dir link
   $ fdupes -r --delete --noprompt .
   $ ls -l dir/
   total 0

Symlinks can make a real mess out of filesystem traversal:

.. code-block:: bash

   $ mkdir dir
   $ cd dir
   $ ln -s . link
   $ cd ..
   $ echo "data" > dir/file
   $ fdupes -rHs dir
   dir/file
   dir/link/file
   dir/link/link/file
   [snip]
   dir/link/link/link/link/link/link/link/link/link/link/link/link/link/link/link/link/link/link/link/link/link/link/link/link/link/link/link/link/link/link/link/link/link/link/link/link/link/link/link/link/file

   Set 1 of 1, preserve files [1 - 41, all]:

*Solution:*

During traversal, the duplicate finder should keep track of all folders visited (by device and inode number).
Don't re-traverse folders that were already traversed.

**Hardlinks:**

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

*Solution:*

Don't find false positives. Check files are on same filesystem before trying to create hardlink.
Temporarily rename the duplicate before creating the hardlink and then deleting the renamed file.

Collision Robustness
~~~~~~~~~~~~~~~~~~~~

**Duplicate detection by file hash**

If a duplicate finder uses file hashes to identify duplicates, there is a very
small risk that two different files have the same hash value.  This is called a
*hash collision* and can result in the two files being falsely flagged as
duplicates.

Several duplicate finders use the popular MD5 Hash, which is 128 bits
long.  With a 128-bit hash, if you have a million sets of same-size files, each set containing
a million different files, the chance of a hash collision is about
``0.000 000 000 000 000 000 147%``. To get a ``0.1%`` chance of a hash collision you would
need nine hundred thousand million (:math:`9\times10^{11}`) groups of (:math:`9\times10^{11}`) files each, or one group
of eight hundred thousand million million (:math:`8\times10^{17}`) files.

If someone had access to your files, and *wanted* to create a malicious duplicate, they
could potentially do something like this (based on http://web.archive.org/web/20071226014140/http://www.cits.rub.de/MD5Collisions/):

.. code-block:: bash

   $ mkdir test && cd test
   $ # get two different files with same md5 hash:
   $ wget http://web.archive.org/web/20071226014140/http://www.cits.rub.de/imperia/md/content/magnus/order.ps
   $ wget http://web.archive.org/web/20071226014140/http://www.cits.rub.de/imperia/md/content/magnus/letter_of_rec.ps
   $ md5sum *  # verify that they have the same md5sum
   a25f7f0b29ee0b3968c860738533a4b9  letter_of_rec.ps
   a25f7f0b29ee0b3968c860738533a4b9  order.ps
   $ sha1sum * # verify that they are not actually the same
   07835fdd04c9afd283046bd30a362a6516b7e216  letter_of_rec.ps
   3548db4d0af8fd2f1dbe02288575e8f9f539bfa6  order.ps
   $ rmlint -a md5 . -o pretty  # run rmlint using md5 hash for duplicate file detection
   # Duplicate(s):
       ls '/home/foo/test/order.ps'
       rm '/home/foo/test/letter_of_rec.ps'
   $ rmlint test -a sha1 -o summary   # run using sha1 hash
   ==> In total 2 files, whereof 0 are duplicates in 0 groups.

If your intention was to free up space by hardlinking the duplicate to the original, you would end up with two
hardlinked files, one called ``order.ps`` and the other called
``letter_of_rec.ps``, both containing the contents of ``order.ps``.

*Solution:*

``fdupes`` detects duplicates using MD5 Hashes, but eliminates the collision
risk by doing a byte-wise comparison of the duplicates detected.  This means
each file is read twice, which can tend to slow things down.

``dupd`` uses direct file comparison, unless there are more than 3 files in a set of duplicates, in which
case it uses MD5 only.

If you use ``rmlint``'s ``sha1`` hash features, which features 160 bit output,
you need at least :math:`5.4\times10^{22}` files before you get a :math:`0.1\%`
probability of collision.  ``rmlint``'s ``-p`` option uses ``SHA512``
(:math:`5.2\times10^{75}` files for :math:`0.1\%` risk), while ``rmlint``'s
``-p`` option uses direct file comparison to eliminate the risk altogether.
Refer to the :ref:`benchmark_ref` chapter for speed and memory overhead
implications.


Unusual Characters Robustness
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Spaces, commas, nonprinting characters etc can all potentially trip up a duplicate finder or the subsequent file
deletion command.  For example:

.. code-block:: bash

   $ mkdir test
   $ echo "data" > 'test/\t\r\"\b\f\\,.'
   $ cp test/\\t\\r\\\"\\b\\f\\\\\,. test/copy  # even just copying filenames like this is ugly!
   $ ls -1 test/
   copy
   \t\r\"\b\f\\,.
   $ md5sum test/*  # md5's output gets a little bit mangled by the odd characters
   6137cde4893c59f76f005a8123d8e8e6  test/copy
   \6137cde4893c59f76f005a8123d8e8e6  test/\\t\\r\\"\\b\\f\\\\,.
   $ dupd scan --path /home/foo/test
   SKIP (comma) [/home/foo/test/\t\r\"\b\f\\,.]
   Files scanned: 1
   Total duplicates: 0

*Solution:* Be careful!

*"Seek Thrash"* Robustness
~~~~~~~~~~~~~~~~~~~~~~~~~~

Duplicate finders use a range of strategies to find duplicates.  It is common to reading and compare small increments
of potential duplicates.  This avoids the need to read the whole file if the files differ in the first few megabytes,
so this can give a major speedup in some cases.  However, in the case of hard disk drives, constantly reading small
increments from several files at the same time causes the hard drive head to have to jump around ("seek thrash").

Here are some speed test results showing relative speed for scanning my ``/usr`` folder (on SSD) and an HDD copy of same.
The speed ratio gives an indication of how effectively the search algorithm manages disk seek overheads:

+----------------+----------------+---------------------+---------+
| Program        | ``/usr`` (SSD) |  ``/mnt/usr`` (HDD) | *Ratio* |
+================+================+=====================+=========+
| ``dupd``       |   48s          |  1769s              | 36.9    |
+----------------+----------------+---------------------+---------+
| ``fdupes``     |   65s          |  486s               |  7.5    |
+----------------+----------------+---------------------+---------+
| ``rmlint``     |   38s          |  106s               |  2.8    |
+----------------+----------------+---------------------+---------+
| ``rmlint -p``  |   40s          |  139s               |  3.5    |
+----------------+----------------+---------------------+---------+

.. note::

    Before each run, disk caches were cleared:

    .. code-block:: bash

        $ sync && echo 3 | sudo tee /proc/sys/vm/drop_caches

*Solution:*

Achieving good speeds on HDD's requires a balance between small file increments early on, then switching to
bigger file increments.  Fiemap information (physical location of files on the disk) can be used to sort the
files into an order that reduces disk seek times.


Memory Usage Robustness
~~~~~~~~~~~~~~~~~~~~~~~

When scanning very large filesystems, duplicate finders may have to hold a large amount of information in
memory at the same time.  Once this information exceeds the computers' RAM, performance will suffer
significantly.  ``dupd`` handles this quite nicely by storing a lot of the data in a sqlite database file,
although this may have a slight performance penalty due to disk read/write time to the database file.
``rmlint`` uses a path tree structure to reduce the memory required to store all traversed paths.
