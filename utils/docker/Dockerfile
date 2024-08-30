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
    sudo                         \
 && apt-get clean

RUN usermod -a -G sudo builder
RUN echo '%sudo ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers

USER builder
#WORKDIR /srv/
WORKDIR /home/builder
RUN git clone --depth 1 https://www.github.com/frank-w/BPI-ROUTER-Linux --branch 6.6-main --single-branch code
RUN sed -i.bak -e 's/^\(ramdisksize\)/#\1/' code/build.conf
