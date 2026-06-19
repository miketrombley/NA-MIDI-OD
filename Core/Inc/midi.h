#ifndef MIDI_H
#define MIDI_H

/**
 * @file midi.h
 * @brief Minimal UART/TRS MIDI input handler (C/HAL port of the "In The Water"
 *        Daisy MidiHandler).
 *
 * Transport-agnostic: the USART1 RX ISR feeds raw bytes in via
 * @ref midi_rx_push (lock-free single-producer ring buffer); the main loop
 * drains, parses (running-status state machine), channel-filters, and
 * dispatches to registered callbacks via @ref midi_poll.
 *
 * Wiring on MIDI-OD: USART1 RX (PA10) @ 31250 baud, 8-N-1. The TRS jack's
 * MIDI/expression mux is selected by EXP_MIDI_CNTRL (PA8). Only channel-voice
 * Control Change / Note On-Off / Pitch Bend are dispatched; System Real-Time
 * (clock) and System Common are parsed-and-skipped. See midi_map.h for the CC
 * numbers and main.c for how CCs drive the VCAs / bias / bypass.
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MIDI_ACCEPT_OMNI  0xFFu   /* accept_channel sentinel: listen on all 16 */

/* Dispatch callbacks (function pointers; NULL = ignore that message type). */
typedef void (*MidiCCHandler)(uint8_t cc, uint8_t value);          /* value 0..127 */
typedef void (*MidiNoteHandler)(uint8_t note, uint8_t velocity, bool on);
typedef void (*MidiPitchBendHandler)(int16_t value);               /* -8192..+8191 */

/** Reset the parser + ring buffer and clear all handlers. Call once at boot
 *  BEFORE enabling the USART1 RX interrupt. Boots omni (all channels). */
void midi_init(void);

/** Push one received byte into the RX ring buffer. Call from the USART1 RX ISR
 *  only (single producer). Drops the byte if the buffer is full. */
void midi_rx_push(uint8_t byte);

/** Drain the ring buffer, parse, and dispatch any complete messages. Call
 *  every main-loop iteration. Runs in thread context (not the ISR). */
void midi_poll(void);

void midi_set_cc_handler(MidiCCHandler h);
void midi_set_note_handler(MidiNoteHandler h);
void midi_set_pitchbend_handler(MidiPitchBendHandler h);

/** Accept channel: 0..15 to lock to one channel, or MIDI_ACCEPT_OMNI for all.
 *  Also settable over MIDI via CC#119 (0 = omni, 1-16 = channel). */
uint8_t midi_get_channel(void);
void    midi_set_channel(uint8_t ch);

#ifdef __cplusplus
}
#endif

#endif /* MIDI_H */
