/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2023 KNfLrPn
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
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include "pico/multicore.h"
#include <string.h>
#include "bsp/board.h"
#include "tusb.h"

#include "usb_descriptors.h"
#include "SwiCC_RP2040.h"

#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/irq.h"
#include "hardware/uart.h"

//--------------------------------------------------------------------
// Global variables
//--------------------------------------------------------------------

// Controller reports and ring buffer.
USB_ControllerReport_Input_t neutral_con, current_con;
USB_ControllerReport_Input_t con_data_buff[CON_BUFF_LEN];
USB_ControllerReport_Input_t rec_data_buff[REC_BUFF_LEN];
uint8_t rec_rle_buff[REC_BUFF_LEN]; // run length encoding buffer
unsigned int queue_tail, queue_head, rec_head, stream_head;

// VSYNC timing
unsigned int frame_delay_us = 10000;
bool vsync_en = false;

// State variables
uint8_t action_mode = A_PLAY;
bool usb_connected = false;
bool led_on = true;
uint8_t vsync_count = 0;
uint8_t uart_count = 0;
uint8_t sent_count = 0;
uint8_t lag_amount = 0;
bool recording = false;
bool recording_wrap = false;

//--------------------------------------------------------------------
// Main
//--------------------------------------------------------------------
int main(void)
{
    board_init();

    // zero-out the controller buffer
    buffer_init();

    // Set up USB
    tusb_init();

    // start serial comms
    uart_setup();

    // Set up debug neopixel
    PIO pio = pio0;
    uint offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, 0, offset, 16, 800000, IS_RGBW);
    debug_pixel(urgb_u32(1, 1, 0));
    // Set up feedback neopixel
    ws2812_program_init(pio, 2, offset, 8, 800000, IS_RGBW);

    // Start second core (handles the display)
    multicore_launch_core1(core1_task);

    // Start the free-running timer
    alarm_in_us(16667);

    // Forever loop
    while (1)
    {
        tud_task(); // tinyusb device task
        hid_task();
    }

    return 0;
}

void core1_task()
{
    while (1)
    {
        if (led_on)
        {
            // Heartbeat
            uint8_t hb = ((vsync_count % 64) == 0) * 4 | ((vsync_count % 64) == 11) * 32;
            debug_pixel(urgb_u32(hb, 0, usb_connected * 16));
        }
        else
            debug_pixel(urgb_u32(0, 0, 0));

        sleep_ms(5);
    }
}

//--------------------------------------------------------------------
// Device callbacks
//--------------------------------------------------------------------

// Invoked when device is mounted
void tud_mount_cb(void)
{
    usb_connected = true;
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
    usb_connected = false;
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
    (void)remote_wakeup_en;
    usb_connected = false;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
    usb_connected = true;
}

// Invoked when sent REPORT successfully to host
// Nothing to do here, since there's only one report.
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report, uint16_t len)
{
    (void)instance;
    (void)len;
}

// Invoked when received GET_REPORT control request
// Nothing to do here; can just STALL (return 0)
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

//--------------------------------------------------------------------
// USB HID
//--------------------------------------------------------------------

void hid_task(void)
{

    // report controller data
    if (tud_hid_ready())
    {
        switch (action_mode)
        {
        case A_PLAY: // play from buffer
        case A_LAG:  // play from lag buffer
        case A_RT:   // Real-time
            tud_hid_report(0, &current_con, sizeof(USB_ControllerReport_Input_t));
            break;
        case A_STOP: // output neutral
            tud_hid_report(0, &neutral_con, sizeof(USB_ControllerReport_Input_t));
            break;

        default:
            break;
        }
    }
}

//--------------------------------------------------------------------
// UART and buffer code
//--------------------------------------------------------------------

/* Initialize the buffer and other controller variables.
 */
void buffer_init()
{
    // Set pointers
    queue_tail = 0;
    rec_head = 0;
    stream_head = 0;
    queue_head = 0;
    // Configure a neutral controller state
    neutral_con.LX = 128;
    neutral_con.LY = 128;
    neutral_con.RX = 128;
    neutral_con.RY = 128;
    neutral_con.HAT = 0x08;
    neutral_con.Button = 0;

    // Copy to initial controller state
    memcpy(&current_con, &neutral_con, sizeof(USB_ControllerReport_Input_t));

    // Copy the neutral controller into all buffer entries
    for (int i = 0; i < CON_BUFF_LEN; i++)
    {
        memcpy(&(con_data_buff[i]), &neutral_con, sizeof(USB_ControllerReport_Input_t));
    }
}

