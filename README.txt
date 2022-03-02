Building & Installing DATOP
===========================

DATOP uses autotools. If you're compiling from git, run `autogen.sh`
and then `make`. Otherwise, use `./configure && make`.

as follows:
 1. automake && autoconf

To install, run `sudo make install`.

To clean: make clean or make distclean.

Build Dependencies
==================

DATOP requires following libraries:

 1. check and check-devel
 2. numactl-devel or libnuma-dev(el)
 3. libncurses
 4. libpthread

Supported Kernels
=================

This tool mainly supports the OpenAnolis OS. And if you find the OS release
which not support, you can contact with us or submit your PR. Any patchset
and suggestions that are valuable to it will be welcome.

You can find more information about OpenAnolis OS in:
https://openanolis.cn

Manual
======

We also has provided datop with man guidebook, the usage of datop's man
as follows:

 $ gzip -c datop.8 > /usr/share/man/man8/datop.8.gz
 $ man 8 datop

The detailed usage of datop can been found in this guidebook.

Contributors
============

Rongwei Wang (rongwei.wang@linux.alibaba.com)
Xin Hao (xhao@linux.alibaba.com)
Xunlei Pang (xlpang@linux.alibaba.com)
