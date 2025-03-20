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

* **glib** :math:`\geq 2.64` (general C Utility Library)

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

Here's a list of readily prepared commands for known operating systems:

* **Fedora**

  .. code-block:: bash

    $ sudo dnf install git pkgconf gcc gettext scons glib2-devel
    # Optional dependencies for more features:
    $ sudo dnf install libblkid-devel elfutils-libelf-devel json-glib-devel
    # Optional dependencies for building documentation:
    $ sudo dnf install python3-sphinx
    # Optional dependencies for the GUI:
    $ sudo dnf install python3-devel python3-setuptools gtksourceview4 gtk3 librsvg2 hicolor-icon-theme

  There are also pre-built packages on `Fedora Copr`_:

  .. code-block:: bash

    $ sudo dnf copr enable rodoma92/rmlint
    $ sudo dnf install rmlint

  Since **Fedora 29** we also have an `official package`_.

.. _official package: https://src.fedoraproject.org/rpms/rmlint

.. _Fedora Copr: https://copr.fedorainfracloud.org/coprs/rodoma92/rmlint/

* **ArchLinux**

  There is/used to be an official package in ``[extra]`` here_:

  .. code-block:: bash

    $ pacman -S rmlint

  Alternatively you can use ``rmlint-git`` in the AUR: 

  .. code-block:: bash

    $ sudo pacman -S pkgconf git scons glib2 gettext
    # Optional dependencies for more features:
    $ sudo pacman -S util-linux-libs libelf json-glib
    # Optional dependencies for building documentation:
    $ sudo pacman -S python-sphinx python-sphinx-bootstrap-theme
    # Optional dependencies for the GUI:
    $ sudo pacman -S python-setuptools python-gobject python-cairo gtksourceview4 librsvg
    # Optional dependancies for tests:
    $ sudo pacman -S python-pytest python-xattr python-psutil btrfs-progs

  There is also git packages in AUR, from the ``master`` branch: `rmlint-git`_, `rmlint-shredder-git`_ ; and the ``develop`` branch: `rmlint-develop-git`_, `rmlint-shredder-develop-git`_.

  .. code-block:: bash

    # Use your favourite AUR Helper.
    $ yaourt/yay/paru -S rmlint-git

.. _here: https://gitlab.archlinux.org/archlinux/packaging/packages/rmlint
.. _rmlint-git: https://aur.archlinux.org/packages/rmlint-git
.. _rmlint-shredder-git: https://aur.archlinux.org/packages/rmlint-shredder-git
.. _rmlint-develop-git: https://aur.archlinux.org/packages/rmlint-develop-git
.. _rmlint-shredder-develop-git: https://aur.archlinux.org/packages/rmlint-shredder-develop-git

* **Debian** / **Ubuntu** / **Raspberry Pi OS**

  Note: `Debian`_ and `Ubuntu`_ ships official packages.
  Use the below instructions if you need a more recent version.

  .. code-block:: bash

    # Ubuntu-only:
    $ sudo apt install software-properties-common && add-apt-repository universe
    $ sudo apt install git scons pkgconf gettext build-essential
    # Optional dependencies for more features:
    $ sudo apt install libelf-dev libglib2.0-dev libblkid-dev libjson-glib-1.0-0 libjson-glib-dev
    # Optional dependencies for building documentation:
    $ sudo apt install python3-sphinx python3-sphinx-bootstrap-theme
    # Optional dependencies for the GUI:
    $ sudo apt install python3-setuptools python3-gi-cairo gir1.2-gtksource-4 gir1.2-polkit-1.0 gir1.2-rsvg-2.0 python3-colorlog
    # Optional dependancies for tests:
    $ sudo apt install python3-pytest python3-psutil python3-xattr

.. _Debian: https://packages.debian.org/rmlint
.. _Ubuntu: https://packages.ubuntu.com/rmlint

* **macOS**

  ``rmlint`` can be installed via `homebrew`_:

  Prerequisite: If homebrew has not already been installed on the system, execute:

  .. code-block:: bash

    $ /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

  With homebrew installed, execute:

  .. code-block:: bash

    $ brew install rmlint

  `MacPorts`_ and `PowerPC ports`_ also provide ports.

.. _homebrew: http://brew.sh
.. _MacPorts: https://github.com/macports/macports-ports/tree/master/sysutils/rmlint
.. _PowerPC ports: https://github.com/barracuda156/powerpc-ports/tree/master/sysutils/rmlint

* **FreeBSD**

  `FreeBSD`_ and `DragonFlyBSD`_ both have official ports.

  .. code-block:: bash

    $ doas pkg install git scons-py311 pkgconf glib gettext
    # Optional dependencies for more features:
    $ doas pkg install json-glib libelf
    # Optional dependencies for building documentation:
    $ doas pkg install py311-sphinx py311-pydata-sphinx-theme gtksourceview4

.. _FreeBSD: https://cgit.freebsd.org/ports/tree/sysutils/rmlint
.. _DragonFlyBSD: https://github.com/DragonFlyBSD/DPorts/tree/master/sysutils/rmlint

* **Others**

  `GNU Guix`_, `Alpine`_, `Gentoo`_, `NixOS`_, `Void`_, `Slackware`_ and `Solus`_ also provide or used to provide ports or packages.

.. _GNU Guix: https://git.savannah.gnu.org/cgit/guix.git/tree/gnu/packages/disk.scm#n1288
.. _Alpine: https://gitlab.alpinelinux.org/alpine/aports/-/tree/master/testing/rmlint
.. _Gentoo: https://gitweb.gentoo.org/repo/gentoo.git/tree/app-misc/rmlint?id=392900cef25d31d5c622e542d636ba37e7a0b71a
.. _NixOS: https://github.com/NixOS/nixpkgs/tree/master/pkgs/tools/misc/rmlint
.. _Void: https://github.com/void-linux/void-packages/tree/master/srcpkgs/rmlint
.. _Slackware: https://git.slackbuilds.org/slackbuilds/plain/misc/rmlint/
.. _Solus: https://github.com/getsolus/packages/tree/main/packages/r/rmlint

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
   $ sudo scons DEBUG=1 --prefix=/usr/local install

Done!

See Developer’s Guide for configuration options.

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
