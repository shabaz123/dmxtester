# DMX Tester

Simple DMX test tool for Pi Pico or other RP2040-based microcontroller boards

This software implements a DMX512 transmitter. It continuously outputs a DMX universe (i.e. 512 channels) at **40 Hz** while providing an interactive serial command interface.

It allows you to use your PC and a command line to set various channels in a DMX universe to desired values.
Here is what the user menu looks like:

```
Commands:
  help                         Show this command list
  set_chan <channel> <value>   Set channel 1..512 to 0..255
  get_chan <channel>           Read channel 1..512
  set_all <value>              Set all channels to 0..255
  set_rgb <first> <r> <g> <b>  Set three consecutive channels
  blackout                     Set all channels to zero
  status                       Show DMX transmitter statistics
```


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

# Software Overview

The application uses both RP2040 cores:

| Core | Responsibility |
|------|----------------|
| Core 0 | User interface, command parsing, line editing, history, and updating the DMX universe |
| Core 1 | Continuous 40 Hz DMX transmission |

Separating these tasks ensures USB serial activity cannot disturb DMX timing.

## Architecture

```text
The software uses an array to store the 512 bytes used for the entire DMX universe. 

Core 0: 
USB Terminal --> Line Editor --> Command Parser --> DMX Universe Shared Memory

Core 1: 
40 Hz DMX Task --> Copy DMX Universe Shared Memory --> Transmit Buffer -> dmx.write()
```

Every 25 milliseconds, Core 1 copies the shared SMX Universe into a local transmit buffer and then calls `dmx.write()`. This ensures that each transmitted DMX frame is internally consistent, even if Core 0 is simultaneously processing user commands and modifying the shared universe.

## Key Source Functions

### `main()`
Initialises stdio, GPIO, the DMX driver, launches Core 1, displays the prompt, and repeatedly accepts commands.

### `core1_main()`
Runs only on Core 1. Maintains the 40 Hz schedule, copies the DMX universe, calls `dmx.write()`, waits for transmission to finish, and repeats.

### `line_editor()`
Provides interactive command editing:
- Left/right cursor movement
- Character insertion
- Backspace/Delete
- Up/down command history
- Editing of recalled commands

History depth is controlled by:

```cpp
#define COMMAND_HISTORY_DEPTH 20
```

### `execute_command()`
Parses a complete command line and dispatches commands such as:
- help
- status
- set_chan
- get_chan
- set_all
- set_rgb
- blackout

### `set_channel()`
Validates channel and value ranges before updating one DMX channel.

### `set_all_channels()`
Writes the same value to every channel.

### `set_rgb()`
Convenience routine that writes three consecutive channels for RGB fixtures.

### `print_help()`
Displays the available commands.

## Thread Safety

The DMX universe is shared by both cores.

Core 0 enters a critical section whenever it modifies the universe.

Core 1 enters the same critical section only while copying the universe into its transmit buffer, keeping lock times extremely short.

## Design Decisions

- Core separation keeps DMX timing independent of terminal activity.
- A local transmit buffer ensures complete frames are transmitted.
- Command parsing is independent of the DMX transport layer, making new commands easy to add.

## Extending the Software

To add a new command:

1. Add the command to `print_help()`.
2. Extend `execute_command()`.
3. Implement any helper function required.

The Core 1 transmission task normally does not need modification.



