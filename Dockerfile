# Builds Win31SS (a Win16 screensaver) headlessly with OpenWatcom 2.0.
#
# OpenWatcom's official Linux installer is an interactive GUI wizard, which
# doesn't run headlessly in CI. Instead we use the "ow-snapshot.tar.xz"
# release asset: a pre-built, ready-to-use copy of the $WATCOM install tree
# (binl/binw/binnt, h, lib286, lib386, ...) that just needs extracting -
# no installer to run.
FROM debian:12-slim

ENV WATCOM=/opt/watcom
ENV PATH="${WATCOM}/binl:${PATH}"
ENV INCLUDE="${WATCOM}/h:${WATCOM}/h/win"
ENV LIB="${WATCOM}/lib286:${WATCOM}/lib286/dos"

RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates curl xz-utils make \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p "${WATCOM}" \
    && curl -fsSL -o /tmp/ow-snapshot.tar.xz \
        https://github.com/open-watcom/open-watcom-v2/releases/download/Current-build/ow-snapshot.tar.xz \
    && tar -xJf /tmp/ow-snapshot.tar.xz -C "${WATCOM}" \
    && rm /tmp/ow-snapshot.tar.xz

WORKDIR /work
COPY . /work

CMD ["make"]
