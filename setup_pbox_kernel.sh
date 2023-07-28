#!/bin/bash

sudo apt-get update
sudo apt-get install -y libelf-dev python3-pip cmake libreadline-dev scons libevent-dev gengetopt python-docutils libxml2-dev libpcre3-dev libevent-dev re2c libsqlite3-dev cgroup-tools intel-cmt-cat linux-tools-common apache2-utils 
pip3 install pandas matplotlib
cp .config src/
cd src && make kvm_guest.config
make -j 20 bindeb-pkg LOCALVERSION=-my-k
cd ..
sudo dpkg -i linux-image-5.4.0-my-k_5.4.0-my-k-1_amd64.deb \
linux-headers-5.4.0-my-k_5.4.0-my-k-1_amd64.deb \
linux-libc-dev_5.4.0-my-k-1_amd64.deb 
