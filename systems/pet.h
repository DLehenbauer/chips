#pragma once
/*#
    # pet.h

    A Commodore PET 4032 emulator in a C header.

    Do this:
    ~~~C
    #define CHIPS_IMPL
    ~~~
    before you include this file in *one* C or C++ file to create the
    implementation.

    Optionally provide the following macros with your own implementation

    ~~~C
    CHIPS_ASSERT(c)
    ~~~
        your own assert macro (default: assert(c))

    You need to include the following headers before including pet.h:

    - chips/chips_common.h
    - chips/m6502.h
    - chips/m6520.h
    - chips/m6522.h
    - chips/mc6845.h
    - chips/beeper.h
    - chips/kbd.h
    - chips/mem.h
    - chips/clk.h

    ## The Commodore PET 4032

    The PET 4032 is a member of Commodore's "40-column" PET/CBM line:

    - MOS 6502 CPU at 1 MHz
    - 32 KByte RAM
    - 1 KByte screen RAM at 0x8000 (40x25 characters)
    - BASIC 4.0, 40-column editor and KERNAL 4.0 ROMs
    - MC6845 (6545) CRT controller for video timing
    - two MOS 6520 PIAs and one MOS 6522 VIA for I/O
    - graphics keyboard (10x8 matrix)

    The emulated machine corresponds to a 4032 with the CRTC editor
    ROM (901499-01, 60Hz) and the graphics character ROM (901447-10).

    ## zlib/libpng license

    Copyright (c) 2024 Andre Weissflog
    This software is provided 'as-is', without any express or implied warranty.
    In no event will the authors be held liable for any damages arising from the
    use of this software.
    Permission is granted to anyone to use this software for any purpose,
    including commercial applications, and to alter it and redistribute it
    freely, subject to the following restrictions:
        1. The origin of this software must not be misrepresented; you must not
        claim that you wrote the original software. If you use this software in a
        product, an acknowledgment in the product documentation would be
        appreciated but is not required.
        2. Altered source versions must be plainly marked as such, and must not
        be misrepresented as being the original software.
        3. This notice may not be removed or altered from any source
        distribution.
#*/
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdalign.h>

#ifdef __cplusplus
extern "C" {
#endif

// bump snapshot version when pet_t memory layout changes
#define PET_SNAPSHOT_VERSION (1)

#define PET_FREQUENCY (1000000)
#define PET_MAX_AUDIO_SAMPLES (1024)
#define PET_DEFAULT_AUDIO_SAMPLES (128)

// PET 4032 framebuffer dimensions
#define PET_FRAMEBUFFER_WIDTH (512)
#define PET_FRAMEBUFFER_HEIGHT (256)
#define PET_FRAMEBUFFER_SIZE_BYTES (PET_FRAMEBUFFER_WIDTH * PET_FRAMEBUFFER_HEIGHT)
#define PET_DISPLAY_WIDTH (320)
#define PET_DISPLAY_HEIGHT (200)

// config parameters for pet_init()
typedef struct {
    chips_debug_t debug;        // optional debugging hook
    chips_audio_desc_t audio;
    // ROM images
    struct {
        chips_range_t chars;    // 2 KByte character ROM (901447-10)
        chips_range_t basic;    // 12 KByte BASIC 4.0 ROM (901465-23/-20/-21)
        chips_range_t editor;   // 2 KByte editor ROM (901499-01)
        chips_range_t kernal;   // 4 KByte KERNAL 4.0 ROM (901465-22)
    } roms;
} pet_desc_t;

// PET emulator state
typedef struct {
    m6502_t cpu;
    m6520_t pia_1;
    m6520_t pia_2;
    m6522_t via;
    mc6845_t crtc;
    beeper_t beeper;
    uint64_t pins;

    kbd_t kbd;                  // keyboard matrix state
    mem_t mem;                  // CPU-visible memory mapping
    bool valid;
    bool vsync_prev;            // previous CRTC VSYNC state (for edge detect)
    bool chars_lowercase;       // character set selected via VIA CA2
    chips_debug_t debug;

    struct {
        chips_audio_callback_t callback;
        int num_samples;
        int sample_pos;
        float sample_buffer[PET_MAX_AUDIO_SAMPLES];
    } audio;

    uint8_t ram[0x8000];        // 32 KB RAM
    uint8_t vram[0x0400];       // 1 KB screen RAM at 0x8000
    uint8_t rom_basic[0x3000];  // 12 KB BASIC 4.0
    uint8_t rom_editor[0x0800]; // 2 KB editor
    uint8_t rom_kernal[0x1000]; // 4 KB KERNAL 4.0
    uint8_t rom_char[0x0800];   // 2 KB character ROM
    alignas(64) uint8_t fb[PET_FRAMEBUFFER_SIZE_BYTES];
} pet_t;

// initialize a new PET instance
void pet_init(pet_t* sys, const pet_desc_t* desc);
// discard a PET instance
void pet_discard(pet_t* sys);
// reset a PET instance
void pet_reset(pet_t* sys);
// query display information
chips_display_info_t pet_display_info(pet_t* sys);
// tick the PET instance for a given number of microseconds, return number of ticks
uint32_t pet_exec(pet_t* sys, uint32_t micro_seconds);
// send a key-down event
void pet_key_down(pet_t* sys, int key_code);
// send a key-up event
void pet_key_up(pet_t* sys, int key_code);
// take a snapshot, patches pointers to zero, returns snapshot version
uint32_t pet_save_snapshot(pet_t* sys, pet_t* dst);
// load a snapshot, returns false if snapshot version doesn't match
bool pet_load_snapshot(pet_t* sys, uint32_t version, pet_t* src);

#ifdef __cplusplus
} // extern "C"
#endif

