FROM ubuntu:bionic

RUN apt-get update && \
    apt-get install -y --no-install-recommends software-properties-common && \
    apt-get update && \
    apt-get install -y --no-install-recommends \
    build-essential \
    bzip2 \
    dfu-util \
    git \
    libreadline-dev \
    unzip \
    zip \
    xxd \
    wget && \
    rm -rf /var/lib/apt/lists/*

RUN wget --quiet "https://developer.arm.com/-/media/Files/downloads/gnu-rm/10.3-2021.10/gcc-arm-none-eabi-10.3-2021.10-x86_64-linux.tar.bz2?rev=78196d3461ba4c9089a67b5f33edf82a&hash=5631ACEF1F8F237389F14B41566964EC" -O gcc-arm-none-eabi-10.3-2021.10.tar.bz2 && \
    tar -xjf gcc-arm-none-eabi-10.3-2021.10.tar.bz2 && \
    ln -s /gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-gcc /usr/bin/arm-none-eabi-gcc && \
    ln -s /gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-g++ /usr/bin/arm-none-eabi-g++ && \
    ln -s /gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-gdb /usr/bin/arm-none-eabi-gdb && \
    ln -s /gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-size /usr/bin/arm-none-eabi-size && \
    ln -s /gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-objcopy /usr/bin/arm-none-eabi-objcopy && \
    ln -s /gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-objdump /usr/bin/arm-none-eabi-objdump && \
    rm gcc-arm-none-eabi-10.3-2021.10.tar.bz2

RUN wget --quiet https://www.lua.org/ftp/lua-5.3.4.tar.gz -O lua.tar.gz && \
    tar -xzf lua.tar.gz && \
    cd lua-5.3.4 && \
    make linux test && \
    make install && \
    cd .. && \
    rm lua.tar.gz

WORKDIR /target
ENTRYPOINT ["make", "-j", "R=1", "zip"]
