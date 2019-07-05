#!/bin/bash
make clean
#这里修改为你的ndk的路径
export NDK=/home/hxk/software/android-ndk-r10e
#注意android-9文件夹的版本号，替换好自己的版本号。下面的路径同理
export SYSROOT=$NDK/platforms/android-9/arch-arm/
export TOOLCHAIN=$NDK/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64
#编译出来后支持的格式
export CPU=arm
export PREFIX=$(pwd)/android/$CPU
export ADDI_CFLAGS="-marm"
./configure --target-os=linux \
--prefix=$PREFIX --arch=arm \
--enable-doc \
--enable-shared \
--disable-static \
--disable-yasm \
--disable-symver \
--enable-gpl \
--disable-x86asm \
--enable-ffmpeg \
--disable-ffplay \
--enable-ffprobe \
--disable-ffserver \
--disable-linux-perf \ 
--disable-doc \
--disable-symver \
--cross-prefix=$TOOLCHAIN/bin/arm-linux-androideabi- \
--enable-cross-compile \
--sysroot=$SYSROOT \
--extra-cflags="-Os -fpic $ADDI_CFLAGS" \
--extra-ldflags="$ADDI_LDFLAGS" \
$ADDITIONAL_CONFIGURE_FLAG
#make clean
make
