/**
 * @file midi.c
 * @brief UART/TRS MIDI input handler — RX ring buffer + running-status parser +
 *        channel filter + callback dispatch. See midi.h / midi_map.h.
 *
 * Ported from the "In The Water" Daisy MidiHandler (which leaned on libDaisy's
 * MidiUartHandler). Here the transport is bare STM32 HAL: the USART1 RX ISR
 * calls midi_rx_push() per byte, and midi_poll() does the parsing/dispatch in
 * the main loop so callbacks never run in interrupt context.
 */

#include "midi.h"
#include "midi_map.h"

/* -- RX ring buffer (single-producer ISR / single-consumer poll) ------------
 * 256-byte buffer with uint8_t head/tail indices, so they wrap for free and
 * head==tail unambiguously means "empty". No locks needed: the ISR only
 * advances head, midi_poll() only advances tail. */
#define MIDI_RB_SIZE  256u
static volatile uint8_t s_rb[MIDI_RB_SIZE];
static volatile uint8_t s_head;   /* ISR writes here   */
static volatile uint8_t s_tail;   /* poll reads here   */

/* -- Parser state ----------------------------------------------------------- */
static uint8_t s_status;          /* current running status (0 = none)         */
static uint8_t s_data[2];         /* collected data bytes                       */
static uint8_t s_data_idx;        /* how many data bytes collected             */
static uint8_t s_data_needed;     /* 1 or 2 for the current status             */
static bool    s_in_sysex;        /* swallow bytes until 0xF7                   */

/* -- Channel filter + dispatch callbacks ------------------------------------ */
static uint8_t s_accept_channel;  /* 0..15 or MIDI_ACCEPT_OMNI                 */
static MidiCCHandler        s_cc_handler;
static MidiNoteHandler      s_note_handler;
static MidiPitchBendHandler s_pb_handler;

/* Channel-voice status -> number of data bytes that follow. */
static uint8_t status_data_len(uint8_t status)
{
    switch (status & 0xF0u) {
        case 0xC0u:  /* Program Change   */
        case 0xD0u:  /* Channel Pressure */
            return 1u;
        default:     /* NoteOff/On, PolyAT, CC, PitchBend */
            return 2u;
    }
}

void midi_init(void)
{
    s_head = s_tail = 0u;
    s_status = 0u;
    s_data_idx = 0u;
    s_data_needed = 0u;
    s_in_sysex = false;
    s_accept_channel = MIDI_ACCEPT_OMNI;
    s_cc_handler = 0;
    s_note_handler = 0;
    s_pb_handler = 0;
}

void midi_rx_push(uint8_t byte)
{
    uint8_t next = (uint8_t)(s_head + 1u);
    if (next != s_tail) {          /* drop on overflow rather than overwrite */
        s_rb[s_head] = byte;
        s_head = next;
    }
}

void midi_set_cc_handler(MidiCCHandler h)              { s_cc_handler = h; }
void midi_set_note_handler(MidiNoteHandler h)          { s_note_handler = h; }
void midi_set_pitchbend_handler(MidiPitchBendHandler h){ s_pb_handler = h; }

uint8_t midi_get_channel(void) { return s_accept_channel; }
void    midi_set_channel(uint8_t ch)
{
    s_accept_channel = (ch >= 16u) ? MIDI_ACCEPT_OMNI : ch;
}

/* A complete channel-voice message is ready — apply the CC#119 / panic
 * intercepts (before the channel filter), then the channel filter, then
 * dispatch to the registered callback. */
static void dispatch(uint8_t status, uint8_t d0, uint8_t d1)
{
    uint8_t type = status & 0xF0u;
    uint8_t ch   = status & 0x0Fu;

    if (type == 0xB0u) {
        /* CC#119 sets the accept channel — intercept BEFORE the channel filter
         * so you can re-channel the pedal from any channel. */
        if (d0 == MIDI_CC_CHANNEL) {
            if (d1 == 0u)        midi_set_channel(MIDI_ACCEPT_OMNI);
            else if (d1 <= 16u)  midi_set_channel((uint8_t)(d1 - 1u));
            return;
        }
        /* Panic is channel-agnostic by spec. */
        if (d0 == MIDI_CC_ALL_SOUND_OFF || d0 == MIDI_CC_ALL_NOTES_OFF) {
            if (s_cc_handler) s_cc_handler(d0, d1);
            return;
        }
    }

    if (s_accept_channel != MIDI_ACCEPT_OMNI && ch != s_accept_channel)
        return;

    switch (type) {
        case 0xB0u:  /* Control Change */
            if (s_cc_handler) s_cc_handler(d0, d1);
            break;
        case 0x90u:  /* Note On (velocity 0 == Note Off per spec) */
            if (s_note_handler) s_note_handler(d0, d1, d1 > 0u);
            break;
        case 0x80u:  /* Note Off */
            if (s_note_handler) s_note_handler(d0, d1, false);
            break;
        case 0xE0u:  /* Pitch Bend: 14-bit, centered, -8192..+8191 */
            if (s_pb_handler)
                s_pb_handler((int16_t)(((int16_t)d1 << 7) | (int16_t)d0) - 8192);
            break;
        default:     /* Program Change, Channel/Poly Pressure: ignored */
            break;
    }
}

static void parse_byte(uint8_t b)
{
    if (b & 0x80u) {                 /* ---- status byte ---- */
        if (b >= 0xF8u) {
            /* System Real-Time (clock/start/stop/...). Interleaves anywhere and
             * does NOT clear running status. No tempo subsystem here -> ignore. */
            return;
        }
        if (b == 0xF0u) {            /* SysEx start */
            s_in_sysex = true;
            s_status = 0u;
            return;
        }
        if (b == 0xF7u) {            /* SysEx end */
            s_in_sysex = false;
            s_status = 0u;
            return;
        }
        if (b >= 0xF1u && b <= 0xF6u) {
            /* System Common — clears running status; we don't use these. */
            s_status = 0u;
            s_data_idx = 0u;
            return;
        }
        /* Channel-voice status: latch it for running status. */
        s_status = b;
        s_data_idx = 0u;
        s_data_needed = status_data_len(b);
        return;
    }

    /* ---- data byte ---- */
    if (s_in_sysex)   return;        /* swallow SysEx payload          */
    if (s_status == 0u) return;      /* orphan data with no status     */

    s_data[s_data_idx++] = b;
    if (s_data_idx >= s_data_needed) {
        dispatch(s_status, s_data[0], (s_data_needed == 2u) ? s_data[1] : 0u);
        s_data_idx = 0u;             /* running status: ready for the next msg */
    }
}

void midi_poll(void)
{
    while (s_tail != s_head) {
        uint8_t b = s_rb[s_tail];
        s_tail = (uint8_t)(s_tail + 1u);
        parse_byte(b);
    }
}
