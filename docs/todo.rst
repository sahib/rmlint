ToDo List
=========

Stable Version
--------------

- Add a test target to the build system.
- Put rmlint on TravisCI.
- Write a tutorial.
- Tickle packagers.
- Integrate 'ack TODO | wc -l' to 0.
- Try "man 3 readv" instead of allocation more buffers.
- cat /sys/block/sda/queue/rotational # find out if dev is sdd 
  Find device name from dev_t via statvfs()?
- long psize = sysconf(_SC_PAGESIZE); // posix compatible page size
- for lint types, instead of -kK -lL etc, we could have --types [enz...]
  (SeeSpotRun)

Further Developement
--------------------

- Multithreaded directory tree traversal? (SeeSpotRun) -- mostly done?
- Some of https://github.com/sahib/rmlint/issues


Done items
----------

- Add Daniel to AUTHORS and file headers.
- add additional checks in find_path_doubles

Documentation Items
-------------------

.. todolist::

.. todo:: Write the tutorial.


