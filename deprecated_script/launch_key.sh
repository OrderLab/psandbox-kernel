#!/bin/bash

export LD_LIBRARY_PATH=$HOME/software/mysql/dist/lib:$HOME/software/
export PATH=$HOME/software/sysbench/dist/bin:$PATH
cd /home/psandbox/software/mysql/dist
./mysqld --defaults-file=my.cnf &
sleep 5
cd /home/psandbox/software/sysbench/dist/share/sysbench/
sysbench --mysql-socket=/home/psandbox/software/mysql/dist/mysqld.sock --mysql-db=test1 --tables=32 --secondary=on --table-size=1000 --threads=16 --time=120 --report-interval=3 oltp_insert.lua run  
sleep 1
cd /home/psandbox/software/mysql/dist
./bin/mysqladmin -S mysqld.sock -u root shutdown
#sudo shutdown
