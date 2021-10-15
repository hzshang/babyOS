#! /bin/sh
#
# setup.sh
# Copyright (C) 2021 mac <hzshang15@gmail.com>
#
# Distributed under terms of the MIT license.
#


ip link add veth1 type veth peer name br-veth1
ifconfig veth1 10.1.1.1/24
ip link add name br1 type bridge
ip link set br-veth1 master br1
ip link set br-veth1 up
ip link set br1 up
tunctl -t aaa0 -u root
ip link set aaa0 up
ip link set aaa0 master br1


