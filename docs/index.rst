|

``rmlint`` finds space waste and other broken things on your filesystem and offers
to remove it. It is able to find:

.. hlist::
   :columns: 2

   + Duplicate files & directories.
   + Nonstripped Binaries
   + Broken symlinks.
   + Empty files.
   + Recursive empty directories.
   + Files with broken user or group id.

.. raw:: html

    <script type="text/javascript" src="https://asciinema.org/a/8leoh1nqxz7t5o3jkedkh4421.js" id="asciicast-8leoh1nqxz7t5o3jkedkh4421" async></script>

|


**Key Features:**

.. hlist::
   :columns: 3

   + Extremely fast.
   + Flexible and easy commandline options.
   + Choice of several hashes for hash-based duplicate detection
   + Option for exact byte-by-byte comparison (only slightly slower).
   + Numerous output options.
   + Option to store time of last run; next time will only scan new files.
   + Many options for original selection / prioritisation.
   + Can handle very large file sets (millions of files).
   + Colorful progressbar. (😃)

----

.. include:: _badges.rst

User manual
-----------

Although ``rmlint`` is easy to use, you might want to read these chapters first.
They show you the basic principles and most of the advanced options:

.. toctree::
   :maxdepth: 2

   install
   tutorial
   gui
   cautions
   faq

Since version ``2.4.0`` we also feature an optional graphical user interface:

.. raw:: html

   <div style="text-align: center">
    <iframe src="https://player.vimeo.com/video/139999878" width="780"
    height="450" style="border: 0" allow="fullscreen"></iframe>
   </div>

Informative reference
---------------------

These chapters are informative and are not essential for the average
user. People that want to extend ``rmlint`` might want to read this though: 

.. toctree::
   :maxdepth: 1
       
   developers
   translators
   benchmarks
   Online-manpage of rmlint(1) <rmlint.1>

The Changelog_ is also updated with new and futures features, fixes and overall
changes.

.. _Changelog: https://github.com/sahib/rmlint/blob/develop/CHANGELOG.md


Authors
-------

``rmlint`` was and is written by: 

===================================  ============================= ===========================================
*Christopher Pahl*                   https://github.com/sahib      2010-2019
*Daniel Thomas*                      https://github.com/SeeSpotRun 2014-2019
*Cebtenzzre*                         https://github.com/Cebtenzzre 2021-2023
*Vassili Tchersky*                   https://github.com/vassilit   2025-
===================================  ============================= ===========================================

Additional thanks to:

.. hlist::
    :columns: 1

    - *Attila* Toth 
    - All sane bugreporters (there are not many)
    - All packagers, porters and patchers.

License
-------

``rmlint`` is licensed under the terms of GPLv3_.

.. _GPLv3: http://www.gnu.org/copyleft/gpl.htm
