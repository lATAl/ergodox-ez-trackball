#include "pimoroni_trackball.h"
#include "i2c_master.h"

#ifdef DEBUG_POLLING
    #include <print.h>
#endif

static uint8_t scrolling      = 0;
static int16_t x_offset       = 0;
static int16_t y_offset       = 0;
static int16_t h_offset       = 0;
static int16_t v_offset       = 0;
static int16_t precisionSpeed = 0;

static bool dirty = false;
static bool left_down = false;
static bool right_down = false;
static bool middle_down = false;

#ifndef I2C_TIMEOUT
#    define I2C_TIMEOUT 35
#endif

void trackball_set_rgbw(uint8_t red, uint8_t green, uint8_t blue, uint8_t white) {
    uint8_t data[] = {0x00, red, green, blue, white};
    i2c_transmit(TRACKBALL_WRITE, data, sizeof(data), I2C_TIMEOUT);
}

void update_member(int8_t* member, int16_t* offset) {
    if (*offset > 127) {
        *member = 127;
        *offset -= 127;
    } else if (*offset < -127) {
        *member = -127;
        *offset += 127;
    } else {
        *member = *offset;
        *offset = 0;
    }
}

int16_t trackball_get_precision(void) { return precisionSpeed; }
void    trackball_set_precision(int16_t precision) { precisionSpeed = precision; }
bool    trackball_is_scrolling(void) { return scrolling; }
void    trackball_set_scrolling(bool scroll) { scrolling = scroll; }

void trackball_set_left(bool pressed) { 
    dirty |= pressed != left_down;
    left_down = pressed; 
}
void trackball_set_right(bool pressed) { 
    dirty |= pressed != right_down;
    right_down = pressed; 
}
void trackball_set_middle(bool pressed) { 
    dirty |= pressed != middle_down;
    middle_down = pressed; 
}

__attribute__((weak)) void pointing_device_init(void) { trackball_set_rgbw(0x00,0x00,0x00,0x00); }

inline int16_t max(int16_t x, int16_t y) {
    return x < y ? y : x;
}

uint16_t poll_timer = 0;
uint16_t poll_count = 0;
static bool trackball_pressed_prev = false;

void pointing_device_task(void) {

#ifdef DEBUG_POLLING
    if (timer_elapsed(poll_timer) >= 1000)
    {
        poll_timer = timer_read();
        uprintf("Poll count %u\n", poll_count);
        poll_count = 0;
    }
    ++poll_count;
#endif
    
    uint8_t state[5] = {};
    if (i2c_readReg(TRACKBALL_WRITE, 0x04, state, 5, I2C_TIMEOUT) == I2C_STATUS_SUCCESS) {
            int16_t x = state[2] - state[3];
            int16_t y = state[1] - state[0];
            if (x != 0 || y != 0) {
                //uprintf("x %d y %d\n", x, y);
                dirty = true;
            }
            int8_t sx = x < 0 ? -1 : 1;
            int8_t sy = y < 0 ? -1 : 1;
            if (scrolling) {
                x = x * x / 2 * (1 + precisionSpeed) * sx;
                y = y * y / 2 * (1 + precisionSpeed) * sy;
                h_offset -= x;
                v_offset += y;
            } else {
                // This seems to feel OK if mouse acceleration is turned on
                // Seems to be okay at the same mouse acceleration as the
                // laptop trackpad
                x = x * x * (1 + precisionSpeed) * sx;
                y = y * y * (1 + precisionSpeed) * sy;

                /* 2^x doesn't feel that great
                if (x != 0) {
                    x = abs(x) & 0xF;
                    x = ((1 << (x-1)) + precisionSpeed*x) * sx;
                }

                if (y != 0) {
                    y = abs(y) & 0xF;
                    y = ((1 << (y-1)) + precisionSpeed*y) * sy;
                }
                */

                // Mouse is rotated a bit
                x_offset -= x;
                y_offset -= y;
            }
    }

    const bool trackball_pressed = state[4] & (1 << 7);
    dirty |= trackball_pressed_prev != trackball_pressed;
    trackball_pressed_prev = trackball_pressed;
    // If we don't need to send a report, don't.
    // Seems to speed up the keyboard polling
    // by 6-7x from 125hz (USB polling speed)
    // to ~680hz. Granted it lowers back down
    // when using the mouse. But should make
    // the keyboard a bit more responsive when
    // not using the mouse.
    if (dirty) {
        dirty = false;

        report_mouse_t mouse = pointing_device_get_report();

        if (trackball_pressed || left_down) {
            mouse.buttons |= MOUSE_BTN1;
        } else {
            mouse.buttons &= ~MOUSE_BTN1;
        }

        if (right_down) {
            mouse.buttons |= MOUSE_BTN2;
        } else {
            mouse.buttons &= ~MOUSE_BTN2;
        }

        if (middle_down) {
            mouse.buttons |= MOUSE_BTN3;
        } else {
            mouse.buttons &= ~MOUSE_BTN3;
        }

        update_member(&mouse.x, &x_offset);
        update_member(&mouse.y, &y_offset);
        update_member(&mouse.h, &h_offset);
        update_member(&mouse.v, &v_offset);

        pointing_device_set_report(mouse);
        pointing_device_send();
    }
}
