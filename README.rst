RMLINT
======

`rmlint` finds space waste and other broken things on your filesystem and offers
to remove it. 

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

INSTALLATION
------------

Chances are that you might have `rmlint` already as readily made package in your
favourite distribution. If not, you might consider 
`compiling it from source<http://rmlint.readthedocs.org/en/latest/install.html>`_.

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
features please `read this<http://rmlint.readthedocs.org/en/latest/developers.html>`_.

AUTHORS
-------

Here's a list of developers to blame:

===================================  ============================= ===========================================
*Christopher Pahl*                   https://github.com/sahib      2010--2014,
*Daniel Thomas*                      https://github.com/SeeSpotRun 2014--2014
===================================  ============================= ===========================================

There are some other people that helped us of course.
Please see the AUTHORS distributed along `rmlint`.

LICENSE
-------

`rmlint` is licensed under the conditions of the
`GPLv3<https://www.gnu.org/licenses/quick-guide-gplv3.html.en>`_.
See the
`COPYING<https://raw.githubusercontent.com/sahib/rmlint/master/COPYING>`_ 
file distributed along the source for details.

DONATIONS
---------

If you think `rmlint` saved you some serious time [*]_ and/or space, you might
consider a donation. 

* Either via *Flattr*:

.. raw:: html

   <a class="FlattrButton" style="display:none;" rev="flattr;button:compact;" href="https://github.com/sahib/glyr">
   </a>
   <a href="http://flattr.com/thing/302682/libglyr" target="_blank">
      <img src="http://api.flattr.com/button/flattr-badge-large.png" alt="Flattr this" title="Flattr this" border="0" />
   </a>

* Or alternatively via *PayPal*:

.. raw:: html

   <form action="https://www.paypal.com/cgi-bin/webscr" method="post">
       <input type="hidden" name="cmd" value="_s-xclick">
       <input type="hidden" name="hosted_button_id" value="JXCXKRMS8EDVC">
       <input type="image" src="https://www.paypalobjects.com/en_US/i/btn/btn_donate_SM.gif" border="0" name="submit" alt="PayPal - The safer, easier way to pay online!">
       <img alt="" border="0" src="https://www.paypalobjects.com/de_DE/i/scr/pixel.gif" width="1" height="1">
   </form>

* Or just buy us a beer if we ever meet. Nice emails are okay too.

.. [*] If it saved you from your beloved data: Sorry.
