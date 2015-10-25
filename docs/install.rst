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

Soft dependencies:
~~~~~~~~~~~~~~~~~~

* **libblkid** (detecting mountpoints)
* **libelf** (nonstripped binary detection)
* **libjson-glib** (parsing rmlint's own json as caching layer)

Build dependencies:
~~~~~~~~~~~~~~~~~~~

* **git** (version control)
* **scons** (build system)
* **sphinx** (manpage/documentation generation)
* **gettext** (support for localization)

Here's a list of readily prepared commands for known distributions:

* **Fedora:**

  .. code-block:: bash
  
    $ yum -y install git scons python3-sphinx gettext json-glib-devel
    $ yum -y install glib2-devel libblkid-devel elfutils-libelf-devel

  There are also pre-built packages on `Fedora Copr`_:

  .. code-block:: bash

    $ dnf copr enable sahib/rmlint
    $ dnf install rmlint

  Those packages are built from master snapshots and might be slightly outdated.

.. _`Fedora Copr`: https://copr.fedoraproject.org/coprs/sahib/rmlint/

* **ArchLinux:**

  There is an official package in ``[community]`` here_:

  .. code-block:: bash

    $ pacman -S rmlint

  Alternatively you can use ``rmlint-git`` in the AUR: 

  .. code-block:: bash

    $ pacman -S git scons python-sphinx
    $ pacman -S glib2 libutil-linux elfutils json-glib

  There is also a `PKGBUILD`_ on the `ArchLinux AUR`_:

  .. code-block:: bash

    $ # Use your favourite AUR Helper.
    $ yaourt -S rmlint-git


  It is built from git master.

.. _here: https://www.archlinux.org/packages/?name=rmlint
.. _`PKGBUILD`: https://aur.archlinux.org/packages/rm/rmlint-git/PKGBUILD
.. _`ArchLinux AUR`: https://aur.archlinux.org/packages/rmlint-git

* **Ubuntu:**

  .. code-block:: bash

    $ apt-get install git scons python3-sphinx python3-nose gettext build-essential
    $ apt-get installlibelf-dev libglib2.0-dev libblkid-dev libjson-glib-1.0 libjson-glib-dev


* **FreeBSD:**

  .. code-block:: bash

    $ pkg install git scons py27-sphinx
    $ pkg install glib gettext libelf json-glib

  Also ``rmlint`` is maintained as port:

  .. code-block:: bash

    $ cd /usr/ports/sysutils/rmlint && make install

Send us a note if you want to see your distribution here.
The commands above install the full dependencies, therefore
some packages might be stripped if you do not need the feature
they enable. Only hard requirement is ``glib``.

Compilation
-----------

Compilation consists of getting the source and translating it into a usable
binary:

.. code-block:: bash

   $ # Omit -b develop if you want to build from the stable master
   $ git clone -b develop https://github.com/sahib/rmlint.git 
   $ cd rmlint/
   $ scons config       # Look what features scons would compile
   $ scons DEBUG=1 -j4  # For releases you can omit DEBUG=1
   $ sudo scons DEBUG=1 -j4 --prefix=/usr install

Done!

You should be now able to see the manpage with ``rmlint --help`` or ``man 1
rmlint``.