/*-- IMPLEMENTATION ----------------------------------------------------------*/
#ifdef CHIPS_IMPL
#include <string.h>
#ifndef CHIPS_ASSERT
    #include <assert.h>
    #define CHIPS_ASSERT(c) assert(c)
#endif

#define _PET_DEFAULT(val,def) (((val) != 0) ? (val) : (def))

static void _pet_init_key_map(pet_t* sys);

void pet_init(pet_t* sys, const pet_desc_t* desc) {
    CHIPS_ASSERT(sys && desc);
    if (desc->debug.callback.func) { CHIPS_ASSERT(desc->debug.stopped); }

    memset(sys, 0, sizeof(pet_t));
    sys->valid = true;
    sys->debug = desc->debug;
    sys->audio.callback = desc->audio.callback;
    sys->audio.num_samples = _PET_DEFAULT(desc->audio.num_samples, PET_DEFAULT_AUDIO_SAMPLES);
    CHIPS_ASSERT(sys->audio.num_samples <= PET_MAX_AUDIO_SAMPLES);

    // copy ROM images
    CHIPS_ASSERT(desc->roms.chars.ptr && (desc->roms.chars.size == sizeof(sys->rom_char)));
    CHIPS_ASSERT(desc->roms.basic.ptr && (desc->roms.basic.size == sizeof(sys->rom_basic)));
    CHIPS_ASSERT(desc->roms.editor.ptr && (desc->roms.editor.size == sizeof(sys->rom_editor)));
    CHIPS_ASSERT(desc->roms.kernal.ptr && (desc->roms.kernal.size == sizeof(sys->rom_kernal)));
    memcpy(sys->rom_char, desc->roms.chars.ptr, sizeof(sys->rom_char));
    memcpy(sys->rom_basic, desc->roms.basic.ptr, sizeof(sys->rom_basic));
    memcpy(sys->rom_editor, desc->roms.editor.ptr, sizeof(sys->rom_editor));
    memcpy(sys->rom_kernal, desc->roms.kernal.ptr, sizeof(sys->rom_kernal));

    // initialize the hardware
    sys->pins = m6502_init(&sys->cpu, &(m6502_desc_t){0});
    m6520_init(&sys->pia_1);
    m6520_init(&sys->pia_2);
    m6522_init(&sys->via);
    mc6845_init(&sys->crtc, MC6845_TYPE_MC6845);
    beeper_init(&sys->beeper, &(beeper_desc_t){
        .tick_hz = PET_FREQUENCY,
        .sound_hz = _PET_DEFAULT(desc->audio.sample_rate, 44100),
        .base_volume = _PET_DEFAULT(desc->audio.volume, 0.5f),
    });

    /* setup the CPU memory map:
        0000..7FFF      32 KB RAM
        8000..83FF      1 KB screen RAM
        9000..AFFF      expansion ROM sockets (unmapped)
        B000..DFFF      12 KB BASIC 4.0
        E000..E7FF      2 KB editor ROM
        E800..E8FF      I/O (handled directly in the tick function)
        F000..FFFF      4 KB KERNAL 4.0
    */
    mem_init(&sys->mem);
    mem_map_ram(&sys->mem, 0, 0x0000, 0x8000, sys->ram);
    mem_map_ram(&sys->mem, 0, 0x8000, 0x0400, sys->vram);
    mem_map_rom(&sys->mem, 0, 0xB000, 0x3000, sys->rom_basic);
    mem_map_rom(&sys->mem, 0, 0xE000, 0x0800, sys->rom_editor);
    mem_map_rom(&sys->mem, 0, 0xF000, 0x1000, sys->rom_kernal);

    _pet_init_key_map(sys);
}

