      
======

`rmlint` finds space waste and other broken things on your filesystem and offers
to remove it. 

.. image:: https://raw.githubusercontent.com/sahib/rmlint/develop/docs/_static/logo.png
   :align: center

**Features:**

- Duplicate Files and duplicate directories.
- Nonstripped binaries (i.e. binaries with debug symbols)
- Broken symbolic links.
- Empty files and directories.
- Files with broken user or/and group ID.

**Differences to other duplicate finders:**

- Extremely fast (no exaggeration, we promise!).
- Many output formats.
- No interactivity.
- Search for files only newer than a certain mtime. 
- ...

Due to some of it's performance optimizations, it currently only runs on Linux
32 and 64 bit. Porting to BSD and Darwin (Mac OSX) architectures is possible
though, but needs some volunteers to test. Patches welcome!


.. image:: https://raw.githubusercontent.com/sahib/rmlint/develop/docs/_static/screenshot.png
   :align: center

INSTALLATION
------------

Chances are that you might have `rmlint` already as readily made package in your
favourite distribution. If not, you might consider 
`compiling it from source <http://rmlint.readthedocs.org/en/latest/install.html>`_.

DOCUMENTATION
-------------

Detailed documentation is available on: 

    https://rmlint.rtfd.org

Most feature you'll ever need is covered in the Tutorial:

    https://rmlint.rtfd.org/en/latest/tutorial.html

An online version of the manpage is available at:

    http://rmlint.rtfd.org/en/latest/rmlint.1.in.html

BUGS
----

If you found bugs, having trouble running `rmlint` or want to suggest new
features please `read this <http://rmlint.readthedocs.org/en/latest/developers.html>`_.
Also read the **BUGS** section of the `manpage <http://rmlint.rtfd.org/en/latest/rmlint.1.in.html>`_ 
to find out how to provide nice debug information.

AUTHORS
-------

Here's a list of developers to blame:

===================================  ============================= ===========================================
*Christopher Pahl*                   https://github.com/sahib      2010-2014
*Daniel Thomas*                      https://github.com/SeeSpotRun 2014-2014
===================================  ============================= ===========================================

There are some other people that helped us of course.
Please see the AUTHORS distributed along `rmlint`.

LICENSE
-------

`rmlint` is licensed under the conditions of the
`GPLv3 <https://www.gnu.org/licenses/quick-guide-gplv3.html.en>`_.
See the
`COPYING <https://raw.githubusercontent.com/sahib/rmlint/master/COPYING>`_ 
file distributed along the source for details.

DONATIONS
---------

If you think `rmlint` saved you some serious time [*]_ and/or space, you might
consider a donation. You can donate either via Flattr, PayPal or you buy us a
beer if we ever meet. `See here for details <http://rmlint.readthedocs.org/en/latest/index.html#donations>`_. 

.. [*] If it freed you from your beloved data: *Sorry.* [*]_
.. [*] Please file a bug or read the source and provide a patch. [*]_
.. [*] For more than 100GB of data loss we owe you one beer. [*]_
.. [*] If you don't like beer or there's only Budweiser available, you can order
   a Club Mate.
