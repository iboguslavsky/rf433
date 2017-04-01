Linux Loadable Kernel Module to control 433MHz transmitter. Uses High Resolution kernel timer to provide accurate timing for the generated waveforms. Tested successfully to work with Etekcity power outlets. Takes arbitrary full frame - or separate address + command combination.

**Works with Orange Pi Zero (Uses PG06 / UART1_TX pin on 26-pin connector). Tested under Legacy Kernel only (3.4.X).**

<pre>
> git clone https://github.com/iboguslavsky/rf433.git
> cd rf433
> make all
> insmod ./rf433.ko
</pre>

After the module loads:

<pre>
> cd cd /sys/class/rf433/rf0
> echo "00f0fff10001" > packet
> echo 1 > send
</pre>

This sends command "00f0fff10001" (address: 00f0fff1, command: 0001).

You can supply the address and command separately via ./address and ./command files in the same directory.
