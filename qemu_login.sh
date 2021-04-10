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

sudo LIBGUESTFS_HV=./guest-images/qemu.wrapper virt-copy-in -a images/psandbox.img launch.sh /home/psandbox/
sudo LIBGUESTFS_HV=./guest-images/qemu.wrapper virt-copy-in -a images/psandbox.img ../psandbox-userlib/cmake-build-debug/libpsandbox.so /home/psandbox/software
sudo LIBGUESTFS_HV=./guest-images/qemu.wrapper virt-copy-in -a images/psandbox.img /home/psandbox/software/mysql/dist/bin/mysqld /home/psandbox/software/mysql/dist/
sleep 1
LD_PRELOAD="../psandbox-userlib/cmake-build-debug/libpsandbox.so" $QEMU -kernel $QEMU_KERNEL  -hda $IMAGE_PATH \
      -append "root=/dev/sda console=ttyS0"\
      -k en-us $GRAPHICS -m $QEMU_MEMORY $QEMU_EXTRA_FLAGS\
      -enable-kvm 
