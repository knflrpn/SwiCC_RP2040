# SwiCC_RP2040
Switch Controller Controller for RP2040 boards

Enumerates as a controller for a Nintendo Switch, and lets you control that controller via Serial commands.

# Serial API
All serial commands begin with "+", then an instruction, then a space character.  Most instruction take a parameter after the space.  All serial commands end with a newline.  For example, "+LED 0\n" disables the NeoPixel status LED.

## Instructions

| Instruction | Parameter | Description |
|--|--|--|
| ID | None | Returns "+SwiCC\r\n" to identify the connected hardware. |
| LED | 0 or 1 | Disables or enables NeoPixel feedback LED. |
| IMM | Controller state | Sets the immediate controller state. |

Controller state (as needed for commands) is a 17-digit hex string representing 7 bytes of data.
- Byte 0 (first btye in string): upper buttons.
- Byte 1: lower buttons
- Byte 2: D-pad
- Bytes 3-6: Analog stick values (LX, LY, RX, RY)
