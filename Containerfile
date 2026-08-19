FROM alpine:3 AS toolchain
RUN apk add --no-cache build-base pkgconf scons \
    git glib-dev json-glib-dev elfutils-dev

FROM toolchain AS build
WORKDIR /rmlint
COPY . .
RUN scons --without-gui --without-gettext

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
    org.opencontainers.image.documentation="https://rmlint.readthedocs.io/" \
    org.opencontainers.image.licenses="GPL-3.0-or-later"
RUN apk add --no-cache glib json-glib libelf
COPY --from=build /rmlint/rmlint /opt
ENTRYPOINT ["/opt/rmlint"]
