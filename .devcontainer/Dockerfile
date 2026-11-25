# Build your own std::vector - reference environment (Tier 1).
#
# Toolchain (C++23 baseline, verified compilers):
#   GCC 16.2.0   from the Docker official image gcc:16.2 (Debian 13 "trixie"),
#                installed in /usr/local; this is the default `g++`/`gcc`.
#   Clang 21     from apt.llvm.org (llvm-toolchain-trixie-21): clang++-21,
#                compiler-rt sanitizers, lldb-21, clangd-21.
# Plus CMake (Debian, >= 3.25 needed), Ninja, ccache, gdb, git.
#
# Standard library: BOTH compilers use GCC 16.2's libstdc++. clang++-21 is
# pointed at the GCC 16.2 install by a clang config file (see below), because
# otherwise it would pick Debian's older system libstdc++ (GCC 14), so the two
# compilers would not see the same C++23 library.
#
# Pinned to the gcc:16.2 tag. Follow-up: also pin by digest
# (FROM gcc:16.2@sha256:<digest>, `docker buildx imagetools inspect gcc:16.2`;
# use the multi-arch index digest so amd64 and arm64 both work).
FROM gcc:16.2

ARG DEBIAN_FRONTEND=noninteractive
ARG LLVM_VERSION=21
# Fingerprint of the apt.llvm.org archive signing key (documented at
# https://apt.llvm.org). The build fails if the downloaded key differs.
ARG LLVM_FPR=6084F3CF814B57C1CF12EFD515CF4D18AF4F7421
ARG USERNAME=vscode
ARG USER_UID=1000
ARG USER_GID=1000

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates ccache cmake curl gdb git gnupg jq ninja-build openssh-client sudo \
 && install -d -m 0755 /etc/apt/keyrings \
 && curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key -o /tmp/llvm.asc \
 && gpg --show-keys --with-colons /tmp/llvm.asc | awk -F: '/^fpr/ {print $10}' | grep -qx "${LLVM_FPR}" \
 && gpg --dearmor -o /etc/apt/keyrings/apt.llvm.org.gpg /tmp/llvm.asc \
 && rm /tmp/llvm.asc \
 && echo "deb [signed-by=/etc/apt/keyrings/apt.llvm.org.gpg] https://apt.llvm.org/trixie/ llvm-toolchain-trixie-${LLVM_VERSION} main" \
      > /etc/apt/sources.list.d/llvm.list \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
      clang-${LLVM_VERSION} clangd-${LLVM_VERSION} lldb-${LLVM_VERSION} \
      libclang-rt-${LLVM_VERSION}-dev \
 && rm -rf /var/lib/apt/lists/*

# Make clang++-21 use GCC 16.2's libstdc++ and libgcc (installed in /usr/local).
# Clang reads clang.cfg / clang++.cfg from the directory of the real binary.
RUN GCC_DIR="$(dirname "$(g++ -print-libgcc-file-name)")" \
 && for n in clang clang++; do \
      echo "--gcc-install-dir=${GCC_DIR}" > "/usr/lib/llvm-${LLVM_VERSION}/bin/$n.cfg"; \
    done \
 && update-alternatives --install /usr/bin/clangd clangd /usr/bin/clangd-${LLVM_VERSION} 100 \
 && update-alternatives --install /usr/bin/lldb lldb /usr/bin/lldb-${LLVM_VERSION} 100

# Non-root user.
RUN groupadd --gid ${USER_GID} ${USERNAME} \
 && useradd --uid ${USER_UID} --gid ${USER_GID} -m -s /bin/bash ${USERNAME} \
 && echo "${USERNAME} ALL=(root) NOPASSWD:ALL" > /etc/sudoers.d/${USERNAME} \
 && chmod 0440 /etc/sudoers.d/${USERNAME}

# Default compiler for ./cppatlas: GCC 16.2 (override with `CXX=clang++-21 ./cppatlas ...`).
ENV CXX=g++ \
    CC=gcc \
    CCACHE_DIR=/home/${USERNAME}/.cache/ccache

USER ${USERNAME}
WORKDIR /workspaces
