### Beginning of user-adjustable variables ###

PSANDBOX_INSTALL_ROOT ?= $(shell pwd)
QEMU_MEMORY=4G

# Where to store final images
OUTDIR ?= $(shell pwd)/images
QEMU_KERNEL ?= $(shell pwd)/build/arch/x86/boot/bzImage
SRC ?= $(dir $(abspath $(lastword $(MAKEFILE_LIST))))/guest-images


# If your host does not support KVM, comment out this variable.
#QEMU_KVM ?= -enable-kvm

# Comment out this variable to enable graphic output
GRAPHICS ?= -nographic -monitor null

### End of user-adjustable variables ###

QEMU64 = /usr/bin/qemu-system-x86_64
QEMU_IMG = /usr/bin/qemu-img
WGET = wget --no-use-server-timestamps -O
_INFO_MSG_COLOR := $(shell tput setaf 3)
_NO_COLOR := $(shell tput sgr0)
INFO_MSG = @echo "$(_INFO_MSG_COLOR)[`date`] $1$(_NO_COLOR)"
QEMU_HD = -append "root=/dev/sda console=ttyS0" -k en-us

ifeq ("$(wildcard $(QEMU64))","")
$(error $(QEMU64) does not exist. Make sure qemu is installed)
endif

### Building Linux kernel ###
define BUILD_KERNEL
	$(call INFO_MSG, Building image...)
	mkdir -p build
	cd build && cp /boot/config-`uname -r`* .config
	cd src && make O=../build defconfig
	cd src && make O=../build kvm_guest.config
	cd src && make O=../build -j `nproc`
endef

### Building images ###
define BASE_LINUX_IMAGES_BUILD
	$(call INFO_MSG, Building image...)
 	mkdir -p $(OUTDIR)
	$(QEMU_IMG) create $(OUTDIR)/psandbox.img 32g
	mkfs.ext2 $(OUTDIR)/psandbox.img
	cd $(OUTDIR) && mkdir -p qemu-mount.dir
	sudo mount -o loop  $(OUTDIR)/psandbox.img $(OUTDIR)/qemu-mount.dir/
	sudo debootstrap --arch amd64  buster $(OUTDIR)/qemu-mount.dir http://mirrors.ustc.edu.cn/debian/ 
	echo 'root:root' | sudo chroot $(OUTDIR)/qemu-mount.dir chpasswd
	echo 'adduser psandbox' | sudo chroot $(OUTDIR)/qemu-mount.dir
	echo "passwd -d root" | sudo chroot $(OUTDIR)/qemu-mount.dir
	echo "passwd -d psandbox" | sudo chroot $(OUTDIR)/qemu-mount.dir
	echo "apt install sudo" | sudo chroot $(OUTDIR)/qemu-mount.dir
	echo "usermod -aG sudo psandbox" | sudo chroot $(OUTDIR)/qemu-mount.dir
	sudo umount $(OUTDIR)/qemu-mount.dir
	
	$(call INFO_MSG, Installing payload...)
	sleep 30
	sudo LIBGUESTFS_HV=$(SRC)/qemu.wrapper virt-copy-in -a $(OUTDIR)/psandbox.img $(SRC)/fstab  /etc/
	sudo LIBGUESTFS_HV=$(SRC)/qemu.wrapper virt-copy-in -a $(OUTDIR)/psandbox.img $(SRC)/launch.sh $(SRC)/.bash_login  /home/psandbox/
	sudo LIBGUESTFS_HV=$(SRC)/qemu.wrapper virt-copy-in -a $(OUTDIR)/psandbox.img $(SRC)/00mylinux /etc/network/interfaces.d/
	sudo LIBGUESTFS_HV=$(SRC)/qemu.wrapper guestfish --rw -a $(OUTDIR)/psandbox.img -i mkdir /etc/systemd/system/serial-getty@ttyS0.service.d/
  sudo LIBGUESTFS_HV=$(SRC)/qemu.wrapper virt-copy-in -a $(OUTDIR)/psandbox.img $(SRC)/autologin.conf /etc/systemd/system/serial-getty@ttyS0.service.d/
	$(call INFO_MSG,Booting disk image...)
	$(QEMU64) -kernel $(QEMU_KERNEL) -hda $(OUTDIR)/psandbox.img $(QEMU_HD) -m 4g -no-reboot $(GRAPHICS) $(QEMU_KVM)
endef


all:
	$(call BUILD_KERNEL)

image:
	$(call BASE_LINUX_IMAGES_BUILD)
	$(call BUILD_PSANDBOX_IMAGE)

clean:
	rm -rf $(OUTDIR)/ 
