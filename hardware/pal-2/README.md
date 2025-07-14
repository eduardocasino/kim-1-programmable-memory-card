# Notes for the PAL-2 version

## **WARNING**

**THIS MOD MAY DAMAGE YOUR PAL-2 IF PERFORMED INCORRECTLY. DO IT AT YOUR OWN RISK!!!**

## PAL-2 mod

The PAL-2 behaves differently from the KIM-1 when the #DECEN signal goes high.

On the KIM-1, this signal disables all internal address decoding, allowing an expansion card to take over. However, on the PAL-2, the signal is wire-ORed with the 8K0 enable output of the first decoder, so it effectively has no impact.

To enable expansion card functionality, a simple modification must be performed on the PAL-2:

* Cut the trace that connects pin K of the Application connector to pin 12 of U6, and the trace connecting pin 12 of U8 to ground.
* Solder a wire from pin K of the Application connector to pin 12 of U8.
* Solder a 3.9kΩ resistor from pin 12 of U8 to any ground point on the board.

[mod schematic](https://github.com/eduardocasino/kim-1-programmable-memory-card/blob/main/hardware/pal-2/images/pal-2-mod-schematic.png?raw=true)
[mod picture](https://github.com/eduardocasino/kim-1-programmable-memory-card/blob/main/hardware/pal-2/images/pal-2-mod-pcb.png?raw=true)
