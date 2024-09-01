FROM ubuntu:noble

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
    kmod                         \
    debhelper rsync cpio         \
 && apt-get clean

RUN usermod -aG sudo ubuntu && \
    echo 'ubuntu ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers

RUN mkdir -p /srv/code && chmod a+rwx -R /srv

WORKDIR /srv/code
