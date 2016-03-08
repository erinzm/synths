#ifndef __MIDI_H__
#define __MIDI_H__

#include "util.h"
#include "usart.h"
#include <avr/io.h>

#define CHECK_BIT(var,pos) ((var) & (1<<(pos)))

enum MidiState {
    MS_IDLE,
    MS_RECIEVING_DATA,
};

enum MsgType {
    BYTE_DATA,
    BYTE_STATUS,
};

enum MidiState midiState = MS_IDLE;
u8 midi_status_nybble = 0;
u8 midi_channel = 0;
u8 midi_data_bytes[2] = {0, 0};
u8 n_byte = 0;

inline void midi_note_on(u8 channel, u8 note, u8 velocity);

static inline void handleMessage() {
    switch (midi_status_nybble) {
        case 0x9:
            midi_note_on(midi_channel, midi_data_bytes[0], midi_data_bytes[1]);
            break;
        default:
            break;
    }

    toggle_bit(PORTB, PB0);

    uart_putchar('s'); uart_putchar(midi_status_nybble);
    uart_putchar('c'); uart_putchar(midi_channel);
    uart_putchar('d');
    uart_putchar(midi_data_bytes[0]);
    uart_putchar(midi_data_bytes[1]);
    uart_putchar('.');
}

static inline enum MsgType byte_type(u8 byte) {
    if (CHECK_BIT(byte, 7)) {
        return BYTE_STATUS;
    } else {
        return BYTE_DATA;
    }
}

static inline void process_status_byte(u8 byte) {
    midi_status_nybble = byte >> 4;
    midiState = MS_RECIEVING_DATA;
}

static inline void process_data_byte(u8 byte) {
    midi_data_bytes[n_byte] = byte;

    if (n_byte == 1) {
        handleMessage();
        n_byte = 0;
        midiState = MS_IDLE;
        return;
    }
    n_byte++;
}

void midi_process_byte(u8 data) {
    switch (midiState) {
        case MS_IDLE:
            if (byte_type(data) == BYTE_STATUS) {
                process_status_byte(data);
            }
            break;
        case MS_RECIEVING_DATA:
            process_data_byte(data);
            break;
        default:
            break;
    }
}




#endif