void uart_setup()
{
    // Set up UART with a basic baud rate.
    uart_init(UART_ID, BAUD_RATE);
    // Set the TX and RX pins by using the function select on the GPIO
    // Set datasheet for more information on function select
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    // Turn off UART flow control CTS/RTS
    uart_set_hw_flow(UART_ID, false, false);
    // Set data format
    uart_set_format(UART_ID, DATA_BITS, STOP_BITS, PARITY);
    // Turn on FIFO's
    uart_set_fifo_enabled(UART_ID, false);
    // Set up a RX interrupt
    int UART_IRQ = UART_ID == uart0 ? UART0_IRQ : UART1_IRQ;
    // And set up and enable the interrupt handlers
    irq_set_exclusive_handler(UART_IRQ, on_uart_rx);
    irq_set_enabled(UART_IRQ, true);
    // Eable the UART to send interrupts (on RX only)
    uart_set_irq_enables(UART_ID, true, false);
}

/* Each time a character is received, process it.
 *  Uses state in cmd_state to track what is happening.
 */
void on_uart_rx()
{
    static int cmd_state = C_IDLE;
    static char cmd_str[32];        // incoming command string
    cmd_str[31] = 0;                // Ensure null termination
    static uint8_t cmd_str_ind = 0; // index into command string

    //    board_led_write(1);
    while (uart_is_readable(UART_ID))
    {
        uint8_t ch = uart_getc(UART_ID);

        // hard force new action on command character
        if (ch == CMD_CHAR)
        {
            // reset command string
            cmd_str_ind = 0;
            memset(cmd_str, 0, 30); // Fill the string with null
            uart_count = 0;
        }
        // parse the full command on newline
        else if ((ch == '\r') || (ch == '\n'))
        {

            // ID self
            if (strncmp(cmd_str, "ID ", 3) == 0)
            {
                uart_puts(UART_ID, "+SwiCC \r\n");
            }

            // Get version
            if (strncmp(cmd_str, "VER ", 4) == 0)
            {
                uart_puts(UART_ID, "+VER 2.2\r\n");
            }

            // Add to queue
            if (strncmp(cmd_str, "Q ", 2) == 0)
            {
                add_to_queue(cmd_str + 2);
                // Assume that adding to the queue means the user wants to play the queue
                action_mode = A_PLAY;
            }

            // Add to lagged queue
            if (strncmp(cmd_str, "QL ", 3) == 0)
            {
                add_to_queue(cmd_str + 3);
                // Assume that the user wants to play lagged
                action_mode = A_LAG;
            }

            // Set the lag amount
            if (strncmp(cmd_str, "SLAG ", 5) == 0)
            {
                uint8_t old_lag = lag_amount;
                char *endptr;
                cmd_str[8] = 32; // cap numerical amount at three digits
                lag_amount = strtol(cmd_str + 5, &endptr, 10);
                if (lag_amount > 120)
                    lag_amount = 120;
                // If lag amount is being reduced, catch up queue tail
                if (lag_amount < old_lag)
                {
                    queue_tail = ((queue_head + CON_BUFF_LEN) - lag_amount) % CON_BUFF_LEN;
                }
            }

            // Immediate command
            if ((strncmp(cmd_str, "IMM ", 4) == 0))
            {
                force_con_state(cmd_str + 4);
                // Reset queue
                queue_head = 0;
                queue_tail = 0;
            }

            // Set VSYNC delay
            if (strncmp(cmd_str, "VSD ", 4) == 0)
            {
                set_frame_delay(cmd_str + 4);
            }

            // Start recording
            if (strncmp(cmd_str, "REC ", 4) == 0)
            {
                if (cmd_str[4] == '1') {
                    rec_head = 0;
                    recording_wrap = false;
                    memcpy(&(rec_data_buff[rec_head]), &current_con, sizeof(USB_ControllerReport_Input_t));
                    rec_rle_buff[rec_head] = 1;
                    recording = true;
                } else {
                    recording = false;
                }
            }


            // Get USB connection status
            if (strncmp(cmd_str, "GCS ", 4) == 0)
            {
                if (usb_connected)
                    uart_puts(UART_ID, "+GCS 1\r\n");
                else
                    uart_puts(UART_ID, "+GCS 0\r\n");
            }

            // Get queue buffer fullness
            if (strncmp(cmd_str, "GQF ", 4) == 0)
            {
                uart_resp_int("GQF", get_queue_fill());
            }

            // Get recording buffer fullness
            if (strncmp(cmd_str, "GRF ", 4) == 0)
            {
                // If recording has wrapped, it is full
                if (recording_wrap) {
                    uart_resp_int("GRF", (unsigned int)(REC_BUFF_LEN));
                } else {
                    uart_resp_int("GRF", (unsigned int)(rec_head));
                }
            }
            // Get recording buffer remaining
            if (strncmp(cmd_str, "GRR ", 4) == 0)
            {
                // If recording has wrapped, it is empty
                if (recording_wrap) {
                    uart_resp_int("GRR", (unsigned int)(0));
                } else {
                    uart_resp_int("GRR", (unsigned int)(REC_BUFF_LEN - rec_head));
                }
            }
            // Get total recording buffer size
            if (strncmp(cmd_str, "GRB ", 4) == 0)
            {
                // If recording has wrapped, it is empty
                uart_resp_int("GRB", (unsigned int)(REC_BUFF_LEN));
            }

            // Retrieve recording
            if (strncmp(cmd_str, "GR ", 3) == 0)
            {
                if (cmd_str[3] == '0') {
                    // Start at beginning
                    if (recording_wrap) {
                        // If wrapped, oldest value is just in front of head
                        stream_head = (rec_head + 1) % REC_BUFF_LEN;
                    } else {
                        stream_head = 0;
                    }
                }
                send_recording();
                if (stream_head == rec_head)
                {
                    // end of stream, entire recording has been sent
                    uart_puts(UART_ID, "+GR 0\r\n");
                }
                else
                {
                    // end of stream but more is pending
                    uart_puts(UART_ID, "+GR 1\r\n");
                }
            }

            // Enable / disable vsync synchronization
            if (strncmp(cmd_str, "VSYNC ", 6) == 0)
            {
                if (cmd_str[6] == '1')
                {
                    vsync_en = true;
                    // Set up GPIO interrupt
                    gpio_set_irq_enabled_with_callback(VSYNC_IN_PIN, GPIO_IRQ_EDGE_RISE, true, &gpio_callback);
                    vsync_count = 0;
                }
                else if (cmd_str[6] == '0')
                {
                    vsync_en = false;
                    // Disable GPIO interrupt
                    gpio_set_irq_enabled(VSYNC_IN_PIN, GPIO_IRQ_EDGE_RISE, false);
                    alarm_in_us(16666); // set an alarm 1/60s in the future
                }
                else {
                    if (vsync_en)
                        uart_puts(UART_ID, "+VSYNC 1\r\n");
                    else
                        uart_puts(UART_ID, "+VSYNC 0\r\n");
                }
            }

            // Enable / disable LED
            if (strncmp(cmd_str, "LED ", 4) == 0)
            {
                if (cmd_str[4] == '1')
                {
                    led_on = true;
                }
                else
                {
                    led_on = false;
                }
            }

            memset(cmd_str, 0, 30); // Clear command; if it wasn't valid, it never will be
        }
        else
        {
            // add chars to the string
            if (cmd_str_ind < (sizeof(cmd_str) - 1))
            {
                cmd_str[cmd_str_ind] = ch;
                cmd_str_ind++;
                uart_count++;
            }
        }
    }
}

