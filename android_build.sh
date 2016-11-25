#!/bin/bash
#

if [ -z "$ANDROID_NDK" ]; then
    echo "You must define ANDROID_NDK before starting."
    echo "They must point to your NDK directories.\n"
    exit 1
fi

# Detect OS
OS=`uname`
HOST_ARCH=`uname -m`
export CCACHE=; type ccache >/dev/null 2>&1 && export CCACHE=ccache
if [ $OS == 'Linux' ]; then
    export HOST_SYSTEM=linux-$HOST_ARCH
elif [ $OS == 'Darwin' ]; then
    export HOST_SYSTEM=darwin-$HOST_ARCH
fi

for arch in armeabi armeabi-v7a arm64-v8a x86; do

  case $arch in
    armeabi)
        SYSTEM_VERSION=15
        ANDROID_ARCH_ABI=armeabi
        SYSROOT=$SYSTEM_VERSION/arch-arm
        C_COMPILER=arm-linux-androideabi-4.9/prebuilt/$HOST_SYSTEM/bin/arm-linux-androideabi
    ;;

    armeabi-v7a)
        SYSTEM_VERSION=15
        ANDROID_ARCH_ABI=armeabi-v7a
        SYSROOT=$SYSTEM_VERSION/arch-arm
        C_COMPILER=arm-linux-androideabi-4.9/prebuilt/$HOST_SYSTEM/bin/arm-linux-androideabi
    ;;
    
    arm64-v8a)
        SYSTEM_VERSION=21
        ANDROID_ARCH_ABI=arm64-v8a
        SYSROOT=$SYSTEM_VERSION/arch-arm64
        C_COMPILER=aarch64-linux-android-4.9/prebuilt/$HOST_SYSTEM/bin/aarch64-linux-android
    ;;
    
    x86)
        SYSTEM_VERSION=15
        ANDROID_ARCH_ABI=x86
        SYSROOT=$SYSTEM_VERSION/arch-x86
        C_COMPILER=x86-4.9/prebuilt/$HOST_SYSTEM/bin/i686-linux-android
    ;;
  esac

make clean
rm -rf CMakeCache.txt CMakeFiles/
cmake .. \
  -DCMAKE_SYSTEM_NAME=Android \
  -DCMAKE_SYSTEM_VERSION=$SYSTEM_VERSION \
  -DCMAKE_ANDROID_ARCH_ABI=$ANDROID_ARCH_ABI \
  -DCMAKE_SYSROOT=$ANDROID_NDK/platforms/android-$SYSROOT \
  -DCMAKE_C_COMPILER=$ANDROID_NDK/toolchains/$C_COMPILER-gcc \
  -DHAVE_FMEMOPEN=1
make libsox gsm lpc10

mkdir -p ../../libs/sox/$arch
cp src/liblibsox.a ../../libs/sox/$arch
cp libgsm/libgsm.a ../../libs/sox/$arch
cp lpc10/liblpc10.a ../../libs/sox/$arch

echo "*****************************finish building arch $arch . *********************";

done
