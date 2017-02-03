#  Magenta on Raspberry Pi 3

Magenta is a 64-bit kernel that is capable of running on the Raspberry Pi 3. 

Presently it supports a number of the Raspberry Pi 3's peripherals including 
the following:
 + USB (Keyboard, Mouse, Flash Drives)
 + Ethernet
 + Software Driven HDMI
 + Serial Port (MiniUART)

The following peripherals are not yet supported:
 + Wi-Fi
 + Hardware Accelerated Graphics
 + Bluetooth

## Requirements

The following hardware is required:
 + Raspberry Pi 3
 + MicroSD Card (at least 32MB suggested)
 + Micro USB cable for power
 + At least one of the following for Input/Output
    - 3.3v FTDI Serial Dongle (recommended, especially for low-level hacking)
    - USB Keyboard / HDMI Monitor

## Building
To build magenta, invoke the following command from the top level Magenta
directory (ensure that you have checked out the ARM64 toolchains). For more
information, see `docs/getting_started.md`:

    make rpi3-test

## Installing
1. To install Magenta, ensure that your SD is formatted as follows:
   + Using an MBR partition table
   + With a FAT32 boot partition

2. Invoking `make rpi3-test`  should have created a file called `magenta.bin` in
   at the following path `./build-rpi3-test/magenta.bin`

3. Copy the `magenta.bin` file to the SD card's boot partition as `kernel8.bin`
   as follows:

        cp ./build-rpi3-test/magenta.bin <path/to/sdcard/mount>/kernel8.bin

4. You must also copy `bootcode.bin` and `start.elf` to the boot partition. They
   can be obtained from [here](https://github.com/raspberrypi/firmware/tree/master/boot)

5. Create a file called `config.txt` in the boot partition with the following
   contents:

   ```
   ### config.txt ###

   # Tells the Pi's bootloader which file contains the kernel.
   kernel=kernel8.img

   # Necessary if you're using a serial dongle to talk to the Pi over the UART
   enable_uart=1 

   # Used to configure HDMI, you may need to tweak these settings dependong on
   # the monitor you're using.
   hdmi_cvt=800 480 60 6 
   hdmi_group=2 
   hdmi_mode=87 
   hdmi_drive=2 
   framebuffer_depth=32 
   framebuffer_ignore_alpha=1
   ```

6. At this point your SD Card should be formatted with an MBR partition table
   and FAT32 boot partition that contains the following four files:
   + bootcode.bin
   + config.txt
   + kernel8.img
   + start.elf

7. If you're using the Serial Console, connect your serial dongle to the RPi3 
   header as follows:
   1. Pin 6 - GND
   2. Pin 8 - TXD (output from Pi)
   3. Pin 10 - RXD (input to pi)
   4. Baudrate = 115200

8. Insert the SD Card and connect power to boot the Pi