/* Respond with an integer encoded in hex, starting with + and a header, ending with newline.
 */
void uart_resp_int(const char *header, unsigned int msg)
{
    char msgstr[5];

    sent_count++;

    sprintf(msgstr, "%04X", msg);
    if (uart_is_writable(UART_ID))
    {
        uart_putc(UART_ID, '+');
        uart_puts(UART_ID, header);
        uart_putc(UART_ID, ' ');
        uart_puts(UART_ID, msgstr);
        uart_putc(UART_ID, '\r');
        uart_putc(UART_ID, '\n');
    }
}

/* Send an entry of the recording from the stream head
 */
void send_recording_entry() {
    char msgstr[5];

    // Header
    uart_putc(UART_ID, '+');
    uart_putc(UART_ID, 'R');
    uart_putc(UART_ID, ' ');

    // Controller state
    sprintf(msgstr, "%04X", rec_data_buff[stream_head].Button);
    uart_puts(UART_ID, msgstr);

    sprintf(msgstr, "%02X", rec_data_buff[stream_head].HAT);
    uart_puts(UART_ID, msgstr);

    sprintf(msgstr, "%02X", rec_data_buff[stream_head].LX);
    uart_puts(UART_ID, msgstr);

    sprintf(msgstr, "%02X", rec_data_buff[stream_head].LY);
    uart_puts(UART_ID, msgstr);

    sprintf(msgstr, "%02X", rec_data_buff[stream_head].RX);
    uart_puts(UART_ID, msgstr);

    sprintf(msgstr, "%02X", rec_data_buff[stream_head].RY);
    uart_puts(UART_ID, msgstr);

    // RLE count
    uart_putc(UART_ID, 'x');

    sprintf(msgstr, "%02X", rec_rle_buff[stream_head]);
    uart_puts(UART_ID, msgstr);

    // Termination
    uart_putc(UART_ID, '\r');
    uart_putc(UART_ID, '\n');

}
void send_recording()
{

    for (uint8_t i = 0; i < 30 && stream_head != rec_head; i++)
    {
        send_recording_entry(stream_head);
        stream_head = (stream_head + 1) % REC_BUFF_LEN;
    }
    // Send the current controller state if needed
    if (stream_head == rec_head) {
        send_recording_entry(stream_head);
    }
}

