#pragma once
/*#
    # ui_pet.h

    Integrated debugging UI for pet.h

    Do this:
    ~~~C
    #define CHIPS_UI_IMPL
    ~~~
    before you include this file in *one* C++ file to create the
    implementation.

    Optionally provide the following macros with your own implementation

    ~~~C
    CHIPS_ASSERT(c)
    ~~~
        your own assert macro (default: assert(c))

    Include the following headers (and their dependencies) before including
    ui_pet.h both for the declaration and implementation.

    - chips_common.h
    - pet.h
    - mem.h
    - ui_chip.h
    - ui_util.h
    - ui_settings.h
    - ui_m6502.h
    - ui_m6520.h
    - ui_m6522.h
    - ui_mc6845.h
    - ui_audio.h
    - ui_display.h
    - ui_dasm.h
    - ui_dbg.h
    - ui_memedit.h
    - ui_memmap.h
    - ui_kbd.h
    - ui_snapshot.h

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

#ifdef __cplusplus
extern "C" {
#endif

// reboot callback
typedef void (*ui_pet_boot_cb)(pet_t* sys);

// setup params for ui_pet_init()
typedef struct {
    pet_t* pet;                 // pointer to pet_t instance to track
    ui_pet_boot_cb boot_cb;     // reboot callback function
    ui_dbg_texture_callbacks_t dbg_texture;     // texture create/update/destroy callbacks
    ui_dbg_keys_desc_t dbg_keys;    // user-defined hotkeys for ui_dbg_t
    ui_snapshot_desc_t snapshot;    // snapshot ui setup params
} ui_pet_desc_t;

typedef struct {
    pet_t* pet;
    ui_pet_boot_cb boot_cb;
    ui_m6502_t cpu;
    ui_m6520_t pia[2];
    ui_m6522_t via;
    ui_mc6845_t crtc;
    ui_audio_t audio;
    ui_display_t display;
    ui_kbd_t kbd;
    ui_memmap_t memmap;
    ui_memedit_t memedit[4];
    ui_dasm_t dasm[4];
    ui_dbg_t dbg;
    ui_snapshot_t snapshot;
    struct {
        const char* title;
        bool open;
    } system;
} ui_pet_t;

typedef struct {
    ui_display_frame_t display;
} ui_pet_frame_t;

void ui_pet_init(ui_pet_t* ui, const ui_pet_desc_t* desc);
void ui_pet_discard(ui_pet_t* ui);
void ui_pet_draw(ui_pet_t* ui, const ui_pet_frame_t* frame);
chips_debug_t ui_pet_get_debug(ui_pet_t* ui);
void ui_pet_save_settings(ui_pet_t* ui, ui_settings_t* settings);
void ui_pet_load_settings(ui_pet_t* ui, const ui_settings_t* settings);

#ifdef __cplusplus
} // extern "C"
#endif

/*-- IMPLEMENTATION (include in C++ source) ----------------------------------*/
#ifdef CHIPS_UI_IMPL
#ifndef __cplusplus
#error "implementation must be compiled as C++"
#endif
#include <string.h> /* memset */
#ifndef CHIPS_ASSERT
    #include <assert.h>
    #define CHIPS_ASSERT(c) assert(c)
#endif
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#endif

