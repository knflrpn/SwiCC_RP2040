# SwiCC_RP2040
Switch Controller Controller for RP2040 boards

Enumerates as a controller for a Nintendo Switch, and lets you control that controller via Serial commands.

## Usage
Once the SwiCC_RP2040 firmware is installed on the RP2040 board, you can use the serial API to control the controller. The API allows you to send commands to the board over a serial connection (115200 baud).  Serial TX (from the RP2040) is pin 0 and RX (into the board) is pin 1.

## Serial API
All serial commands begin with "+", then an instruction, then a space character.  Most instruction take a parameter after the space.  All serial commands end with a newline.  For example, `+LED 0\n` disables the NeoPixel status LED.

### Instructions

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
| GQF | None | Gets the queue buffer fullness, returning four hex digits. |
| SRC | None | Continues sending recording. |
| VSYNC | 0 or 1 | Enables or disables VSYNC synchronization. |

Controller state (as needed for commands) is a 17-digit hex string representing 7 bytes of data.
- Byte 0 (first btye in string): upper buttons.
- Byte 1: lower buttons
- Byte 2: D-pad
- Bytes 3-6: Analog stick values (LX, LY, RX, RY)

Optionally, only the first three bytes can be sent if the analog sticks are not needed.  In that case, they will be set to neutral.

## The Queue
SwiCC allows you to add controller states to a queue, which will be played back automatically, one per frame.  This is intended for TAS playback.

The queue has a capacity of 256 controller states, so it's important to monitor the buffer usage and avoid exceeding its capacity. To use the queue functionality, use the Q instruction to add controller states to the queue.  It's recommended to send around 100 controller states to the queue and then monitor the buffer usage using the GQF (Get Queue Fill) instruction. Once the buffer falls to around 50, you can send another batch of controller states.

For TAS playback to sync, frame timing information must be provided to SwiCC and tuned using the VSD instruction.

The `VSYNC 1` instruction must be executed to enable synchronization.  `VSYNC 0` will use an internal approximately-60-Hz timer.

## The Lagged Queue
Using the QL instruction is similar to the IMM instruction in that it should be used to set real-time controller states, but the state will be added to a buffer and played a fixed amount of time in the future.  The amount of time in the future is controller by the SLAG instruction.  This is a gimmick functionality intended to make it more difficult to play games.
