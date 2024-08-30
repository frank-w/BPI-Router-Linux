#FROM ubuntu:bionic
#FROM ubuntu:focal
#FROM ubuntu:jammy
FROM ubuntu:noble

RUN useradd -ms /bin/bash builder

RUN apt-get update               \
 && apt-get -y -q upgrade        \
 && apt-get -y -q install        \
    bc                           \
    bison                        \
    build-essential              \
    ccache                       \
    flex                         \
    gcc-arm-linux-gnueabihf      \
    gcc-aarch64-linux-gnu        \
    git                          \
    libc6-armhf-cross            \
    libncurses-dev               \
    libssl-dev                   \
    make                         \
    u-boot-tools                 \
    wget                         \
    xz-utils                     \
    libncurses-dev               \
    nano                         \
 && apt-get clean

USER builder
WORKDIR /srv/