void pet_discard(pet_t* sys) {
    CHIPS_ASSERT(sys && sys->valid);
    sys->valid = false;
}

void pet_reset(pet_t* sys) {
    CHIPS_ASSERT(sys && sys->valid);
    m6520_reset(&sys->pia_1);
    m6520_reset(&sys->pia_2);
    m6522_reset(&sys->via);
    mc6845_reset(&sys->crtc);
    beeper_reset(&sys->beeper);
    sys->vsync_prev = false;
    sys->chars_lowercase = false;
    sys->pins |= M6502_RES;
}

// decode the whole framebuffer from screen RAM and the character ROM
static void _pet_decode_vidmem(pet_t* sys) {
    // honor the CRTC start address and geometry registers, but clamp
    // everything to the visible 40x25 display
    uint16_t start = ((sys->crtc.start_addr_hi << 8) | sys->crtc.start_addr_lo) & 0x03FF;
    int cols = sys->crtc.h_displayed;
    int rows = sys->crtc.v_displayed;
    if ((cols <= 0) || (cols > 40)) { cols = 40; }
    if ((rows <= 0) || (rows > 25)) { rows = 25; }
    // graphics charset in lower half, lowercase charset in upper half
    const uint8_t* font = sys->rom_char + (sys->chars_lowercase ? 0x0400 : 0x0000);
    uint16_t addr = start;
    for (int row = 0; row < 25; row++) {
        for (int line = 0; line < 8; line++) {
            uint8_t* dst = &sys->fb[(row * 8 + line) * PET_FRAMEBUFFER_WIDTH];
            for (int col = 0; col < 40; col++, dst += 8) {
                uint8_t pixels = 0;
                if ((col < cols) && (row < rows)) {
                    uint8_t code = sys->vram[(addr + col) & 0x03FF];
                    pixels = font[((code & 0x7F) << 3) | line];
                    if (code & 0x80) {
                        pixels = ~pixels;
                    }
                }
                for (int p = 0; p < 8; p++) {
                    dst[p] = (pixels & (0x80 >> p)) ? 1 : 0;
                }
            }
        }
        addr += cols;
    }
}

