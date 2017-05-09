Linux Loadable Kernel Module to control 433MHz transmitter. Uses High Resolution kernel timer to provide accurate timing for the generated waveforms. Tested successfully to work with Etekcity power outlets. Takes arbitrary full frame (or "codeword" in Princeton Technology parlance) - or a separate address + command combination.

**Works with Orange Pi Zero (Uses PG06 / UART1_TX pin on 26-pin connector). Tested under Legacy Kernel only (3.4.X).**

Uses popular Princeton Technology's PT2262/PT2272 style bit representation and encoding scheme illustrated here:

<img src="https://github.com/iboguslavsky/rf433/blob/master/images/pt2272_encoding1.jpg" width="600">

To compile:

<pre>
> git clone https://github.com/iboguslavsky/rf433.git
> cd rf433
> make all
> insmod ./rf433.ko
</pre>

After the module loads:

<pre>
> cd /sys/class/rf433/rf0
> echo "00f0fff10001" > codeword  # Use command "01" ("ON") to address "00f0fff100"
> echo "1" > send                 # Send it out
</pre>

Alternatively, you can supply address and command separately:
<pre>
> cd /sys/class/rf433/rf0
> echo "00f0fff100" > address  # Set address to "00f0fff100"
> echo "01" > command          # Set command to "01" ("ON") 
> echo "1" > send              # Send it out
</pre>

Trasmitter used: RCT-433-UTR (Mouser P/N: 509-RCT-433-UTR):

<img src="https://github.com/iboguslavsky/rf433/blob/master/images/IMG_3643.JPG" width="500">
