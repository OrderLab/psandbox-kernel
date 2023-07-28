#!/bin/bash

red='\033[0;31m'
green='\033[0;32m'
yellow='\033[0;33m'
clear='\033[0m'

color_out() {
  local color=$1
  local msg=$2
  echo -e -n "${color}${msg}${clear}"
}

color_out ${yellow} "[Step 1]: installing dependencies...\n"
set -x
sudo apt-get update
sudo apt-get install -y libelf-dev python3-pip cmake libreadline-dev scons libevent-dev gengetopt python-docutils libxml2-dev libpcre3-dev libevent-dev re2c libsqlite3-dev cgroup-tools intel-cmt-cat linux-tools-common apache2-utils 
pip3 install pandas matplotlib
set +x
cp .config src/
cd src && make kvm_guest.config
color_out ${yellow} "[Step 2]: compiling pBox kernel...\n"
make -j 20 bindeb-pkg LOCALVERSION=-my-k
if [ $? != 0 ]; then
  color_out ${red} "Failed to build pBox kernel\n"
  exit 1
else
  color_out ${green} "pBox kernel is successfully built!\n"
fi
cd ..
color_out ${yellow} "Continue to install pBox kernel? (y/n) "
read -n 1 choice
echo
case "$choice" in
  y|Y) 
    color_out ${yellow} "[Step 3]: installing pBox kernel...\n"
    (set -x; sudo dpkg -i linux-image-5.4.0-my-k_5.4.0-my-k-1_amd64.deb \
    linux-headers-5.4.0-my-k_5.4.0-my-k-1_amd64.deb \
    linux-libc-dev_5.4.0-my-k-1_amd64.deb)
    if [ $? != 0 ]; then
      color_out ${red} "Failed to install pBox kernel\n"
    else
      color_out ${green} "Successfully installed pBox kernel!\n"
    fi
    ;;
  *) 
    color_out ${yellow} "Skip installation.\n"
    ;;
esac 
