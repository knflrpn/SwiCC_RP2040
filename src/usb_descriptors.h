/* 
 * The MIT License (MIT)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef USB_DESCRIPTORS_H_
#define USB_DESCRIPTORS_H_

#define TUD_HID_REPORT_DESC_USBCON(...) \
  HID_USAGE_PAGE ( HID_USAGE_PAGE_DESKTOP     )        ,\
  HID_USAGE      ( HID_USAGE_DESKTOP_GAMEPAD  )        ,\
  HID_COLLECTION ( HID_COLLECTION_APPLICATION )        ,\
    /* Report ID if any */\
    __VA_ARGS__ \
		HID_LOGICAL_MIN(0),\
		HID_LOGICAL_MAX(1),\
		HID_PHYSICAL_MIN(0),\
		HID_PHYSICAL_MAX(1),\
		/* The Switch will allow us to expand the original HORI descriptors to a full 16 buttons.*/\
		/* The Switch will make use of 14 of those buttons.*/\
		HID_REPORT_SIZE(1),\
		HID_REPORT_COUNT(16),\
		HID_USAGE_PAGE(9),\
		HID_USAGE_MIN(1),\
		HID_USAGE_MAX(16),\
		HID_INPUT(2),\
		/* HAT Switch (1 nibble) */\
		HID_USAGE_PAGE(1),\
		HID_LOGICAL_MAX(7),\
		HID_PHYSICAL_MAX_N(315,2),\
		HID_REPORT_SIZE(4),\
		HID_REPORT_COUNT(1),\
		HID_UNIT(20),\
		HID_USAGE(57),\
		HID_INPUT(66),\
		/* There's an additional nibble here that's utilized as part of the Switch Pro Controller.*/\
		/* I believe this -might- be separate U/D/L/R bits on the Switch Pro Controller, as they're utilized as four button descriptors on the Switch Pro Controller.*/\
		HID_UNIT(0),\
		HID_REPORT_COUNT(1),\
		HID_INPUT(1),\
		/* Joystick (4 bytes)*/\
		HID_LOGICAL_MAX_N(255,2),\
		HID_PHYSICAL_MAX_N(255,2),\
		HID_USAGE(48),\
		HID_USAGE(49),\
		HID_USAGE(50),\
		HID_USAGE(53),\
		HID_REPORT_SIZE(8),\
		HID_REPORT_COUNT(4),\
		HID_INPUT(2),\
		/* ??? Vendor Specific (1 byte)*/\
		/* This byte requires additional investigation.*/\
		HID_USAGE_PAGE_N(65280,2),\
		HID_USAGE(32),\
		HID_REPORT_COUNT(1),\
		HID_INPUT(2),\
		/* Output (8 bytes)*/\
		/* Original observation of this suggests it to be a mirror of the inputs that we sent.*/\
		/* The Switch requires us to have these descriptors available.*/\
		HID_USAGE_N(9761,2),\
		HID_REPORT_COUNT(8),\
		HID_OUTPUT(2),    /* 16 bit Button Map */ \
  HID_COLLECTION_END \

  
#endif /* USB_DESCRIPTORS_H_ */
