FROM --platform=linux/amd64 ubuntu:22.04

# The following 2 lines are added to avoid hanging the container creation. 
# See <https://grigorkh.medium.com/fix-tzdata-hangs-docker-image-build-cdb52cc3360d>
ENV TZ=Europe/Brussels
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

RUN apt-get update && apt-get install -y \
        bison \
        flex \
        gettext \
        texinfo \
        libncurses5-dev \
        locales \
        libncursesw5-dev \
        gperf \
        automake \
        libtool \
        pkg-config \
        build-essential \
        genromfs \
        libgmp-dev \
        libmpc-dev \
        libmpfr-dev \
        libisl-dev \
        binutils-dev \
        libelf-dev \
        libexpat-dev \
        gcc-multilib \
        g++-multilib \
        picocom \
        u-boot-tools \
        util-linux \
        sudo \
        xxd \
        sed \
        zip \
        tcl \
        kconfig-frontends \
        xz-utils \
        python3 \
        python3-distutils \
        python3-pip \
        python3-apt \
        clang-15 \
        git \
        curl \
        srecord || true

RUN pip3 install jinja2

# Install the Arm GNU Toolchain rather than Ubuntu's gcc-arm-none-eabi package.
#
# The apt package is pinned to whatever the distribution shipped (10.3.1 on
# 22.04), which is years behind what developers install locally.  That matters
# beyond compiler version drift: nuttx/libs/libc/elf/elf_globals.S is a
# generated, committed table of every symbol exported to dlopen'd modules, and
# it is produced by running tools/build-globals.sh against a developer's own
# build.  Any symbol in that table has to exist in whatever libm the build
# links, so a toolchain older than the one that generated the table fails the
# user-space link on newlib internals such as __ieee754_tgamma and the GNU
# fenv extensions (feenableexcept and friends).
#
# Keep ARM_TOOLCHAIN_VERSION in step with the version developers install; if
# you bump one, bump the other and regenerate elf_globals.S.
ARG ARM_TOOLCHAIN_VERSION=15.3.rel1
RUN set -eux; \
        ARM_BASE="https://gitlab.arm.com/api/v4/projects/tooling%2Fgnu-toolchains-for-arm/packages/generic/gnu-toolchain"; \
        ARM_FILE="arm-gnu-toolchain-${ARM_TOOLCHAIN_VERSION}-x86_64-arm-none-eabi.tar.xz"; \
        mkdir -p /opt/arm-gnu-toolchain; \
        curl -fsSL "${ARM_BASE}/${ARM_TOOLCHAIN_VERSION}/${ARM_FILE}" | tar -xJ -C /opt/arm-gnu-toolchain --strip-components=1; \
        /opt/arm-gnu-toolchain/bin/arm-none-eabi-gcc --version

ENV PATH="/opt/arm-gnu-toolchain/bin:${PATH}"

# CMake and ninja for the Mono runtime build (build-mono.sh).  The runtime
# needs CMake 3.26 or later, newer than Ubuntu 22.04's package.
RUN pip3 install --no-cache-dir cmake==3.31.6 ninja==1.11.1.4

RUN groupadd -g 1000 dev \
        && useradd -u 1000 -g dev -d /home/dev dev \
        && mkdir /home/dev \
        && chown -R dev:dev /home/dev

RUN if [ -f /usr/bin/clang-15 ]; then ln -s /usr/bin/clang-15 /usr/bin/clang; fi

# The following 3 lines allow the 'dev' user to run sudo (password is "dev"). 
# Useful when later on packages need to be installed that are missing.
RUN usermod -aG sudo dev
SHELL ["/bin/bash", "-o", "pipefail", "-c"]
RUN echo 'dev:dev' | chpasswd

RUN apt-get update && apt-get install -y locales && locale-gen en_US.UTF-8
ENV LANG=en_US.UTF-8
ENV LANGUAGE=en_US:en
ENV LC_ALL=en_US.UTF-8

USER dev

WORKDIR /home/dev
