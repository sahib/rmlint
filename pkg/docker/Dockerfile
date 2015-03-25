# Rmlint
# VERSION 0.0.1
# Website: https://github.com/sahib/rmlint

FROM base/arch 
MAINTAINER Christopher Pahl <sahib@online.de>
ENV PATH /rmlint:$PATH

RUN pacman --noconfirm -Syy
RUN pacman --noconfirm -S base-devel
RUN pacman-db-upgrade
RUN chmod a+x /var/lib/pacman/  # bugfix for possible db bug.
RUN pacman --noconfirm -S git scons python-sphinx libunistring
RUN pacman --noconfirm -S glib2 libutil-linux elfutils json-glib
RUN git clone -b develop https://github.com/sahib/rmlint.git
WORKDIR rmlint
RUN scons DEBUG=1 -j4  # For releases you can omit DEBUG=1
CMD ["/bin/bash"]
