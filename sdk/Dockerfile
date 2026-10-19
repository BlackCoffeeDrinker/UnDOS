# Build the prerequisite with DockerGccCross/Dockerfile, or supply an equivalent
# image containing the i686-elf GCC toolchain and CMake.
ARG TOOLCHAIN_IMAGE=gcc-standalone:i686-elf
FROM ${TOOLCHAIN_IMAGE} AS sdk-build
WORKDIR /src
COPY CMakeLists.txt ./
COPY cmake/ cmake/
COPY sdk/ sdk/
COPY tools/ tools/
COPY kernel_userland_shared/ kernel_userland_shared/
COPY userland/ userland/
RUN mkdir -p kernel/src/syscalls \
    && cmake -S . -B /sdk-build \
      -DCMAKE_TOOLCHAIN_FILE=/src/sdk/UnDOS-toolchain.cmake \
      -DUNDOS_SDK_ONLY=ON -DCMAKE_BUILD_TYPE=Release \
      -DUNDOS_SDK_PREFIX=/opt/undos-sdk \
    && cmake --build /sdk-build --target sdk --parallel 4 \
    && python3 /opt/undos-sdk/share/undos/check-sdk.py --sdk /opt/undos-sdk

FROM ${TOOLCHAIN_IMAGE}
COPY --from=sdk-build /opt/undos-sdk /opt/undos-sdk
ENV UNDOS_SDK_ROOT=/opt/undos-sdk
ENV PATH="/opt/undos-sdk/bin:${PATH}"
ENV CMAKE_TOOLCHAIN_FILE=/opt/undos-sdk/share/cmake/UnDOS/UnDOS-toolchain.cmake
WORKDIR /work
CMD ["cmake", "--help"]
