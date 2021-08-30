# PerfSandbox Kernel

## Build

```bash
make
make image
```

## TEST
Run the mysql case

```bash
./qemu_login.sh

```

## Move file
Copy the software file

```bash
rsync -rcnv --exclude=file yigonghu@192.168.122.1:/home/yigonghu/research/perfIsolation/software/isolation_mysql/5.6.22/ /home/psandbox/software/mysql/5.6.22/
```
