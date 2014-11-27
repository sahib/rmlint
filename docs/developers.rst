Developer's Guide
=================

This guide is targeted to people that want to write new features or fix bugs in rmlint.

Bugs
----

Please use the issue tracker to post and discuss bugs and features:

    https://github.com/sahib/rmlint/issues

Philosophy
----------

We try to adhere to some principles when adding features:

* Try to stay compatible to standard unix' tools and ideas.
* Try to stay out of the users way and never be interactive.
* Try to make scripting as easy as possible.
* Never make **rmlint** modify the filesystem itself, only produce output
  to let the user easily do it.

Also keep this in mind, if you want to make a feature request.

Making contributions
--------------------

The code is hosted on GitHub, therefore our preferred way of receiving patches
is using GitHub's pull requests (normal git pull requests are okay too of course). 

Here's a short step-by-step:

1. `Fork it`_.
2. Create a branch from develop. (``git checkout develop && git checkout -b my_feature``)
3. Commit your changes. (``git commit -am "Fixed it all."``)
4. Check if your commit message is good. (If not: ``git commit --amend``)
5. Push to the branch (``git push origin my_feature``)
6. Open a `Pull Request`_.
7. Enjoy a refreshing ClubMate and wait.

.. _`Fork it`: https://github.com/sahib/rmlint
.. _`Pull Request`: http://github.com/studentkittens/moosecat/pulls

Here are some other things to check before submitting your contribution:

- Does your code look alien to the other code? Is the style the same?
- Do all tests run? (Simply run ``nosetests`` to find out)
  Also after opening the pull request, your code will be checked via `TravisCI`_.
- Is your commit message descriptive?

.. _`TravisCI`: https://travis-ci.org/sahib/rmlint

For language-translations/updates it is also okay to send the ``.po`` files via
mail at sahib@online.de, since not every translator is necessarily a
software developer.

Buildsystem Helpers
-------------------

Environement Variables
~~~~~~~~~~~~~~~~~~~~~~

:CFLAGS:

    Extra flags passed to the compiler.

:LDFLAGS:

    Extra flags passed to the linker.

:CC:

    Which compiler to use? 

.. code-block:: bash

   # Use clang and enable profiling, verbose build and enable debugging
   CC=clang CFLAGS='-pg' LDFLAGS='-pg' scons VERBOSE=1 DEBUG=1

Variables
~~~~~~~~~

:DEBUG:

    Enable debugging symbols for ``rmlint``. This should always be enabled during
    developement. Backtraces wouldn't be useful elsewhise.

:VERBOSE:

    Print the exact compiler and linker commands. Useful for troubleshooting
    build errors.

Arguments
~~~~~~~~~

:--prefix:

    Change the installation prefix. By default this is ``/usr``, but some users
    might prefer ``/usr/local`` or ``/opt``. 

Notable targets
~~~~~~~~~~~~~~~

:install:

    Install all program parts system-wide.

:man:

    Build the manpage.

:docs:

    Build the onlice html docs (which you are reading now).

:test:

    Build the tests (requires ``python`` and ``nosetest`` installed).

:xgettext:

    Extract a gettext ``.pot`` template from the source.

Sourcecode layout
-----------------

- All C-source lives in ``src``, the file names should be self explanatory.
- All documentation is inside ``docs``. 
- All translation stuff should go to ``po``.
- All packaging should be done in ``pkg/<distribution>``.
- Tests are written in Python and live in ``tests``.


Hashfunctions
-------------

Here is a short comparasion of the existing hashfunctions_ in ``rmlint`` (linear_ scale).
For reference: Those plots were rendered with these_ sources - which are very ugly, sorry.

If you want to add new hashfunctions, you should have some arguments why it is valueable and possiblye
even benchmark it with the above scripts to see if it's really that much faster.

Also keep in mind that most of the time the hashfunction is not the bottleneck.

.. _these: https://github.com/sahib/rmlint/tree/gh-pages/plots
.. _linear: https://raw.githubusercontent.com/sahib/rmlint/gh-pages/plots/hash_comparasion_lin.png
.. _hashfunctions: https://raw.githubusercontent.com/sahib/rmlint/gh-pages/plots/hash_comparasion_log.png