/* Set a new amount of delay from VSYNC to controller data change.
 */
int set_frame_delay(const char *cstr)
{
    for (uint8_t i = 0; i < 4; i++)
    {
        // error on any non-hex characters
        if (!((cstr[i] >= '0' && cstr[i] <= '9') || (cstr[i] >= 'A' && cstr[i] <= 'F')))
            return -1;
    }
    frame_delay_us = hex2int(cstr, 4);
    return 0;
}

/* Add a new controller state to the buffer.
 *  Incoming data is a hex-encoded string.
 */
int add_to_queue(const char *cstr)
{

    for (uint8_t i = 0; i < 6; i++)
    {
        // error on any non-hex characters
        if (!((cstr[i] >= '0' && cstr[i] <= '9') || (cstr[i] >= 'A' && cstr[i] <= 'F')))
            return -1;
    }

    // If normal queueing, increment the head, wrapping if needed.  In lag mode,
    // head stays the same.
    if (action_mode == A_PLAY)
        queue_head = (queue_head + 1) % CON_BUFF_LEN;

    // Add the new data to the head
    con_data_buff[queue_head].Button = hex2int(cstr + 0, 4);
    con_data_buff[queue_head].HAT = hex2int(cstr + 4, 2);

    bool hasStick = true;
    for (uint8_t i = 7; i < 14; i++)
    {
        // error on any non-hex characters
        if (!((cstr[i] >= '0' && cstr[i] <= '9') || (cstr[i] >= 'A' && cstr[i] <= 'F')))
            hasStick = false;
    }

    if (hasStick)
    {
        con_data_buff[queue_head].LX = hex2int(cstr + 6, 2);
        con_data_buff[queue_head].LY = hex2int(cstr + 8, 2);
        con_data_buff[queue_head].RX = hex2int(cstr + 10, 2);
        con_data_buff[queue_head].RY = hex2int(cstr + 12, 2);
    }
    else
    {
        con_data_buff[queue_head].LX = 0x80;
        con_data_buff[queue_head].LY = 0x80;
        con_data_buff[queue_head].RX = 0x80;
        con_data_buff[queue_head].RY = 0x80;
    }

    return get_queue_fill();
}

/* Returns the amount of space currently used in the playback buffer.
 */
unsigned int get_queue_fill()
{
    // Account for the fact that the buffer wraps around.
    if (queue_head >= queue_tail)
    {
        return (unsigned int)(queue_head - queue_tail);
    }
    else
    {
        return (unsigned int)(CON_BUFF_LEN - (queue_tail - queue_head));
    }
}


/* Set a new forced controller state (aka an immediate state).
 *  Data is a hex-encoded string.
 */
int force_con_state(const char *cstr)
{
    for (uint8_t i = 0; i < 6; i++)
    {
        // error on any non-hex characters in mandatory bytes
        if (!((cstr[i] >= '0' && cstr[i] <= '9') || (cstr[i] >= 'A' && cstr[i] <= 'F')))
            return -1;
    }

    // Assume that writing an immediate means the user wants to enter a real-time mode
    action_mode = A_RT;

    // Write the data to the controller state variable.
    current_con.Button = hex2int(cstr + 0, 4);
    current_con.HAT = hex2int(cstr + 4, 2);

    bool hasStick = true;
    for (uint8_t i = 0; i < 14; i++)
    {
        // error on any non-hex characters
        if (!((cstr[i] >= '0' && cstr[i] <= '9') || (cstr[i] >= 'A' && cstr[i] <= 'F')))
            hasStick = false;
    }

    if (hasStick)
    {
        current_con.LX = hex2int(cstr + 6, 2);
        current_con.LY = hex2int(cstr + 8, 2);
        current_con.RX = hex2int(cstr + 10, 2);
        current_con.RY = hex2int(cstr + 12, 2);
    }
    else
    {
        current_con.LX = 0x80;
        current_con.LY = 0x80;
        current_con.RX = 0x80;
        current_con.RY = 0x80;
    }

    return get_queue_fill();
}

