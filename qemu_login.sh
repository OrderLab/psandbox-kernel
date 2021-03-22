#!/bin/bash

GRAPHICS=-nographic
ENV_DIR="/home/yigonghu/phd/research/isolation/psandbox/psandbox-kernel"
IMAGE_PATH="$ENV_DIR/images/psandbox.img"
FS="$ENV_DIR/images/qemu-mount.dir"

if [ ! -f "$IMAGE_PATH" ]; then
  echo "$IMAGE_PATH do not exist. Please check that your images are build properly."
  exit 1
fi

QEMU_MEMORY="4G"
QEMU_KERNEL="$ENV_DIR/build/arch/x86/boot/bzImage"
QEMU="qemu-system-x86_64"
#QEMU_EXTRA_FLAGS="-device e1000,netdev=net0,mac=DE:AD:BE:EF:7B:6A -netdev tap,id=net0"

sudo mount -o loop $IMAGE_PATH $FS
sleep 1
sudo cp $ENV_DIR/.bash_login $FS/root
sudo cp $ENV_DIR/queue $FS/root
sudo cp $ENV_DIR/mutex $FS/root
sudo cp $ENV_DIR/sleep $FS/root
sudo cp $ENV_DIR/mutex_pre $FS/root
sleep 1
sudo umount $FS
sleep 1
sudo $QEMU -kernel $QEMU_KERNEL  -hda $IMAGE_PATH \
      -append "root=/dev/sda console=ttyS0"\
      -k en-us $GRAPHICS -m $QEMU_MEMORY $QEMU_EXTRA_FLAGS\
      -enable-kvm 