static void _ui_pet_draw_menu(ui_pet_t* ui) {
    CHIPS_ASSERT(ui && ui->pet && ui->boot_cb);
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("System")) {
            ui_snapshot_menus(&ui->snapshot);
            if (ImGui::MenuItem("Reset")) {
                pet_reset(ui->pet);
                ui_dbg_reset(&ui->dbg);
            }
            if (ImGui::MenuItem("Cold Boot")) {
                ui->boot_cb(ui->pet);
                ui_dbg_reboot(&ui->dbg);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Hardware")) {
            ImGui::MenuItem("System", 0, &ui->system.open);
            ImGui::MenuItem("Memory Map", 0, &ui->memmap.open);
            ImGui::MenuItem("Keyboard Matrix", 0, &ui->kbd.open);
            ImGui::MenuItem("Audio Output", 0, &ui->audio.open);
            ImGui::MenuItem("Display", 0, &ui->display.open);
            ImGui::MenuItem("MOS 6502 (CPU)", 0, &ui->cpu.open);
            ImGui::MenuItem("MOS 6520 #1 (PIA)", 0, &ui->pia[0].open);
            ImGui::MenuItem("MOS 6520 #2 (PIA)", 0, &ui->pia[1].open);
            ImGui::MenuItem("MOS 6522 (VIA)", 0, &ui->via.open);
            ImGui::MenuItem("MC6845 (CRTC)", 0, &ui->crtc.open);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Debug")) {
            ImGui::MenuItem("CPU Debugger", 0, &ui->dbg.ui.open);
            ImGui::MenuItem("Breakpoints", 0, &ui->dbg.ui.breakpoints.open);
            ImGui::MenuItem("Stopwatch", 0, &ui->dbg.ui.stopwatch.open);
            ImGui::MenuItem("Execution History", 0, &ui->dbg.ui.history.open);
            ImGui::MenuItem("Memory Heatmap", 0, &ui->dbg.ui.heatmap.open);
            if (ImGui::BeginMenu("Memory Editor")) {
                ImGui::MenuItem("Window #1", 0, &ui->memedit[0].open);
                ImGui::MenuItem("Window #2", 0, &ui->memedit[1].open);
                ImGui::MenuItem("Window #3", 0, &ui->memedit[2].open);
                ImGui::MenuItem("Window #4", 0, &ui->memedit[3].open);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Disassembler")) {
                ImGui::MenuItem("Window #1", 0, &ui->dasm[0].open);
                ImGui::MenuItem("Window #2", 0, &ui->dasm[1].open);
                ImGui::MenuItem("Window #3", 0, &ui->dasm[2].open);
                ImGui::MenuItem("Window #4", 0, &ui->dasm[3].open);
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        ui_util_options_menu();
        ImGui::EndMainMenuBar();
    }
}

#define _UI_PET_MEMLAYER_CPU    (0)     // CPU visible mapping
#define _UI_PET_CODELAYER_NUM   (1)
#define _UI_PET_MEMLAYER_NUM    (1)

static const char* _ui_pet_memlayer_names[_UI_PET_MEMLAYER_NUM] = {
    "CPU Mapped"
};

static uint8_t _ui_pet_mem_read(int layer, uint16_t addr, void* user_data) {
    CHIPS_ASSERT(user_data);
    ui_pet_t* ui = (ui_pet_t*) user_data;
    pet_t* pet = ui->pet;
    (void)layer;
    return mem_rd(&pet->mem, addr);
}

static void _ui_pet_mem_write(int layer, uint16_t addr, uint8_t data, void* user_data) {
    CHIPS_ASSERT(user_data);
    ui_pet_t* ui = (ui_pet_t*) user_data;
    pet_t* pet = ui->pet;
    (void)layer;
    mem_wr(&pet->mem, addr, data);
}

static void _ui_pet_update_memmap(ui_pet_t* ui) {
    CHIPS_ASSERT(ui && ui->pet);
    ui_memmap_reset(&ui->memmap);
    ui_memmap_layer(&ui->memmap, "SYS");
        ui_memmap_region(&ui->memmap, "RAM",    0x0000, 0x8000, true);
        ui_memmap_region(&ui->memmap, "SCREEN", 0x8000, 0x0400, true);
        ui_memmap_region(&ui->memmap, "BASIC",  0xB000, 0x3000, true);
        ui_memmap_region(&ui->memmap, "EDITOR", 0xE000, 0x0800, true);
        ui_memmap_region(&ui->memmap, "IO",     0xE800, 0x0100, true);
        ui_memmap_region(&ui->memmap, "KERNAL", 0xF000, 0x1000, true);
}

static int _ui_pet_eval_bp(ui_dbg_t* dbg_win, int trap_id, uint64_t pins, void* user_data) {
    (void)dbg_win; (void)pins; (void)user_data;
    return trap_id;
}

static const ui_chip_pin_t _ui_pet_cpu_pins[] = {
    { "D0",     0,      M6502_D0 },
    { "D1",     1,      M6502_D1 },
    { "D2",     2,      M6502_D2 },
    { "D3",     3,      M6502_D3 },
    { "D4",     4,      M6502_D4 },
    { "D5",     5,      M6502_D5 },
    { "D6",     6,      M6502_D6 },
    { "D7",     7,      M6502_D7 },
    { "RW",     9,      M6502_RW },
    { "SYNC",   10,     M6502_SYNC },
    { "RDY",    11,     M6502_RDY },
    { "IRQ",    12,     M6502_IRQ },
    { "NMI",    13,     M6502_NMI },
    { "RES",    14,     M6502_RES },
    { "A0",     16,     M6502_A0 },
    { "A1",     17,     M6502_A1 },
    { "A2",     18,     M6502_A2 },
    { "A3",     19,     M6502_A3 },
    { "A4",     20,     M6502_A4 },
    { "A5",     21,     M6502_A5 },
    { "A6",     22,     M6502_A6 },
    { "A7",     23,     M6502_A7 },
    { "A8",     24,     M6502_A8 },
    { "A9",     25,     M6502_A9 },
    { "A10",    26,     M6502_A10 },
    { "A11",    27,     M6502_A11 },
    { "A12",    28,     M6502_A12 },
    { "A13",    29,     M6502_A13 },
    { "A14",    30,     M6502_A14 },
    { "A15",    31,     M6502_A15 },
};

static const ui_chip_pin_t _ui_pet_pia_pins[] = {
    { "D0",     0,      M6520_D0 },
    { "D1",     1,      M6520_D1 },
    { "D2",     2,      M6520_D2 },
    { "D3",     3,      M6520_D3 },
    { "D4",     4,      M6520_D4 },
    { "D5",     5,      M6520_D5 },
    { "D6",     6,      M6520_D6 },
    { "D7",     7,      M6520_D7 },
    { "RS0",    9,      M6520_RS0 },
    { "RS1",    10,     M6520_RS1 },
    { "RW",     12,     M6520_RW },
    { "CS",     13,     M6520_CS },
    { "IRQ",    14,     M6520_IRQ },
    { "PA0",    20,     M6520_PA0 },
    { "PA1",    21,     M6520_PA1 },
    { "PA2",    22,     M6520_PA2 },
    { "PA3",    23,     M6520_PA3 },
    { "PA4",    24,     M6520_PA4 },
    { "PA5",    25,     M6520_PA5 },
    { "PA6",    26,     M6520_PA6 },
    { "PA7",    27,     M6520_PA7 },
    { "CA1",    28,     M6520_CA1 },
    { "CA2",    29,     M6520_CA2 },
    { "PB0",    30,     M6520_PB0 },
    { "PB1",    31,     M6520_PB1 },
    { "PB2",    32,     M6520_PB2 },
    { "PB3",    33,     M6520_PB3 },
    { "PB4",    34,     M6520_PB4 },
    { "PB5",    35,     M6520_PB5 },
    { "PB6",    36,     M6520_PB6 },
    { "PB7",    37,     M6520_PB7 },
    { "CB1",    38,     M6520_CB1 },
    { "CB2",    39,     M6520_CB2 },
};

static const ui_chip_pin_t _ui_pet_via_pins[] = {
    { "D0",     0,      M6522_D0 },
    { "D1",     1,      M6522_D1 },
    { "D2",     2,      M6522_D2 },
    { "D3",     3,      M6522_D3 },
    { "D4",     4,      M6522_D4 },
    { "D5",     5,      M6522_D5 },
    { "D6",     6,      M6522_D6 },
    { "D7",     7,      M6522_D7 },
    { "RS0",    9,      M6522_RS0 },
    { "RS1",    10,     M6522_RS1 },
    { "RS2",    11,     M6522_RS2 },
    { "RS3",    12,     M6522_RS3 },
    { "RW",     14,     M6522_RW },
    { "CS1",    15,     M6522_CS1 },
    { "CS2",    16,     M6522_CS2 },
    { "IRQ",    17,     M6522_IRQ },
    { "PA0",    20,     M6522_PA0 },
    { "PA1",    21,     M6522_PA1 },
    { "PA2",    22,     M6522_PA2 },
    { "PA3",    23,     M6522_PA3 },
    { "PA4",    24,     M6522_PA4 },
    { "PA5",    25,     M6522_PA5 },
    { "PA6",    26,     M6522_PA6 },
    { "PA7",    27,     M6522_PA7 },
    { "CA1",    28,     M6522_CA1 },
    { "CA2",    29,     M6522_CA2 },
    { "PB0",    30,     M6522_PB0 },
    { "PB1",    31,     M6522_PB1 },
    { "PB2",    32,     M6522_PB2 },
    { "PB3",    33,     M6522_PB3 },
    { "PB4",    34,     M6522_PB4 },
    { "PB5",    35,     M6522_PB5 },
    { "PB6",    36,     M6522_PB6 },
    { "PB7",    37,     M6522_PB7 },
    { "CB1",    38,     M6522_CB1 },
    { "CB2",    39,     M6522_CB2 },
};

static const ui_chip_pin_t _ui_pet_crtc_pins[] = {
    { "D0",     0,      MC6845_D0 },
    { "D1",     1,      MC6845_D1 },
    { "D2",     2,      MC6845_D2 },
    { "D3",     3,      MC6845_D3 },
    { "D4",     4,      MC6845_D4 },
    { "D5",     5,      MC6845_D5 },
    { "D6",     6,      MC6845_D6 },
    { "D7",     7,      MC6845_D7 },
    { "CS",     9,      MC6845_CS },
    { "RS",    10,      MC6845_RS },
    { "RW",    11,      MC6845_RW },
    { "DE",    13,      MC6845_DE },
    { "VS",    14,      MC6845_VS },
    { "HS",    15,      MC6845_HS },
    { "MA0",   20,      MC6845_MA0 },
    { "MA1",   21,      MC6845_MA1 },
    { "MA2",   22,      MC6845_MA2 },
    { "MA3",   23,      MC6845_MA3 },
    { "MA4",   24,      MC6845_MA4 },
    { "MA5",   25,      MC6845_MA5 },
    { "MA6",   26,      MC6845_MA6 },
    { "MA7",   27,      MC6845_MA7 },
    { "MA8",   28,      MC6845_MA8 },
    { "MA9",   29,      MC6845_MA9 },
    { "MA10",  30,      MC6845_MA10 },
    { "MA11",  31,      MC6845_MA11 },
    { "MA12",  32,      MC6845_MA12 },
    { "MA13",  33,      MC6845_MA13 },
    { "RA0",   35,      MC6845_RA0 },
    { "RA1",   36,      MC6845_RA1 },
    { "RA2",   37,      MC6845_RA2 },
    { "RA3",   38,      MC6845_RA3 },
    { "RA4",   39,      MC6845_RA4 },
};

void ui_pet_init(ui_pet_t* ui, const ui_pet_desc_t* ui_desc) {
    CHIPS_ASSERT(ui && ui_desc);
    CHIPS_ASSERT(ui_desc->pet);
    CHIPS_ASSERT(ui_desc->boot_cb);
    ui->pet = ui_desc->pet;
    ui->system.title = "PET System";
    ui->boot_cb = ui_desc->boot_cb;
    ui_snapshot_init(&ui->snapshot, &ui_desc->snapshot);
    int x = 20, y = 20, dx = 10, dy = 10;
    {
        ui_dbg_desc_t desc = {0};
        desc.title = "CPU Debugger";
        desc.x = x;
        desc.y = y;
        desc.m6502 = &ui->pet->cpu;
        desc.read_cb = _ui_pet_mem_read;
        desc.break_cb = _ui_pet_eval_bp;
        desc.texture_cbs = ui_desc->dbg_texture;
        desc.keys = ui_desc->dbg_keys;
        desc.user_data = ui;
        ui_dbg_init(&ui->dbg, &desc);
    }
    x += dx; y += dy;
    {
        ui_m6502_desc_t desc = {0};
        desc.title = "MOS 6502";
        desc.cpu = &ui->pet->cpu;
        desc.x = x;
        desc.y = y;
        desc.h = 390;
        UI_CHIP_INIT_DESC(&desc.chip_desc, "6502", 32, _ui_pet_cpu_pins);
        ui_m6502_init(&ui->cpu, &desc);
    }
    x += dx; y += dy;
    {
        ui_m6520_desc_t desc = {0};
        desc.title = "MOS 6520 #1 (PIA)";
        desc.pia = &ui->pet->pia_1;
        desc.regs_base = 0xE810;
        desc.x = x;
        desc.y = y;
        UI_CHIP_INIT_DESC(&desc.chip_desc, "6520", 40, _ui_pet_pia_pins);
        ui_m6520_init(&ui->pia[0], &desc);
        x += dx; y += dy;
        desc.title = "MOS 6520 #2 (PIA)";
        desc.pia = &ui->pet->pia_2;
        desc.regs_base = 0xE820;
        desc.x = x;
        desc.y = y;
        ui_m6520_init(&ui->pia[1], &desc);
    }
    x += dx; y += dy;
    {
        ui_m6522_desc_t desc = {0};
        desc.title = "MOS 6522 (VIA)";
        desc.via = &ui->pet->via;
        desc.regs_base = 0xE840;
        desc.x = x;
        desc.y = y;
        UI_CHIP_INIT_DESC(&desc.chip_desc, "6522", 40, _ui_pet_via_pins);
        ui_m6522_init(&ui->via, &desc);
    }
    x += dx; y += dy;
    {
        ui_mc6845_desc_t desc = {0};
        desc.title = "MC6845 (CRTC)";
        desc.mc6845 = &ui->pet->crtc;
        desc.x = x;
        desc.y = y;
        UI_CHIP_INIT_DESC(&desc.chip_desc, "6845", 40, _ui_pet_crtc_pins);
        ui_mc6845_init(&ui->crtc, &desc);
    }
    x += dx; y += dy;
    {
        ui_audio_desc_t desc = {0};
        desc.title = "Audio Output";
        desc.sample_buffer = ui->pet->audio.sample_buffer;
        desc.num_samples = ui->pet->audio.num_samples;
        desc.x = x;
        desc.y = y;
        ui_audio_init(&ui->audio, &desc);
    }
    x += dx; y += dy;
    {
        ui_display_desc_t desc = {0};
        desc.title = "Display";
        desc.x = x;
        desc.y = y;
        ui_display_init(&ui->display, &desc);
    }
    x += dx; y += dy;
    {
        ui_kbd_desc_t desc = {0};
        desc.title = "Keyboard Matrix";
        desc.kbd = &ui->pet->kbd;
        desc.layers[0] = "None";
        desc.layers[1] = "Shift";
        desc.x = x;
        desc.y = y;
        ui_kbd_init(&ui->kbd, &desc);
    }
    x += dx; y += dy;
    {
        ui_memedit_desc_t desc = {0};
        for (int i = 0; i < _UI_PET_MEMLAYER_NUM; i++) {
            desc.layers[i] = _ui_pet_memlayer_names[i];
        }
        desc.read_cb = _ui_pet_mem_read;
        desc.write_cb = _ui_pet_mem_write;
        desc.user_data = ui;
        static const char* titles[] = { "Memory Editor #1", "Memory Editor #2", "Memory Editor #3", "Memory Editor #4" };
        for (int i = 0; i < 4; i++) {
            desc.title = titles[i]; desc.x = x; desc.y = y;
            ui_memedit_init(&ui->memedit[i], &desc);
            x += dx; y += dy;
        }
    }
    x += dx; y += dy;
    {
        ui_memmap_desc_t desc = {0};
        desc.title = "Memory Map";
        desc.x = x;
        desc.y = y;
        ui_memmap_init(&ui->memmap, &desc);
    }
    x += dx; y += dy;
    {
        ui_dasm_desc_t desc = {0};
        for (int i = 0; i < _UI_PET_CODELAYER_NUM; i++) {
            desc.layers[i] = _ui_pet_memlayer_names[i];
        }
        desc.cpu_type = UI_DASM_CPUTYPE_M6502;
        desc.start_addr = mem_rd16(&ui->pet->mem, 0xFFFC);
        desc.read_cb = _ui_pet_mem_read;
        desc.user_data = ui;
        static const char* titles[4] = { "Disassembler #1", "Disassembler #2", "Disassembler #3", "Disassembler #4" };
        for (int i = 0; i < 4; i++) {
            desc.title = titles[i]; desc.x = x; desc.y = y;
            ui_dasm_init(&ui->dasm[i], &desc);
            x += dx; y += dy;
        }
    }
}

void ui_pet_discard(ui_pet_t* ui) {
    CHIPS_ASSERT(ui && ui->pet);
    ui->pet = 0;
    ui_m6502_discard(&ui->cpu);
    ui_m6520_discard(&ui->pia[0]);
    ui_m6520_discard(&ui->pia[1]);
    ui_m6522_discard(&ui->via);
    ui_mc6845_discard(&ui->crtc);
    ui_kbd_discard(&ui->kbd);
    ui_audio_discard(&ui->audio);
    ui_display_discard(&ui->display);
    ui_memmap_discard(&ui->memmap);
    for (int i = 0; i < 4; i++) {
        ui_memedit_discard(&ui->memedit[i]);
        ui_dasm_discard(&ui->dasm[i]);
    }
    ui_dbg_discard(&ui->dbg);
}

void ui_pet_draw_system(ui_pet_t* ui) {
    if (!ui->system.open) {
        return;
    }
    pet_t* sys = ui->pet;
    ImGui::SetNextWindowSize({ 220, 160}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin(ui->system.title, &ui->system.open)) {
        ImGui::Text("Model: CBM 4032");
        ImGui::Text("Char set: %s", sys->chars_lowercase ? "Text (lower/upper)" : "Graphics (upper/gfx)");
        ImGui::Text("VSYNC: %s", sys->vsync_prev ? "active" : "inactive");
        if (ImGui::CollapsingHeader("CRTC", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("H Displayed: %d", sys->crtc.h_displayed);
            ImGui::Text("V Displayed: %d", sys->crtc.v_displayed);
            ImGui::Text("Start Addr:  %02X%02X", sys->crtc.start_addr_hi, sys->crtc.start_addr_lo);
        }
    }
    ImGui::End();
}

void ui_pet_draw(ui_pet_t* ui, const ui_pet_frame_t* frame) {
    CHIPS_ASSERT(ui && ui->pet && frame);
    _ui_pet_draw_menu(ui);
    if (ui->memmap.open) {
        _ui_pet_update_memmap(ui);
    }
    ui_pet_draw_system(ui);
    ui_audio_draw(&ui->audio, ui->pet->audio.sample_pos);
    ui_display_draw(&ui->display, &frame->display);
    ui_kbd_draw(&ui->kbd);
    ui_m6502_draw(&ui->cpu);
    ui_m6520_draw(&ui->pia[0]);
    ui_m6520_draw(&ui->pia[1]);
    ui_m6522_draw(&ui->via);
    ui_mc6845_draw(&ui->crtc);
    ui_memmap_draw(&ui->memmap);
    for (int i = 0; i < 4; i++) {
        ui_memedit_draw(&ui->memedit[i]);
        ui_dasm_draw(&ui->dasm[i]);
    }
    ui_dbg_draw(&ui->dbg);
}

chips_debug_t ui_pet_get_debug(ui_pet_t* ui) {
    chips_debug_t res = {};
    res.callback.func = (chips_debug_func_t)ui_dbg_tick;
    res.callback.user_data = &ui->dbg;
    res.stopped = &ui->dbg.dbg.stopped;
    return res;
}

void ui_pet_save_settings(ui_pet_t* ui, ui_settings_t* settings) {
    CHIPS_ASSERT(ui && settings);
    ui_m6502_save_settings(&ui->cpu, settings);
    ui_m6520_save_settings(&ui->pia[0], settings);
    ui_m6520_save_settings(&ui->pia[1], settings);
    ui_m6522_save_settings(&ui->via, settings);
    ui_mc6845_save_settings(&ui->crtc, settings);
    ui_audio_save_settings(&ui->audio, settings);
    ui_display_save_settings(&ui->display, settings);
    ui_kbd_save_settings(&ui->kbd, settings);
    ui_memmap_save_settings(&ui->memmap, settings);
    for (int i = 0; i < 4; i++) {
        ui_memedit_save_settings(&ui->memedit[i], settings);
    }
    for (int i = 0; i < 4; i++) {
        ui_dasm_save_settings(&ui->dasm[i], settings);
    }
    ui_dbg_save_settings(&ui->dbg, settings);
    ui_settings_add(settings, ui->system.title, ui->system.open);
}

void ui_pet_load_settings(ui_pet_t* ui, const ui_settings_t* settings) {
    CHIPS_ASSERT(ui && settings);
    ui_m6502_load_settings(&ui->cpu, settings);
    ui_m6520_load_settings(&ui->pia[0], settings);
    ui_m6520_load_settings(&ui->pia[1], settings);
    ui_m6522_load_settings(&ui->via, settings);
    ui_mc6845_load_settings(&ui->crtc, settings);
    ui_audio_load_settings(&ui->audio, settings);
    ui_display_load_settings(&ui->display, settings);
    ui_kbd_load_settings(&ui->kbd, settings);
    ui_memmap_load_settings(&ui->memmap, settings);
    for (int i = 0; i < 4; i++) {
        ui_memedit_load_settings(&ui->memedit[i], settings);
    }
    for (int i = 0; i < 4; i++) {
        ui_dasm_load_settings(&ui->dasm[i], settings);
    }
    ui_dbg_load_settings(&ui->dbg, settings);
    ui->system.open = ui_settings_isopen(settings, ui->system.title);
}

#ifdef __clang__
#pragma clang diagnostic pop
#endif
#endif /* CHIPS_UI_IMPL */
