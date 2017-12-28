#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#include "cdnw.h"

static int screen_x = 0;
static int screen_y = 0;
static int screen_width = 80;
static int screen_height = 25;

static uint8_t current_attribute_byte = 0x07;

static void kputc_at(char ch, int x, int y) {
    ((volatile uint8_t *)0xB8000)[(y * screen_width + x) * 2] = ch;
    ((volatile uint8_t *)0xB8000)[(y * screen_width + x) * 2 + 1] = current_attribute_byte;
}

static void do_newline(void) {
    screen_x = 0;
    screen_y += 1;
    if(screen_y >= screen_height) {
        screen_y = 0;
    }
    for(int i = 0; i < screen_width; i += 1) {
        kputc_at(' ', i, screen_y);
    }
}

static void kputc(char ch) {
    if(ch == '\n') {
        do_newline();
        return;
    }
    if(screen_x >= screen_width) {
        do_newline();
    }
    kputc_at(ch, screen_x, screen_y);
    screen_x += 1;
}

static void kputs(const char *str) {
    while(*str) {
        kputc(*str++);
    }
}

static void put_integer(unsigned long value, int radix, bool signedp, bool pad_with_zeros, int padding) {
    if(value == 0) {
        for(int i = 0; i < padding - 1; i += 1) {
            if(pad_with_zeros) {
                kputc('0');
            } else {
                kputc(' ');
            }
        }
        kputc('0');
        return;
    }
    if(signedp && (signed long)value < 0) {
        value = -value;
        kputc('-');
        padding -= 1;
    }
    char buf[33];
    int index = 32;
    buf[--index] = 0;
    while(value) {
        buf[--index] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"[value % radix];
        value /= radix;
    }
    for(int i = 0; i < padding - (31 - index); i += 1) {
        if(pad_with_zeros) {
            kputc('0');
        } else {
            kputc(' ');
        }
    }
    kputs(&buf[index]);
}

static int hex_nibble_to_int(char nibble) {
    const char *chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    int weight = 0;
    while(chars[weight] && chars[weight] != nibble) {
        weight += 1;
    }
    return weight;
}

static void kvprintf(const char *control, va_list ap) {
    while(*control) {
        if(*control == '%') {
            bool pad_with_zeros = false;
            int padding = 0;
            control++;
        again:
            switch(*control++) {
            case 0: return;
            case '0':
                if(!padding) {
                    pad_with_zeros = true;
                    goto again;
                }
                // Fall through, reading padding.
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                padding *= 10;
                padding += control[-1] - '0';
                goto again;
            case '%':
                kputc('%');
                break;
            case 'c':
                kputc(va_arg(ap, int));
                break;
            case 'x':
            case 'X':
                put_integer(va_arg(ap, int), 16, false, pad_with_zeros, padding);
                break;
            case 'u':
            case 'U':
                put_integer(va_arg(ap, int), 10, false, pad_with_zeros, padding);
                break;
            }
        } else if(*control == '^') {
            control++;
            if(*control == '^') {
                control++;
                kputc('^');
            } else if(*control == '*') {
                control++;
                current_attribute_byte = va_arg(ap, int);
            } else {
                uint8_t hi_nibble = hex_nibble_to_int(*control++);
                uint8_t lo_nibble = hex_nibble_to_int(*control++);
                current_attribute_byte = (hi_nibble << 4) | lo_nibble;
            }
        } else {
            kputc(*control++);
        }
    }
}

void kprintf(const char *control, ...) {
    va_list ap;
    va_start(ap, control);
    kvprintf(control, ap);
    va_end(ap);
}
