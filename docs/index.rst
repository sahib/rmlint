.. .. image:: _static/screenshot.png
..    :align: center
..    :width: 66%

Introduction
------------

.. image:: https://readthedocs.org/projects/rmlint/badge/?version=latest
   :target: https://rmlint.rtfd.org

.. image:: https://img.shields.io/travis/sahib/rmlint/develop.svg?style=flat
   :target: https://travis-ci.org/sahib/rmlint

.. image:: https://img.shields.io/github/issues/sahib/rmlint.svg?style=flat
   :target: https://github.com/sahib/rmlint/issues

.. image:: https://img.shields.io/github/release/sahib/rmlint.svg?style=flat
   :target: https://github.com/sahib/rmlint/releases


**rmlint** finds space waste and other broken things on your filesystem and offers
to remove it. It is able to find:

.. hlist::
   :columns: 2

   + Duplicate files.
   + Nonstripped Binaries 
   + Broken symlinks.
   + Empty files.
   + Recursive empty directories.
   + Files with broken user or group id.

**Key Features:**

.. hlist::
   :columns: 2

   + Extremely fast.
   + Exchangeable hashing algorithm.
   + Numerous output formats.
   + Easy commandline interface.
   + Possibility to update files with newer mtime.
   + Many options for originaldetection.


.. DANGER::

    **rmlint** is currently in the progress of being rewritten. 
    This means that it still may contain bugs that might burn your data.

    Use at your own risk!

Manual
------

Although **rmlint** is easy to use, you might want to read these chapters first.
They show you the basic principles and most of the advanced options:

.. toctree::
   :maxdepth: 2

   install
   tutorial

Reference
---------

These chapters are informative and are not essential for the average
user. People that want to extend **rmlint** might want to read this though: 

.. toctree::
   :maxdepth: 2
       
   developers
   translators
   Online-manpage of rmlint(1) <rmlint.1>

Authors
-------

**rmlint** was and is written by: 

===================================  ============================= ===========================================
*Christopher Pahl*                   https://github.com/sahib      2010-2014
*Daniel Thomas*                      https://github.com/SeeSpotRun 2014-2014
===================================  ============================= ===========================================

Additional thanks to:

.. hlist::
    :columns: 3

    - qitta_ (Ideas & Testing)
    - dieterbe_ (Bugs & -D is his fault)
    - `My cats`_.
    - Attila Toth 
    - All sane bugreporters (there are not many)
    - All packagers, porters and patchers.


.. _qitta: https://github.com/qitta
.. _dieterbe: https://github.com/Dieterbe
.. _`My cats`: http://imgur.com/gallery/rims0yl

License
-------

**rmlint** is licensed under the terms of GPLv3_.

.. _GPLv3: http://www.gnu.org/copyleft/gpl.htm
.. _sahib: https://github.com/sahib
.. _SeeSpotRun: https://github.com/SeeSpotRun

Donations
---------

If you think rmlint saved [*]_ you some serious time and/or space, you might
consider a donation. You can donate either via *Flattr* or via *PayPal*:

.. image:: http://api.flattr.com/button/flattr-badge-large.png
   :target: http://flattr.com/thing/302682/libglyr
   :align: center 

.. raw:: html

   <br />
   <center>
       <form action="https://www.paypal.com/cgi-bin/webscr" method="post">
           <input type="hidden" name="cmd" value="_s-xclick">
           <input type="hidden" name="hosted_button_id" value="JXCXKRMS8EDVC">
           <input type="image" src="https://www.paypalobjects.com/en_US/i/btn/btn_donate_SM.gif" border="0" name="submit" alt="PayPal - The safer, easier way to pay online!">
           <img alt="" border="0" src="https://www.paypalobjects.com/de_DE/i/scr/pixel.gif" width="1" height="1">
       </form>
   </center>
   <br />

Or just buy us a beer if we ever meet. Nice emails are okay too.

.. [*] If it freed you from your beloved data: *Sorry.* [*]_
.. [*] Please file a bug or read the source and provide a patch. [*]_
.. [*] For more than 100GB of data loss we owe you one beer. [*]_
.. [*] If you don't like beer or there's only Budweiser available, you can order
   a Club Mate.
