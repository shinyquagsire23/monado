#!/bin/sh
VERSION=r21
FN=android-ndk-${VERSION}-linux-x86_64.zip
wget https://dl.google.com/android/repository/$FN
unzip $FN -d /opt
mv /opt/android-ndk-${VERSION} /opt/android-ndk
