#!/bin/bash
export LD_LIBRARY_PATH=$HOME/software/mysql/dist/lib:$HOME/software/psandbox-userlib/build/libs:$HOME/software/postgresql/dist/lib
export PATH=$HOME/software/sysbench/dist/bin:$HOME/software/postgresql/dist/bin:$PATH
export PSANDBOXDIR=$HOME/software/psandbox-userlib
cd /home/psandbox/software/postgresql/dist
./bin/postgres -D data/ --config-file=data/postgresql.conf &
sleep 1
./test.py
#sudo shutdown
