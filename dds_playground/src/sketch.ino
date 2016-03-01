#include <Streaming.h>
#include <MIDI.h>
#include <LiquidCrystal.h>
#include <avr/pgmspace.h>

#define u8 uint8_t
#define u16 uint16_t

#define clear_bit(sfr, bit) (_SFR_BYTE(sfr) &= ~_BV(bit))
#define set_bit(sfr, bit)   (_SFR_BYTE(sfr) |= _BV(bit))

#define EVERY(ms) if (millis() % ms == 0)
#define _EVERY(ms) (millis() % ms == 0)

#define ISR_DBG_PIN_ON set_bit(PORTB, PB1)
#define ISR_DBG_PIN_OFF clear_bit(PORTB, PB1)
#define ISR_DBG_PIN_INIT set_bit(DDRB, PB1)

#define AUDIOPIN 11

#define SAMPLE_RATE 8000UL
#define ACCUMULATOR_STEPS 2048

#include "wavetables.h"
#include "tuning.h"
#include "effects.h"

#define N_WAVETABLES 6
Wavetable wavetables[N_WAVETABLES];
u8 used_wavetables = 0;

u8 globaleffects = 0;
struct Lfo lfo = {.rate = 0.05, .p = 0, .amp = 800};
struct Arpeggiator arp = {.period = 10};

MIDI_CREATE_DEFAULT_INSTANCE();

LiquidCrystal lcd(7, 8, 5, 4, 3, 2);

void setup() {
    ISR_DBG_PIN_INIT;
    pinMode(13, OUTPUT);

    MIDI.setHandleNoteOn(handleNoteOn);
    MIDI.setHandleNoteOff(handleNoteOff);

    MIDI.begin(MIDI_CHANNEL_OMNI);

    lcd.begin(16, 2);
    lcd.createChar(ARPEGGIATOR_ICON, arpeggiator_icon);

    initialize_datatables();
    for (int i=0; i<N_WAVETABLES; i++) {
        wt_init(&wavetables[i],
            wt_sine_data,
            WT_SINE_LENGTH,
            WT_SINE_BASEFREQ);
        wavetables[i].increment = 0; // all oscillators silent
    }

    globaleffects |= ARPEGGIATOR;

    setup_sample_timer();
    setup_pwm_audio_timer();
}

void loop() {
    MIDI.read();

    EVERY(250) {
        lcd.setCursor(0, 0);

        if (EFFECT_ON(LFO, globaleffects))
            lcd.write('l');
        else
            lcd.write(' ');

        if (EFFECT_ON(ARPEGGIATOR, globaleffects))
            lcd.write(ARPEGGIATOR_ICON);
        else
            lcd.write(' ');

        u8 tablemask = 0x00;
        for (int i=0; i<N_WAVETABLES; i++) {
            if (wavetables[i].currentnote != NONOTE) {
                tablemask |= (1 << i);
            }
        }

        lcd.write('|');
        lcd.print(tablemask, HEX);
        lcd.write('|');
        lcd.print(used_wavetables);
        lcd.print("  ");
    }

    if (EFFECT_ON(LFO, globaleffects) && _EVERY(20)) {
        int lfo_value = update_lfo(&lfo) * lfo.amp;
        for (int i=0; i<N_WAVETABLES; i++) {
            if (wavetables[i].currentnote <= 127) {
                wavetables[i].increment = note_to_increment(wavetables[i].currentnote, wavetables[i].basefreq) + lfo_value;
            }
        }
    }

    if (EFFECT_ON(ARPEGGIATOR, globaleffects) && _EVERY(arp.period)) {
        for (int i=0; i<N_WAVETABLES; i++) {
            if (wavetables[i].currentnote <= 127) {
            }
        }
    }
}

void handleNoteOn(u8 channel, u8 pitch, u8 velocity) {
    digitalWrite(13, HIGH);
    for (int i=0; i<N_WAVETABLES; i++) {
        if (wavetables[i].currentnote == NONOTE) {
            wavetables[i].increment = note_to_increment(pitch, wavetables[i].basefreq);
            wavetables[i].currentnote = pitch;
            used_wavetables++;
            break;
        }
    }
}

void handleNoteOff(u8 channel, u8 pitch, u8 velocity) {
    digitalWrite(13, LOW);
    for (int i=0; i<N_WAVETABLES; i++) {
        if (wavetables[i].currentnote == pitch) {
            wavetables[i].currentnote = NOTEOFF;
            used_wavetables--;
        }
    }
}

ISR(TIMER1_COMPA_vect) {
    ISR_DBG_PIN_ON;

    int sum = 0;

    for (int i=0; i<N_WAVETABLES; i++) {
        if (wavetables[i].currentnote == NOTEOFF) {
            wavetables[i].accumulator = 0;
            wavetables[i].position = 0;
            wavetables[i].increment = 0;
            wavetables[i].currentnote = NONOTE;
        } else if (wavetables[i].currentnote != NONOTE) {
            sum += wavetables[i].wave[wavetables[i].position];

            wavetables[i].accumulator += wavetables[i].increment;
            wavetables[i].position += wavetables[i].accumulator / ACCUMULATOR_STEPS;
            wavetables[i].accumulator %= ACCUMULATOR_STEPS;
            if (wavetables[i].position > wavetables[i].len - 1) {
                wavetables[i].position -= wavetables[i].len;
            }
        }
    }

    sum /= used_wavetables;

    OCR2A = (u16) (sum + 128);

    ISR_DBG_PIN_OFF;
}

inline void setup_sample_timer() {
    // undo arduino defaults
    TCCR1A = 0;
    TCCR1B = 0;
    TIMSK1 = 0;

    // set up count-until-clear mode
    set_bit(TCCR1B, WGM12);

    // sync to CPU clock (fast)
    set_bit(TCCR1B, CS10);

    // enable output-compare interrupt
    set_bit(TIMSK1, OCIE1A);
    
    OCR1A = F_CPU / SAMPLE_RATE - 1;

    sei();
}

inline void setup_pwm_audio_timer() {
    // undo Arduino defaults
    TCCR2A = 0;
    TCCR2B = 0;

    // fast pwm
    TCCR2A |= _BV(WGM21) | _BV(WGM20);

    // output on pin 11
    set_bit(TCCR2A, COM2A1);
    
    // fastest possible clock speed (~62kHz @ 16MHz sysosc)
    set_bit(TCCR2B, CS20);

    // top value for the counter (signal is symmetric around 127)
    OCR2A = 127;

    // set pin 11 as an output
    set_bit(DDRB, PB3);
}