static uint64_t _pet_tick(pet_t* sys, uint64_t pins) {
    // tick the CPU
    pins = m6502_tick(&sys->cpu, pins);
    const uint16_t addr = M6502_GET_ADDR(pins);

    // tick the CRTC and detect a VSYNC rising edge to render a frame
    uint64_t crtc_pins = mc6845_tick(&sys->crtc);
    const bool vsync = 0 != (crtc_pins & MC6845_VS);
    if (vsync && !sys->vsync_prev) {
        _pet_decode_vidmem(sys);
    }
    sys->vsync_prev = vsync;

    // the IRQ pin is driven by the two PIAs and the VIA each tick
    pins &= ~M6502_IRQ;

    const bool io_access = (addr & 0xFF00) == 0xE800;

    // regular memory access (everything except the I/O page)
    if (!io_access) {
        if (pins & M6502_RW) {
            M6502_SET_DATA(pins, mem_rd(&sys->mem, addr));
        }
        else {
            mem_wr(&sys->mem, addr, M6502_GET_DATA(pins));
        }
    }

    // tick PIA-1 (keyboard, screen retrace IRQ)
    {
        uint64_t pia1_pins = pins & (M6502_RW | 0xFF0000ULL | M6520_RS0 | M6520_RS1);
        // keyboard: PIA-1 PA outputs a row number (0..9), decoded to one of
        // ten one-hot column lines; the pressed keys are read back on PB.
        uint8_t kbd_sel = (sys->pia_1.pa.outr & sys->pia_1.pa.ddr) & 0x0F;
        kbd_set_active_columns(&sys->kbd, (kbd_sel < 10) ? (1 << kbd_sel) : 0);
        uint8_t kbd_lines = ~((uint8_t)kbd_scan_lines(&sys->kbd));
        // PA inputs (diagnostic sense etc.) read high
        M6520_SET_PA(pia1_pins, 0xFF);
        M6520_SET_PB(pia1_pins, kbd_lines);
        // CB1 = screen retrace (60 Hz) drives the system IRQ
        if (vsync) {
            pia1_pins |= M6520_CB1;
        }
        if (io_access && (addr & 0x10)) {
            pia1_pins |= M6520_CS;
        }
        pia1_pins = m6520_tick(&sys->pia_1, pia1_pins);
        if ((pia1_pins & (M6520_CS | M6520_RW)) == (M6520_CS | M6520_RW)) {
            pins = M6502_COPY_DATA(pins, pia1_pins);
        }
        if (pia1_pins & M6520_IRQ) {
            pins |= M6502_IRQ;
        }
    }

    // tick PIA-2 (IEEE-488 bus, not connected to anything here)
    {
        uint64_t pia2_pins = pins & (M6502_RW | 0xFF0000ULL | M6520_RS0 | M6520_RS1);
        M6520_SET_PA(pia2_pins, 0xFF);
        M6520_SET_PB(pia2_pins, 0xFF);
        if (io_access && (addr & 0x20)) {
            pia2_pins |= M6520_CS;
        }
        pia2_pins = m6520_tick(&sys->pia_2, pia2_pins);
        if ((pia2_pins & (M6520_CS | M6520_RW)) == (M6520_CS | M6520_RW)) {
            pins = M6502_COPY_DATA(pins, pia2_pins);
        }
        if (pia2_pins & M6520_IRQ) {
            pins |= M6502_IRQ;
        }
    }

    // tick the VIA (CB2 sound output, CA2 selects the character set)
    {
        uint64_t via_pins = pins & (M6502_RW | 0xFF0000ULL | M6522_RS_PINS);
        M6522_SET_PA(via_pins, 0xFF);
        M6522_SET_PB(via_pins, 0xFF);
        if (io_access && (addr & 0x40)) {
            via_pins |= M6522_CS1;
        }
        via_pins = m6522_tick(&sys->via, via_pins);
        if ((via_pins & (M6522_CS1 | M6522_RW)) == (M6522_CS1 | M6522_RW)) {
            pins = M6502_COPY_DATA(pins, via_pins);
        }
        if (via_pins & M6522_IRQ) {
            pins |= M6502_IRQ;
        }
        // CA2 high selects the graphics character set, low selects lowercase
        sys->chars_lowercase = 0 == (via_pins & M6522_CA2);
        // CB2 drives the speaker
        beeper_set(&sys->beeper, 0 != (via_pins & M6522_CB2));
    }

    // CRTC register access at 0xE880 (address) / 0xE881 (data)
    if (io_access && (addr & 0x80)) {
        uint64_t cpins = (pins & (0xFF0000ULL | M6502_RW)) | MC6845_CS;
        if (pins & M6502_RW) { cpins |= MC6845_RW; }
        if (addr & 0x01)     { cpins |= MC6845_RS; }
        cpins = mc6845_iorq(&sys->crtc, cpins);
        if (pins & M6502_RW) {
            pins = M6502_COPY_DATA(pins, cpins);
        }
    }

    // tick the speaker
    if (beeper_tick(&sys->beeper)) {
        sys->audio.sample_buffer[sys->audio.sample_pos++] = sys->beeper.sample;
        if (sys->audio.sample_pos == sys->audio.num_samples) {
            if (sys->audio.callback.func) {
                sys->audio.callback.func(sys->audio.sample_buffer, sys->audio.num_samples, sys->audio.callback.user_data);
            }
            sys->audio.sample_pos = 0;
        }
    }
    return pins;
}

