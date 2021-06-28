#!/bin/bash

export LD_LIBRARY_PATH=$HOME/software/mysql/dist/lib:$HOME/software/
export PATH=$HOME/software/sysbench/dist/bin:$PATH
cd /home/psandbox/software/mysql/dist
./mysqld --defaults-file=my.cnf &
sleep 5
cd /home/psandbox/software/sysbench/dist/share/sysbench/
sysbench --mysql-socket=/home/psandbox/software/mysql/dist/mysqld.sock --mysql-db=test --tables=5 --table-size=1000 --threads=4 --time=40 oltp_update_index.lua  run & 
sleep 1
sysbench --mysql-socket=/home/psandbox/software/mysql/dist/mysqld.sock --mysql-db=test --tables=5 --table-size=1000 --threads=1 --time=30 oltp_point_select.lua --report-interval=1 run 
sleep 5
cd /home/psandbox/software/mysql/dist
./bin/mysqladmin -S mysqld.sock -u root shutdown
#sudo shutdown