//--------------------------------------------------------------------
// Timer code
//--------------------------------------------------------------------

/* Alarm interrupt handler.
   This happens once per game frame, and is used to update the USB data.
*/
static void alarm_irq(void)
{
    // Clear the alarm irq
    hw_clear_bits(&timer_hw->intr, 1u << 0);
    if (!vsync_en)
    {
        alarm_in_us(16667); // set an alarm 1/60s in the future
        vsync_count++;
    }

    // If playing back, move the queue pointers and send the next entry
    if (action_mode == A_PLAY)
    {
        // Increment tail as long as buffer isn't empty, wrapping when needed
        if (queue_tail != queue_head)
        {
            queue_tail = (queue_tail + 1) % CON_BUFF_LEN;
        }
        // Copy the current entry to the USB data
        memcpy(&current_con, &(con_data_buff[queue_tail]), sizeof(USB_ControllerReport_Input_t));
    }
    // If playing in lag mode, move the queue pointers and send the next entry.
    else if (action_mode == A_LAG)
    {
        unsigned int old_head = queue_head;
        // Copy the current entry to the USB data
        memcpy(&current_con, &(con_data_buff[queue_tail]), sizeof(USB_ControllerReport_Input_t));
        // Increment the head pointer, and increment the tail if needed to maintain lag amount.
        queue_head = (queue_head + 1) % CON_BUFF_LEN;
        if (queue_tail < queue_head)
        { // Head is not wrapped
            if ((queue_head - queue_tail) > lag_amount)
            { // lag at limit; tail needs to keep up
                queue_tail = (queue_tail + 1) % CON_BUFF_LEN;
            }
        }
        else
        { // Head is wrapped; need to account for it.
            if (((CON_BUFF_LEN + queue_head) - queue_tail) > lag_amount)
            {
                queue_tail = (queue_tail + 1) % CON_BUFF_LEN;
            }
        }
        // Copy the old head data to the new head
        memcpy(&(con_data_buff[queue_head]), &(con_data_buff[old_head]), sizeof(USB_ControllerReport_Input_t));
    }

    // If recording, copy real-time buffer to record buffer
    if (recording)
    {
        // Implement run-length encoding.
        if ((rec_rle_buff[rec_head] < 240) && (are_cons_equal(rec_data_buff[rec_head], current_con))) {
            // One more of the same
            rec_rle_buff[rec_head] += 1;
        } else {
            // Controller data has changed, or max rle length reached.
            // increment index
            rec_head++;
            if (rec_head == REC_BUFF_LEN) {
                rec_head = 0;
                recording_wrap = true;
            }
            // Copy to record buffer
            memcpy(&(rec_data_buff[rec_head]), &current_con, sizeof(USB_ControllerReport_Input_t));
            rec_rle_buff[rec_head] = 1;
        }
    }
}

/* Set up an alarm in the future.
 */
static void alarm_in_us(uint32_t delay_us)
{
    // Enable the interrupt for the alarm
    hw_set_bits(&timer_hw->inte, 1u << 0);
    // Set irq handler for alarm irq
    irq_set_exclusive_handler(ALARM_IRQ, alarm_irq);
    // Enable the alarm irq
    irq_set_enabled(ALARM_IRQ, true);
    // Enable interrupt in block and at processor
    uint64_t target = timer_hw->timerawl + delay_us;

    // Write the lower 32 bits of the target time to the alarm which
    // will arm it
    timer_hw->alarm[0] = (uint32_t)target;
}

//--------------------------------------------------------------------
// GPIO code
//--------------------------------------------------------------------

/* GPIO interrupt handler
 */
void gpio_callback(uint gpio, uint32_t events)
{
    // set up an interrupt in the future to change controller data
    alarm_in_us(frame_delay_us);
    vsync_count++;
}

//--------------------------------------------------------------------
// Unused
//--------------------------------------------------------------------

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}