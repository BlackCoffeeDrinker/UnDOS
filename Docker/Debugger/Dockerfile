ARG TOOLCHAIN_IMAGE=gcc-standalone:i686-elf
FROM ${TOOLCHAIN_IMAGE} AS gdb-build

ARG GDB_VERSION=16.3
ARG BUILD_JOBS=4

RUN apk --no-cache add \
    bison flex texinfo xz \
    gmp-dev mpfr-dev \
    ncurses-dev expat-dev zlib-dev python3-dev

WORKDIR /build/gdb

RUN curl --fail --show-error --location \
        --output /build/gdb.tar.xz \
        "https://ftp.gnu.org/gnu/gdb/gdb-${GDB_VERSION}.tar.xz" \
    && mkdir /build/gdb-source \
    && tar -xJf /build/gdb.tar.xz \
        -C /build/gdb-source --strip-components=1 \
    && /build/gdb-source/configure \
        --target=i686-elf \
        --prefix=/usr/local/cross \
        --with-python=/usr/bin/python3 \
        --with-system-readline=no \
        --with-debuginfod=no \
        --disable-nls \
        --disable-werror \
    && make -j"${BUILD_JOBS}" all-gdb \
    && make DESTDIR=/gdb-install install-gdb \
    && rm -rf /build/gdb-source /build/gdb /build/gdb.tar.xz

FROM ${TOOLCHAIN_IMAGE}
RUN apk --no-cache add \
    gmp mpfr ncurses-libs expat zlib python3 libgcc libstdc++

COPY --from=gdb-build /gdb-install/usr/local/cross/ /usr/local/cross/

RUN i686-elf-gdb --batch \
    -ex "show configuration" \
    -ex "python import sys; print(sys.version)"

WORKDIR /src
ENTRYPOINT ["i686-elf-gdb"]
CMD ["--help"]
