# Rmlint
# VERSION 0.0.1
# Website: https://github.com/sahib/rmlint

FROM alpine:latest
MAINTAINER Christopher Pahl <sahib@online.de>
ENV PATH /rmlint:$PATH

RUN apk add build-base python2 python3
RUN apk add git scons glib glib-dev
RUN apk add libelf elfutils-dev
RUN apk add sqlite-libs json-glib-dev

RUN git clone -b develop https://github.com/sahib/rmlint.git
WORKDIR rmlint
RUN scons config
RUN scons DEBUG=1 -j4  # For releases you can omit DEBUG=1

# Install test dependencies:
RUN apk add python3-dev py3-pip
RUN pip install wheel
RUN pip3 install -r test-requirements.txt
CMD ["/bin/sh"]
