#!/bin/bash

addr='1f0ff110';
cmdon='f000';
cmdoff='f010';

echo $addr > /sys/class/rf433/rf0/address
echo $cmdon > /sys/class/rf433/rf0/command
echo 2 > /sys/class/rf433/rf0/send

read stop
echo 0 > /sys/class/rf433/rf0/send

echo $addr > /sys/class/rf433/rf0/address
echo $cmdoff > /sys/class/rf433/rf0/command
echo 2 > /sys/class/rf433/rf0/send

read stop
echo 0 > /sys/class/rf433/rf0/send
