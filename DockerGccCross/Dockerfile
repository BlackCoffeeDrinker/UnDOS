FROM alpine AS build

RUN apk --no-cache add curl curl-dev bison flex build-base gcc g++ linux-headers \
    musl-dev make autoconf automake nasm tar gmp-dev mpc1-dev mpfr-dev zlib-dev && \
    rm -rf /var/cache/apk/*

ENV TARGET=i686-elf
ENV PREFIX=/usr/local/cross
ENV PATH="${PREFIX}/bin:/usr/local/bin:${PATH}"

ENV GCC=15.2.0
ENV BINUTILS=2.45
ENV CMAKE=4.0.5
ENV NASM=2.16.03

WORKDIR /src
RUN curl -fsSL https://ftp.gnu.org/gnu/binutils/binutils-${BINUTILS}.tar.gz                         | tar xvzf - -C /src && \
    curl -fsSL https://ftp.gnu.org/gnu/gcc/gcc-${GCC}/gcc-${GCC}.tar.gz                             | tar xvzf - -C /src && \
    curl -fsSL https://github.com/Kitware/CMake/releases/download/v${CMAKE}/cmake-${CMAKE}.tar.gz   | tar xvzf - -C /src && \
    curl -fsSL https://www.nasm.us/pub/nasm/releasebuilds/${NASM}/nasm-${NASM}.tar.gz               | tar xvzf - -C /src

#############################
## NASM
#############################
RUN cd /src/nasm-${NASM}/ && \
    sh ./autogen.sh && \
    ./configure --prefix=$PREFIX && \
    make && \
    make install

#############################
## Binutils
#############################
WORKDIR /build/binutils
RUN /src/binutils-${BINUTILS}/configure --target=$TARGET --prefix="$PREFIX" \
    --with-sysroot --disable-nls --disable-werror && \
    make && \
    make install

#############################
## GCC
#############################
WORKDIR /build/gcc
RUN  /src/gcc-${GCC}/configure --target=$TARGET --prefix="$PREFIX" --disable-nls \
     --enable-languages=c,c++ --without-headers --without-isl --disable-threads \
     --disable-shared --disable-libssp --disable-libstdcxx-pch --disable-libgomp \
     --disable-werror --disable-hosted-libstdcxx --disable-__cxa_atexit  \
     --disable-hosted-libstdcxx --disable-libquadmath && \
     make -j4 all-gcc && \
     make -j4 all-target-libgcc && \
     make install-gcc && \
     make install-target-libgcc

# We can't do
# make all-target && make install-target
# as libstdc++ needs to be built freestanding, so do it manually
WORKDIR /build/libstdcpp
RUN /src/gcc-${GCC}/libstdc++-v3/configure --target=$TARGET --prefix="$PREFIX" --disable-multilib \
    --disable-nls --disable-libstdcxx-threads --disable-libstdcxx-pch --disable-hosted-libstdcxx && \
    make -j4 && \
    make install
    
# Sanity check
RUN which -- $TARGET-as || echo "$TARGET-as is not in the PATH"

#############################
## CMake
#############################
WORKDIR /build/cmake
RUN CC=gcc CXX=g++ /src/cmake-${CMAKE}/bootstrap --system-curl && \
    make -j8 && \
    make install


RUN rm -rf /src && \
    rm -rf /build

# Sanity check
RUN $TARGET-gcc --version

FROM alpine

#############################
## Final setup
#############################
WORKDIR /code

RUN apk --no-cache add curl build-base make nasm ninja-build cmake grub xorriso && \
    rm -rf /var/cache/apk/* 

ENV PATH="/usr/local/cross/bin:/usr/local/bin:${PATH}"

COPY --from=build /usr/local /usr/local

