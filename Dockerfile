# Build stage
FROM ubuntu:24.04 AS builder

# Install build dependencies
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        git \
        pkg-config \
        libsodium-dev \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# Build with static libstdc++/libgcc for portability
# Disable -Werror due to GCC 13+ false positives in bundled fmt (spdlog dependency)
RUN cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DTOXTUNNEL_BUILD_TESTS=ON \
    -DTOXTUNNEL_WARNINGS_AS_ERRORS=OFF \
    -DCMAKE_EXE_LINKER_FLAGS="-static-libgcc -static-libstdc++" && \
    cmake --build build -j$(nproc)

# Run tests to verify build
RUN build/tests/unit_tests && build/tests/integration_tests

# Runtime stage - use same Ubuntu for glibc compatibility
FROM ubuntu:24.04

# Install minimal runtime dependencies
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        ca-certificates \
        libsodium23 \
    && rm -rf /var/lib/apt/lists/*

# Copy the binary
COPY --from=builder /src/build/toxtunnel /usr/local/bin/toxtunnel

# Create non-root user for security
RUN useradd -r -s /bin/false toxtunnel
USER toxtunnel

ENTRYPOINT ["toxtunnel"]
CMD ["--help"]
