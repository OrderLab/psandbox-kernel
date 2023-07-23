#!/bin/bash

sudo apt-get update
sudo apt-get install -y libelf-dev
cp .config src/
cd src && make kvm_guest.config
cd src && make -j 20 bindeb-pkg LOCALVERSION=-my-k
sudo dpkg -i linux-image-5.4.0-my-k_5.4.0-my-k-1_amd64.deb \
linux-headers-5.4.0-my-k_5.4.0-my-k-1_amd64.deb \
linux-libc-dev_5.4.0-my-k-1_amd64.deb 
sudo reboot

