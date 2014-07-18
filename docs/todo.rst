ToDo List
=========

Stable Version
--------------

- Add a test target to the build system.
- Write a tutorial.
- Tickle packagers.
- Integrate 'ack TODO | wc -l' to 0.
- Try "man 3 readv" instead of allocation more buffers.
- long psize = sysconf(_SC_PAGESIZE); // posix compatible page size
- for lint types, instead of -kK -lL etc, we could have --types [enz...]
  (SeeSpotRun)
- Integrate the filemap.c optimization.
- Integrate new Scheduler (sahib?) and make use of mounttables.c
- Make digest update iteratively (can be done before scheduler changes) - SeeSpotRun?
- Implement // or -- for orig-path parsing.

Further Developement
--------------------

- Multithreaded directory tree traversal? (SeeSpotRun) -- mostly done?
- Some of https://github.com/sahib/rmlint/issues


Done items
----------

- Add Daniel to AUTHORS and file headers.
- add additional checks in find_path_doubles
- Put rmlint on TravisCI.

Documentation Items
-------------------

.. todolist::

.. todo:: Write the tutorial.
