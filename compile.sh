#!/bin/bash

mkdir -p build
cd build
#cp ../linux-5.4/config-x86_64 .config
cp /boot/config-`uname -r`* .config 
cd ../linux-5.4
make O=../build defconfig
make O=../build kvm_guest.config 
make O=../build -j4

if [ $? -ne 0 ]; then
  echo "Failed to build mysql"
  exit 1
fi
