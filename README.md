Simple USB headset, implemented on dev board STM32F407 discovery.

Demonstration and explanation available on YouTube (Ukrainian language): https://youtu.be/o_cMQ-7D7RI

This USB device is based on USB specification 2.0 & USB audio specification 1.0

Main drawback:
 * Device can work correctly only with host on Linux (was tested on Linux Arch and Linux Kali)
 * You can record sound from mic with Audacity program (as shown in video). It is possible, that there is another program
that can record sound as well, but I can do it with Audacity.
 * During first couple of seconds of recording, signal is very noisy. It is definitely a bug. See detail in Issue
