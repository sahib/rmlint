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

.. .. image:: https://raw.githubusercontent.com/sahib/rmlint/develop/docs/_static/logo.png
..    :align: left 
..    :width: 150

.. raw:: html

    <center>

.. image:: https://readthedocs.org/projects/rmlint/badge/?version=latest
   :target: http://rmlint.rtfd.org

.. image:: https://img.shields.io/travis/sahib/rmlint/develop.svg?style=flat
   :target: https://travis-ci.org/sahib/rmlint

.. image:: https://img.shields.io/github/issues/sahib/rmlint.svg?style=flat
   :target: https://github.com/sahib/rmlint/issues

.. image:: https://img.shields.io/github/release/sahib/rmlint.svg?style=flat
   :target: https://github.com/sahib/rmlint/releases

.. image:: http://img.shields.io/badge/license-GPLv3-4AC51C.svg?style=flat
   :target: https://www.gnu.org/licenses/quick-guide-gplv3.html.en

.. image:: https://badges.gitter.im/rmlint/community.svg
   :target: https://gitter.im/rmlint/community?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge


.. raw:: html

    </center>

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

If you have usage questions or find weird behaviour, you can also try to reach
us via *IRC* in ``#rmlint`` on ``irc.freenode.net``.

Since version ``2.4.0`` we also feature an optional graphical user interface:

.. raw:: html

   <center>
    <iframe src="https://player.vimeo.com/video/139999878" width="780"
    height="450"
    frameborder="0" webkitallowfullscreen mozallowfullscreen
    allowfullscreen></iframe>
   </center>

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
===================================  ============================= ===========================================

Additional thanks to:

.. hlist::
    :columns: 3

    - `vvs-`_ (Scalability testing)
    - *Attila* Toth 
    - All sane bugreporters (there are not many)
    - All packagers, porters and patchers.


.. _qitta: https://github.com/qitta
.. _dieterbe: https://github.com/Dieterbe
.. _`My cats`: http://imgur.com/gallery/rims0yl
.. _`vvs-`: https://github.com/vvs-?tab=activity

License
-------

``rmlint`` is licensed under the terms of GPLv3_.

.. _GPLv3: http://www.gnu.org/copyleft/gpl.htm
.. _sahib: https://github.com/sahib
.. _SeeSpotRun: https://github.com/SeeSpotRun
