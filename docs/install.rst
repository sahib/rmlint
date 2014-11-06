Installation
============

Many major Linux distribution might already package ``rmlint`` -- but watch out for
the version. This manual describes the rewrite of ``rmlint`` (i.e. version :math:`\geq 2`).
Old versions before this might contain bugs, have design flaws or might eat your
hamster. We recommend using the newest version.

If there is no package yet or you want to try a development version, you gonna
need to compile ``rmlint`` from source.

Dependencies
------------

Hard dependencies:
~~~~~~~~~~~~~~~~~~

* **glib** :math:`\geq 2.32` (general C Utility Library)
* **libblkid** (detecting mountpoints)
* **libelf** (nonstripped binary detection)

Build dependencies:
~~~~~~~~~~~~~~~~~~~

* **git** (version control)
* **scons** (build system)
* **rst2man** (manpage generation)

Here's a list of readily prepared commands for known distributions:

* **Fedora:**

  .. code-block:: bash
  
    $ yum -y install git scons python-docutils
    $ yum -y install glib-devel libblkid-devel libelf-devel

* **ArchLinux:**

  .. code-block:: bash

    $ pacman -S git scons python-docutils
    $ pacman -S glib2 libutil-linux elfutils

Send us a note if you want to see your distribution here.

Compilation
-----------

Compilation consists of getting the source and translating it into a usable
binary:

.. code-block:: bash

   $ git clone -b develop https://github.com/sahib/rmlint.git 
   $ cd rmlint/
   $ scons DEBUG=1  # For releases you can omit DEBUG=1
   $ sudo scons install

Done!

You should be now able to see the manpage with ``rmlint -h`` or ``man 1
rmlint``.