uint32_t pet_exec(pet_t* sys, uint32_t micro_seconds) {
    CHIPS_ASSERT(sys && sys->valid);
    uint32_t num_ticks = clk_us_to_ticks(PET_FREQUENCY, micro_seconds);
    uint64_t pins = sys->pins;
    if (0 == sys->debug.callback.func) {
        for (uint32_t tick = 0; tick < num_ticks; tick++) {
            pins = _pet_tick(sys, pins);
        }
    }
    else {
        for (uint32_t tick = 0; (tick < num_ticks) && !(*sys->debug.stopped); tick++) {
            pins = _pet_tick(sys, pins);
            sys->debug.callback.func(sys->debug.callback.user_data, pins);
        }
    }
    sys->pins = pins;
    kbd_update(&sys->kbd, micro_seconds);
    return num_ticks;
}

void pet_key_down(pet_t* sys, int key_code) {
    CHIPS_ASSERT(sys && sys->valid);
    kbd_key_down(&sys->kbd, key_code);
}

void pet_key_up(pet_t* sys, int key_code) {
    CHIPS_ASSERT(sys && sys->valid);
    kbd_key_up(&sys->kbd, key_code);
}

static void _pet_init_key_map(pet_t* sys) {
    // the PET graphics keyboard is a 10x8 matrix; PIA-1 PA selects one of
    // ten "columns" (rows on the schematic), PIA-1 PB reads back eight "lines"
    kbd_init(&sys->kbd, 1);
    // shift keys (left and right) both register modifier bit 0
    kbd_register_modifier(&sys->kbd, 0, 8, 0);

    kbd_register_key(&sys->kbd, ' ', 9, 2, 0);
    kbd_register_key(&sys->kbd, '!', 0, 0, 0);
    kbd_register_key(&sys->kbd, '"', 1, 0, 0);
    kbd_register_key(&sys->kbd, '#', 0, 1, 0);
    kbd_register_key(&sys->kbd, '$', 1, 1, 0);
    kbd_register_key(&sys->kbd, '%', 0, 2, 0);
    kbd_register_key(&sys->kbd, '&', 0, 3, 0);
    kbd_register_key(&sys->kbd, '\'', 1, 2, 0);
    kbd_register_key(&sys->kbd, '(', 0, 4, 0);
    kbd_register_key(&sys->kbd, ')', 1, 4, 0);
    kbd_register_key(&sys->kbd, '*', 5, 7, 0);
    kbd_register_key(&sys->kbd, '+', 7, 7, 0);
    kbd_register_key(&sys->kbd, ',', 7, 3, 0);
    kbd_register_key(&sys->kbd, '-', 8, 7, 0);
    kbd_register_key(&sys->kbd, '.', 9, 6, 0);
    kbd_register_key(&sys->kbd, '/', 3, 7, 0);
    kbd_register_key(&sys->kbd, '0', 8, 6, 0);
    kbd_register_key(&sys->kbd, '1', 6, 6, 0);
    kbd_register_key(&sys->kbd, '2', 7, 6, 0);
    kbd_register_key(&sys->kbd, '3', 6, 7, 0);
    kbd_register_key(&sys->kbd, '4', 4, 6, 0);
    kbd_register_key(&sys->kbd, '5', 5, 6, 0);
    kbd_register_key(&sys->kbd, '6', 4, 7, 0);
    kbd_register_key(&sys->kbd, '7', 2, 6, 0);
    kbd_register_key(&sys->kbd, '8', 3, 6, 0);
    kbd_register_key(&sys->kbd, '9', 2, 7, 0);
    kbd_register_key(&sys->kbd, ':', 5, 4, 0);
    kbd_register_key(&sys->kbd, ';', 6, 4, 0);
    kbd_register_key(&sys->kbd, '<', 9, 3, 0);
    kbd_register_key(&sys->kbd, '=', 9, 7, 0);
    kbd_register_key(&sys->kbd, '>', 8, 4, 0);
    kbd_register_key(&sys->kbd, '?', 7, 4, 0);
    kbd_register_key(&sys->kbd, '@', 8, 1, 0);
    kbd_register_key(&sys->kbd, 'A', 4, 0, 0);
    kbd_register_key(&sys->kbd, 'B', 6, 2, 0);
    kbd_register_key(&sys->kbd, 'C', 6, 1, 0);
    kbd_register_key(&sys->kbd, 'D', 4, 1, 0);
    kbd_register_key(&sys->kbd, 'E', 2, 1, 0);
    kbd_register_key(&sys->kbd, 'F', 5, 1, 0);
    kbd_register_key(&sys->kbd, 'G', 4, 2, 0);
    kbd_register_key(&sys->kbd, 'H', 5, 2, 0);
    kbd_register_key(&sys->kbd, 'I', 3, 3, 0);
    kbd_register_key(&sys->kbd, 'J', 4, 3, 0);
    kbd_register_key(&sys->kbd, 'K', 5, 3, 0);
    kbd_register_key(&sys->kbd, 'L', 4, 4, 0);
    kbd_register_key(&sys->kbd, 'M', 6, 3, 0);
    kbd_register_key(&sys->kbd, 'N', 7, 2, 0);
    kbd_register_key(&sys->kbd, 'O', 2, 4, 0);
    kbd_register_key(&sys->kbd, 'P', 3, 4, 0);
    kbd_register_key(&sys->kbd, 'Q', 2, 0, 0);
    kbd_register_key(&sys->kbd, 'R', 3, 1, 0);
    kbd_register_key(&sys->kbd, 'S', 5, 0, 0);
    kbd_register_key(&sys->kbd, 'T', 2, 2, 0);
    kbd_register_key(&sys->kbd, 'U', 2, 3, 0);
    kbd_register_key(&sys->kbd, 'V', 7, 1, 0);
    kbd_register_key(&sys->kbd, 'W', 3, 0, 0);
    kbd_register_key(&sys->kbd, 'X', 7, 0, 0);
    kbd_register_key(&sys->kbd, 'Y', 3, 2, 0);
    kbd_register_key(&sys->kbd, 'Z', 6, 0, 0);
    kbd_register_key(&sys->kbd, '[', 9, 1, 0);
    kbd_register_key(&sys->kbd, '\\', 1, 3, 0);
    kbd_register_key(&sys->kbd, ']', 8, 2, 0);
    kbd_register_key(&sys->kbd, '^', 2, 5, 0);
    kbd_register_key(&sys->kbd, '_', 0, 5, 0);
    // lowercase letters reuse the uppercase keys (shifted == graphics)
    kbd_register_key(&sys->kbd, 'a', 4, 0, (1<<0));
    kbd_register_key(&sys->kbd, 'b', 6, 2, (1<<0));
    kbd_register_key(&sys->kbd, 'c', 6, 1, (1<<0));
    kbd_register_key(&sys->kbd, 'd', 4, 1, (1<<0));
    kbd_register_key(&sys->kbd, 'e', 2, 1, (1<<0));
    kbd_register_key(&sys->kbd, 'f', 5, 1, (1<<0));
    kbd_register_key(&sys->kbd, 'g', 4, 2, (1<<0));
    kbd_register_key(&sys->kbd, 'h', 5, 2, (1<<0));
    kbd_register_key(&sys->kbd, 'i', 3, 3, (1<<0));
    kbd_register_key(&sys->kbd, 'j', 4, 3, (1<<0));
    kbd_register_key(&sys->kbd, 'k', 5, 3, (1<<0));
    kbd_register_key(&sys->kbd, 'l', 4, 4, (1<<0));
    kbd_register_key(&sys->kbd, 'm', 6, 3, (1<<0));
    kbd_register_key(&sys->kbd, 'n', 7, 2, (1<<0));
    kbd_register_key(&sys->kbd, 'o', 2, 4, (1<<0));
    kbd_register_key(&sys->kbd, 'p', 3, 4, (1<<0));
    kbd_register_key(&sys->kbd, 'q', 2, 0, (1<<0));
    kbd_register_key(&sys->kbd, 'r', 3, 1, (1<<0));
    kbd_register_key(&sys->kbd, 's', 5, 0, (1<<0));
    kbd_register_key(&sys->kbd, 't', 2, 2, (1<<0));
    kbd_register_key(&sys->kbd, 'u', 2, 3, (1<<0));
    kbd_register_key(&sys->kbd, 'v', 7, 1, (1<<0));
    kbd_register_key(&sys->kbd, 'w', 3, 0, (1<<0));
    kbd_register_key(&sys->kbd, 'x', 7, 0, (1<<0));
    kbd_register_key(&sys->kbd, 'y', 3, 2, (1<<0));
    kbd_register_key(&sys->kbd, 'z', 6, 0, (1<<0));
    // control keys
    kbd_register_key(&sys->kbd, 0x0D, 6, 5, 0);      // return
    kbd_register_key(&sys->kbd, 0x09, 0, 7, 0);      // cursor right
    kbd_register_key(&sys->kbd, 0x08, 0, 7, (1<<0)); // cursor left
    kbd_register_key(&sys->kbd, 0x0A, 1, 6, 0);      // cursor down
    kbd_register_key(&sys->kbd, 0x0B, 1, 6, (1<<0)); // cursor up
    kbd_register_key(&sys->kbd, 0x01, 1, 7, 0);      // delete
    kbd_register_key(&sys->kbd, 0x0C, 1, 7, (1<<0)); // insert (shift+delete)
    kbd_register_key(&sys->kbd, 0x13, 0, 6, 0);      // home
    kbd_register_key(&sys->kbd, 0x03, 9, 4, 0);      // run/stop
}

