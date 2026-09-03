FROM alpine:3 AS toolchain
RUN apk add --no-cache build-base pkgconf scons \
    git glib-dev json-glib-dev elfutils-dev

FROM toolchain AS build
WORKDIR /rmlint
COPY . .
RUN scons --without-gui --without-gettext

FROM toolchain AS build-install-gui
WORKDIR /rmlint
COPY . .
RUN apk add --no-cache py3-build py3-installer py3-setuptools gtk-update-icon-cache
RUN scons install --without-gettext DESTDIR=/staging PREFIX=/usr install
RUN gtk-update-icon-cache -t /staging/usr/share/icons/hicolor

FROM alpine:3 AS run-gui
RUN apk add --no-cache glib gtk+3.0 python3 json-glib libelf
RUN apk add --no-cache py3-colorlog py3-cairo py3-gobject3 py3-parsedatetime \
    gtksourceview4 hicolor-icon-theme
COPY --from=build-install-gui /staging /
RUN glib-compile-schemas /usr/share/glib-2.0/schemas
ENTRYPOINT ["/usr/bin/rmlint", "--gui"]

FROM build AS test
RUN apk add --no-cache py3-sphinx py3-pip bash dash mandoc
RUN scons --without-gui --without-gettext DEBUG=1
RUN pip install --break-system-packages -r tests/requirements.txt
ENTRYPOINT ["pytest", "-m", "not slow"]
CMD ["tests"]

FROM alpine:3 AS run
LABEL \
    org.opencontainers.image.title="rmlint" \
    org.opencontainers.image.source="https://github.com/sahib/rmlint" \
    org.opencontainers.image.documentation="https://rmlint.rtfd.org/" \
    org.opencontainers.image.licenses="GPL-3.0-or-later"
RUN apk add --no-cache glib json-glib libelf
COPY --from=build /rmlint/rmlint /opt
ENTRYPOINT ["/opt/rmlint"]
