#!/bin/sh

set -ex

# Install 32-bit user space for 64-bit kernels
install_x86() {
    if uname -a | grep -q i386; then
        sudo dpkg --add-architecture x86
        sudo apt update

    fi
    # install required lib for psandbox
    sleep 2
    sudo apt -y install wget git cmake vim bison build-essential default-libmysqlclient-dev libaio-dev libmariadb-dev libncurses-dev libssl-dev pkg-config liblua5.3-dev
}


# Install mysql from source
# Add for psandbox project
#install_mysql
install_mysql() {
 mkdir -p /home/psandbox/software/
 cd /home/psandbox/software
 git clone https://github.com/gongxini/isolation_mysql.git
 mv isolation_mysql mysql
 sleep 2
 cd mysql
 ./compile.sh
 cp my.cnf dist
 ./init_db.sh
 cd /home/psandbox
}

# Install sysbench from source
install_sysbench() {
 cd /home/psandbox/software/
 mkdir sysbench
 cd sysbench
 git clone https://github.com/akopytov/sysbench.git
 mv sysbench 1.0.19
 cd 1.0.19
 git checkout 1.0.19
 ./autogen.sh
 ./configure --prefix=/home/psandbox/software/sysbench/dist --with-mysql-includes=/home/psandbox/software/mysql/dist/include --with-mysql-libs=/home/psandbox/software/mysql/dist/lib
 make
 make install
 cd /home/psandbox
}


# Install kernels last, the cause downgrade of libc,
# which will cause issues when installing other packages
install_kernel() {
    sudo dpkg -i *.deb

    MENU_ENTRY="$(grep menuentry /boot/grub/grub.cfg  | grep s2e | cut -d "'" -f 2 | head -n 1)"
    echo "Default menu entry: $MENU_ENTRY"
    echo "GRUB_DEFAULT=\"1>$MENU_ENTRY\"" | sudo tee -a /etc/default/grub
    sudo update-grub
}

# Install the prerequisites for cgc packages
install_apt_packages() {
    APT_PACKAGES="
    python-apt
    python-crypto
    python-daemon
    python-lockfile
    python-lxml
    python-matplotlib
    python-yaml
    tcpdump
    "

    sudo apt-get -y install ${APT_PACKAGES}

    # This package no longer exists on recent debian version
    wget http://ftp.us.debian.org/debian/pool/main/p/python-support/python-support_1.0.15_all.deb
    sudo dpkg -i python-support_1.0.15_all.deb
}

sudo apt update
install_x86
install_mysql
install_sysbench
#install_kernel
sleep 10
# QEMU will stop (-no-reboot)
sudo shutdown
