ARG TOOLCHAIN_IMAGE=gcc-standalone:i686-elf
FROM ${TOOLCHAIN_IMAGE} AS sdk-build
WORKDIR /src
COPY BuildSupport/ BuildSupport/
COPY SDK/ SDK/
COPY CRT/ CRT/
COPY ABI/ ABI/
COPY Tools/ Tools/
RUN mkdir -p Kernel/Source/src/syscalls \
    && cmake -S SDK -B /sdk-build \
       -DCMAKE_TOOLCHAIN_FILE=/src/SDK/UnDOS-toolchain.cmake \
       -DCMAKE_BUILD_TYPE=Release -DUNDOS_SDK_PREFIX=/opt/undos-sdk \
    && cmake --build /sdk-build --target sdk --parallel 4 \
    && python3 /opt/undos-sdk/share/undos/check-sdk.py --sdk /opt/undos-sdk

FROM ${TOOLCHAIN_IMAGE}
COPY --from=sdk-build /opt/undos-sdk /opt/undos-sdk
ENV UNDOS_SDK_ROOT=/opt/undos-sdk
ENV PATH="/opt/undos-sdk/bin:${PATH}"
ENV CMAKE_TOOLCHAIN_FILE=/opt/undos-sdk/share/cmake/UnDOS/UnDOS-toolchain.cmake
WORKDIR /work
CMD ["cmake", "--help"]
