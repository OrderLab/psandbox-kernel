#!/bin/bash

mkdir -p images
cd images
qemu-img create psandbox.img 4g
mkfs.ext2 psandbox.img 
mkdir qemu-mount.dir
sudo mount -o loop  psandbox.img qemu-mount.dir/ 
sudo debootstrap --arch amd64  buster qemu-mount.dir http://ftp.cn.debian.org/debian/
echo 'root:root' | sudo chroot qemu-mount.dir chpasswd
sudo chroot qemu-mount.dir
cat << EOF | sudo tee "qemu-mount.dir/etc/fstab"
/dev/sda / ext4 errors=remount-ro,acl 0 1
EOF
sudo umount qemu-mount.dir

