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
* Try to offer an easy but powerful interface.
* Never make **rmlint** modify the filesystem itself, only produce output
  to let the user easily do it.

Keep this in mind, if you want to make a feature request.

Infrastructure
--------------

Github, Travis, Tests
