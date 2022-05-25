FROM ubuntu:22.04

ARG COMPILER=aarch64-elf-12.1.0-Linux-x86_64
ARG COMPILER_TAR=$COMPILER.tar.xz
ADD http://newos.org/toolchains/$COMPILER_TAR /compiler/

RUN apt update
RUN apt install build-essential xz-utils qemu-system-aarch64 patchelf python3-dev -y
WORKDIR /compiler
RUN tar xf $COMPILER_TAR
RUN patchelf --replace-needed libpython3.8.so.1.0 /usr/lib/x86_64-linux-gnu/libpython3.10.so /compiler/aarch64-elf-12.1.0-Linux-x86_64/bin/aarch64-elf-gdb

ENV PATH=/compiler/$COMPILER/bin:$PATH
# RUN PROJECT=rpi3-test ARCH_arm64_TOOLCHAIN_PREFIX=/compiler/aarch64-elf-12.1.0-Linux-x86_64/bin/aarch64-elf- make -j`nproc`
# RUN qemu-system-aarch64 -M raspi3b -kernel lk.elf -semihosting -serial null -serial mon:stdio -nographic