chips_display_info_t pet_display_info(pet_t* sys) {
    static const uint32_t palette[2] = {
        0xFF000000,     // black
        0xFF44EE44,     // phosphor green
    };
    const chips_display_info_t res = {
        .frame = {
            .dim = {
                .width = PET_FRAMEBUFFER_WIDTH,
                .height = PET_FRAMEBUFFER_HEIGHT,
            },
            .buffer = {
                .ptr = sys ? sys->fb : 0,
                .size = PET_FRAMEBUFFER_SIZE_BYTES,
            },
            .bytes_per_pixel = 1,
        },
        .screen = {
            .x = 0,
            .y = 0,
            .width = PET_DISPLAY_WIDTH,
            .height = PET_DISPLAY_HEIGHT,
        },
        .palette = {
            .ptr = (void*)palette,
            .size = sizeof(palette),
        },
    };
    CHIPS_ASSERT(((sys == 0) && (res.frame.buffer.ptr == 0)) || ((sys != 0) && (res.frame.buffer.ptr != 0)));
    return res;
}

uint32_t pet_save_snapshot(pet_t* sys, pet_t* dst) {
    CHIPS_ASSERT(sys && dst);
    *dst = *sys;
    chips_debug_snapshot_onsave(&dst->debug);
    chips_audio_callback_snapshot_onsave(&dst->audio.callback);
    m6502_snapshot_onsave(&dst->cpu);
    mem_snapshot_onsave(&dst->mem, sys);
    return PET_SNAPSHOT_VERSION;
}

bool pet_load_snapshot(pet_t* sys, uint32_t version, pet_t* src) {
    CHIPS_ASSERT(sys && src);
    if (version != PET_SNAPSHOT_VERSION) {
        return false;
    }
    static pet_t im;
    im = *src;
    chips_debug_snapshot_onload(&im.debug, &sys->debug);
    chips_audio_callback_snapshot_onload(&im.audio.callback, &sys->audio.callback);
    m6502_snapshot_onload(&im.cpu, &sys->cpu);
    mem_snapshot_onload(&im.mem, sys);
    *sys = im;
    return true;
}

#endif // CHIPS_IMPL
