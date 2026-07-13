# DMX Tester

Simple DMX test tool for Pi Pico or other RP2040-based microcontroller boards

# Connections

GPIO 0 : UART TXD (output from RP2040), connected to input of RS-485 transceiver

GPIO 1 : UART RXD (input for RP2040), currently unused

GPIO 2 : Driver Enable, connected to both the DE pin and the *RE pin of the RS-485 transceiver

GPIO 12: Connect this to an LED (via a suitable resistor). The LED simply lights up to indicate if the firmware is running, but it may be used for other purposes in the future.

# Building the Code

## Pre-requisites:
  ARM GNU Toolchain
  Pi Pico C/C++ SDK

## Windows
First examine the build.ps1 file, and edit it to suit your environment. Next, in a Windows PowerShell, type:
```
.\build.ps1
```
The built code (dmxtester.uf2) should appear in the subfolder called "build-Release".

# Uploading the Firmware to the RP2040
Hold down the BOOTSEL button, and insert the USB cable into the Pi Pico. Now release the BOOTSEL button.

Note: if your board has a RESET button too, then the process is a little easier; with the USB cable plugged in, hold down BOOTSEL, and then press and release RESET, and then release BOOTSEL.

The board should appear as a USB mass storage device. A drive letter will appear on your PC if using Windows. Drag the .uf2 file onto the drive letter, and after a few seconds, the firmware will have been transferred, and the firmware will immediately begin execution.

# Operating the Device
Device interaction is via USB.

If you're using Windows, use Device Manager to see what COM port has appeared. Connect using serial terminal software, using 115200 baud.

Press Enter to see a menu appear on the serial terminal. The commands are self-explanatory.



