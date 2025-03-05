Installation
============

Many major Linux distribution might already package ``rmlint`` -- but watch out for
the version. If possible, we recommend using the `newest version`_ available.

.. _`newest version`: https://github.com/sahib/rmlint/releases

If there is no package yet or you want to try a development version, you gonna
need to compile ``rmlint`` from source.

Dependencies
------------

Hard dependencies:
~~~~~~~~~~~~~~~~~~

* **glib** :math:`\geq 2.32` (general C Utility Library)
* **libjson-glib** (parsing rmlint's own json as caching layer)

Soft dependencies:
~~~~~~~~~~~~~~~~~~

* **libblkid** (detecting mountpoints)
* **libelf** (nonstripped binary detection)

Build dependencies:
~~~~~~~~~~~~~~~~~~~

* **git** (version control)
* **scons** (build system)
* **sphinx** (manpage/documentation generation)
* **gettext** (support for localization)

Here's a list of readily prepared commands for known operating systems:

* **Fedora** :math:`\geq 21`:

  .. code-block:: bash

    $ yum -y install pkgconf git scons python3-sphinx gettext json-glib-devel
    $ yum -y install glib2-devel libblkid-devel elfutils-libelf-devel
    # Optional dependencies for the GUI:
    $ yum -y install pygobject3 gtk3 librsvg2

  There are also pre-built packages on `Fedora Copr`_:

  .. code-block:: bash

    $ dnf copr enable eclipseo/rmlint
    $ dnf install rmlint

  Since **Fedora 29** we also have an `official package`_.

.. _`official package`: https://bugzilla.redhat.com/show_bug.cgi?id=1655338

.. _`Fedora Copr`: https://copr.fedorainfracloud.org/coprs/eclipseo/rmlint/

* **ArchLinux:**

  There is an official package in ``[community]`` here_:

  .. code-block:: bash

    $ pacman -S rmlint

  Alternatively you can use ``rmlint-git`` in the AUR: 

  .. code-block:: bash

    $ pacman -S pkgconf git scons python-sphinx base-devel
    $ pacman -S glib2 libutil-linux elfutils json-glib
    # Optional dependencies for the GUI:
    $ pacman -S gtk3 python-gobject librsvg python-cairo gtksourceview3

  There is also a `PKGBUILD`_ on the `ArchLinux AUR`_:

  .. code-block:: bash

    $ # Use your favourite AUR Helper.
    $ yaourt -S rmlint-git

  It is built from git ``master``, not from the ``develop`` branch.

.. _here: https://www.archlinux.org/packages/?name=rmlint
.. _`PKGBUILD`: https://aur.archlinux.org/packages/rm/rmlint-git/PKGBUILD
.. _`ArchLinux AUR`: https://aur.archlinux.org/packages/rmlint-git

* **Debian** / **Ubuntu** :math:`\geq 12.04`:

  Note: Debian also `ships an official package`_.
  Use the below instructions if you need a more recent version.

  This most likely applies to most distributions that are derived from Ubuntu.
  Note that the ``GUI`` depends on ``GTK+ >= 3.12``! 
  Ubuntu 14.04 LTS and earlier `still ships`_  with ``3.10``.

  .. code-block:: bash

    $ apt-get install pkg-config git scons python3-sphinx python3-pytest gettext build-essential
    # Optional dependencies for more features:
    $ apt-get install libelf-dev libglib2.0-dev libblkid-dev libjson-glib-1.0 libjson-glib-dev
    # Optional dependencies for the GUI:
    $ apt-get install python3-gi gir1.2-rsvg gir1.2-gtk-3.0 python-cairo gir1.2-polkit-1.0 gir1.2-gtksource-3.0 

 
.. _`ships an official package`: https://packages.debian.org/de/sid/rmlint
.. _`still ships`: https://github.com/sahib/rmlint/issues/171#issuecomment-199070974

* **macOS**

  ``rmlint`` can be installed via `homebrew`_. Note that the ``shredder`` graphical interface is *not* included in this. Please see `Issue #253`_ for details and updates.

  Prerequisite: If homebrew has not already been installed on the system, execute:

  .. code-block:: bash

      $ /usr/bin/ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)".

  With homebrew installed, execute:

  .. code-block:: bash

      $ brew install rmlint


  See also this `issue`_ for more information on the homebrew formula.

.. _`Issue #253`: https://github.com/sahib/rmlint/issues/253
.. _homebrew: http://brew.sh
.. _issue: https://github.com/sahib/rmlint/issues/175#issuecomment-253186769

* **FreeBSD** :math:`\geq 10.1`:

  .. code-block:: bash

    $ pkg install git scons-py37 py37-sphinx pkgconf
    $ pkg install glib gettext libelf json-glib

-----

Send us a note if you want to see your distribution here or the instructions
need an update. The commands above install the full dependencies, therefore
some packages might be stripped if you do not need the feature
they enable. Only hard requirement for the commandline is ``glib``.

Also be aware that the GUI needs at least :math:`gtk \geq 3.12` to work!

Compilation
-----------

Compilation consists of getting the source and translating it into a usable
binary. We use the build system ``scons``. Note that the following instructions
build the software from the potentially unstable ``develop`` branch: 

.. code-block:: bash

   $ # Omit -b develop if you want to build from the stable master
   $ git clone -b develop https://github.com/sahib/rmlint.git 
   $ cd rmlint/
   $ scons config       # Look what features scons would compile
   $ scons DEBUG=1      # Optional, build locally.
   # Install (and build if necessary). For releases you can omit DEBUG=1
   $ sudo scons DEBUG=1 --prefix=/usr install

Done!

You should be now able to see the manpage with ``rmlint --help`` or ``man 1
rmlint``.

Uninstall with ``sudo scons uninstall`` and clean with ``scons -c``.

You can also only type the ``install`` command above. The buildsystem is clever
enough to figure out which targets need to be built beforehand.

Troubleshooting
---------------

On some distributions (especially Debian derived) ``rmlint --gui`` might fail
with ``/usr/bin/python3: No module named shredder`` (or similar). This is due 
some incompatible changes on Debian's side.

See `this thread`_ for a workaround using ``PYTHONPATH``.


.. _`this thread`: https://github.com/sahib/rmlint/issues/171#issuecomment-199070974
