      
======

.. image:: https://raw.githubusercontent.com/sahib/rmlint/develop/docs/_static/logo.png
  :align: center

`rmlint` finds space waste and other broken things on your filesystem and
offers to remove it.

.. image:: https://readthedocs.org/projects/rmlint/badge/?version=latest
 :target: http://rmlint.rtfd.org

.. image:: https://img.shields.io/github/issues/sahib/rmlint.svg?style=flat
 :target: https://github.com/sahib/rmlint/issues

.. image:: https://img.shields.io/github/release/sahib/rmlint.svg?style=flat
 :target: https://github.com/sahib/rmlint/releases

.. image:: https://img.shields.io/github/downloads/sahib/rmlint/latest/total.svg
 :target: https://github.com/sahib/rmlint/releases/latest

.. image:: http://img.shields.io/badge/license-GPLv3-4AC51C.svg?style=flat
 :target: https://www.gnu.org/licenses/quick-guide-gplv3.html.en

.. image:: https://badges.gitter.im/rmlint/community.svg
 :target: https://gitter.im/rmlint/community?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge

**Features:**

Finds…

- …Duplicate Files and duplicate directories.
- …Nonstripped binaries (i.e. binaries with debug symbols)
- …Broken symbolic links.
- …Empty files and directories.
- …Files with broken user or/and group ID.

**Differences to other duplicate finders:**

- Extremely fast (no exaggeration, we promise!)
- Paranoia mode for those who do not trust hashsums.
- Many output formats.
- No interactivity.
- Search for files only newer than a certain ``mtime``.
- Many ways to handle duplicates.
- Caching and replaying.
- `BTRFS` support.
- ...

It runs and compiles under most Unices, including Linux, FreeBSD and Darwin.
The main target is Linux though, some optimisations might not be available
elsewhere.

.. image:: https://raw.githubusercontent.com/sahib/rmlint/develop/docs/_static/screenshot.png
   :align: center


INSTALLATION
------------

Chances are that you might have `rmlint` already as readily made package in
your favourite distribution. If not, you might consider
`compiling it from source <http://rmlint.readthedocs.org/en/latest/install.html>`_.


.. image:: https://repology.org/badge/vertical-allrepos/rmlint.svg?columns=5
 :target: https://repology.org/project/rmlint/versions
  :align: center

DOCUMENTATION
-------------

Detailed documentation is available on: 

    http://rmlint.rtfd.org

Most features you'll ever need are covered in the tutorial:

    http://rmlint.rtfd.org/en/latest/tutorial.html

An online version of the manpage is available at:

    http://rmlint.rtfd.org/en/latest/rmlint.1.html

BUGS
----

If you found bugs, having trouble running `rmlint` or want to suggest new
features please `read this <http://rmlint.readthedocs.org/en/latest/developers.html>`_.

Also read the `BUGS <http://rmlint.readthedocs.org/en/latest/rmlint.1.html#bugs>`_ section of the `manpage <http://rmlint.rtfd.org/en/latest/rmlint.1.html>`_
to find out how to provide good debug information.

AUTHORS
-------

Here's a list of developers to blame:

===================================  ============================= ===========================================
*Christopher Pahl*                   https://github.com/sahib      2010-2017
*Daniel Thomas*                      https://github.com/SeeSpotRun 2014-2021
*Cebtenzzre*                         https://github.com/Cebtenzzre 2021-2023
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
