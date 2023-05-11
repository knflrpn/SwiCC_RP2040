# SwiCC_RP2040
Switch Controller Controller for RP2040 boards

Enumerates as a controller for a Nintendo Switch, and lets you control that controller via Serial commands.

## Usage
The recommended development board is a Waveshare RP2040 Zero because of its small size and onboard RGB LED, but any RP2040 board should work with appropriate configuration changes.  The precompiled firmware in the releases assume this board.

Once the SwiCC_RP2040 firmware is installed on the RP2040 board, you can use the serial API to control the controller. The API allows you to send commands to the board over a serial connection (115200 baud).  Serial TX (from the RP2040) is pin 0 and RX (into the board) is pin 1.

There is a web interface to make game controller i/o easier if that's what you plan to do with it: see [https://github.com/knflrpn/GLaMS](https://github.com/knflrpn/GLaMS).

## Assembly
The recommended assembly is to configure a second Waveshare RP2040 board as a USB-UART adapter using [this file](/documentation/SwiCC_UART_Bridge.uf2) and then mounting both boards in an enclosure. A custom box is available [here](https://www.printables.com/model/408393-swicc-box).  If using this method, cross-wire pins 0 and 1 (0->1 and 1->0) between the boards and connect their grounds.

![Alt text](/documentation/SwiCCBox.jpg)

If you don't have the ability to solder, you can buy the boards with pre-installed pin headers and use female-female hookup wires to connect the required pins.

A WS2812 ("Neopixel") LED can be connected to GPIO 16 to display connection state and a heartbeat.  The WaveShare RP2040 Zero board has an onboard LED already connected to this pin.

## Serial API
All serial commands begin with "+", then an instruction, then a space character.  Most instructions take a parameter after the space.  All serial commands end with a newline.  For example, `+LED 0\n` disables the NeoPixel status LED.

### General Use Instructions

| Instruction | Parameter | Description |
|--|--|--|
| ID | None | Returns "+SwiCC\r\n" to identify the connected hardware. |
| LED | 0 or 1 | Disables or enables NeoPixel feedback LED. |
| IMM | Controller state | Sets the immediate controller state. |
| Q | Controller state | Adds the controller state to the queue. |
| QL | Controller state | Adds the controller state to the lagged queue. |
| SLAG | Decimal number 0-120 | Sets the amount of lag, in frames, for the lagged queue. |
| VSD | Four hex digits | Sets the VSYNC delay. Should be between 0x0000 and 0x3A00. |
| GCS | None | Gets the USB connection status, returning "+GCS \_\r\n" where _ is 0 or 1. |
| GQF | None | Gets the queue buffer fullness, returning "+GQF [four hex digits]\r\n". |

Controller state (as needed for commands) is a 17-digit hex string representing 7 bytes of data.
- Byte 0 (first byte in string): upper buttons.
- Byte 1: lower buttons
- Byte 2: D-pad
- Bytes 3-6: Analog stick values (LX, LY, RX, RY)

The upper/lower buttons are a bit mask indicating which buttons are pressed.  The order for the upper buttons is, in order of bit0-bit4, [minus, plus, left stick, right stick, home, capture].  The lower buttons are, in order of bit0-bit7, [Y, B, A, X, L, R, ZL, ZR].  For the d-pad, a value of 8 is neutral, and a value of 0-7 indicates a direction being pressed, with 0 indicating up, 1 indicating up-right, 2 indicating right, and continuing clockwise up to 7 (up-left).

Optionally, only the first three bytes of a controller state can be sent (with IMM, Q, or QL commands) if the analog sticks are not needed.  In that case, they will be set to neutral.

#### TAS Instructions

| Instruction | Parameter | Description |
|--|--|--|
| VSYNC | 0 or 1 | Enables or disables VSYNC synchronization. |
| REC | 0 or 1 | Stops (0) or starts (1) recording. |
| GRF | None | Gets the recording buffer fullness. |
| GRR | None | Gets the recording buffer remaining. |
| GRB | None | Gets the total recording buffer size. |
| GR | 0 or 1 | Initiates transfer of recorded inputs.  If parameter is 0, transfer will begin at the beginning.  If 1, transfer will continue from the previous point.

Recorded inputs are sent as a controller state followed by the character "x" and then the number of frames that the same input was active (i.e. run-length encoding).

## The Queue
SwiCC allows you to add controller states to a queue, which will be played back automatically, one per frame.  This is intended for TAS playback.

The queue has a capacity of 256 controller states, so it's important to monitor the buffer usage and avoid exceeding its capacity. To use the queue functionality, issue `Q` instructions to add controller states to the queue.  It's recommended to send around 100 controller states to the queue and then monitor the buffer usage using the GQF (Get Queue Fill) instruction. Once the buffer falls to around 50, you can send another batch of controller states.

For TAS playback to sync, frame timing information must be provided to SwiCC and tuned using the VSD instruction.

The `VSYNC 1` instruction must be executed to enable synchronization.  `VSYNC 0` will use an internal approximately-60-Hz timer.

## The Lagged Queue
Using the QL instruction is similar to the IMM instruction in that it should be used to set real-time controller states, but the state will be added to a buffer and played a fixed amount of time in the future.  The amount of time in the future is controller by the SLAG instruction.  This is a gimmick functionality intended to make it more difficult to play games.
