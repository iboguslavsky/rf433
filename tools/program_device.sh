#!/bin/bash
#
# Program Etekcity devices with custom on/off codes
#
# Etekcity code words consist of 10-bit address (0, 1 and f bits)
# and 2-bit command (only 0 and 1 bits as per PT2262 spec)
# "on" = '01'
# "off = '10'

echo "Enter outlet address (10 bits valued 0, 1 or f)"
read addr

echo ${addr} > /sys/class/rf433/rf0/address

# Send "on" command
echo 01 > /sys/class/rf433/rf0/command

# Send "endlessly" until interrupted by user
echo 2 > /sys/class/rf433/rf0/send

echo -n "Plug in your outlet and push <Enter> when you hear the outlet click..."
read stop

# Stop transmitting codeframe
echo 0 > /sys/class/rf433/rf0/send

echo ${addr} > /sys/class/rf433/rf0/address

# Send "off" command
echo 10 > /sys/class/rf433/rf0/command
echo 2 > /sys/class/rf433/rf0/send

echo -n "Push <Enter> when you hear the outlet click again..."
read stop

echo 0 > /sys/class/rf433/rf0/send
