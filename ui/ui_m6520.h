#pragma once
/*#
    # ui_m6520.h

    Debug visualization UI for m6520.h

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

    Include the following headers before the including the *declaration*:
        - m6520.h
        - ui_chip.h

    Include the following headers before including the *implementation*:
        - imgui.h
        - m6520.h
        - ui_chip.h
        - ui_util.h
        - ui_settings.h

    All strings provided to ui_m6520_init() must remain alive until
    ui_m6520_discard() is called!

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

/* setup parameters for ui_m6520_init()
    NOTE: all string data must remain alive until ui_m6520_discard()!
*/
typedef struct ui_m6520_desc_t {
    const char* title;          /* window title */
    m6520_t* pia;               /* m6520_t instance to track */
    uint16_t regs_base;         /* register bank base address (e.g. E810 on PET) */
    int x, y;                   /* initial window pos */
    int w, h;                   /* initial window size (or 0 for default size) */
    bool open;                  /* initial window open state */
    ui_chip_desc_t chip_desc;   /* chip visualization desc */
} ui_m6520_desc_t;

typedef struct ui_m6520_t {
    const char* title;
    m6520_t* pia;
    uint16_t regs_base;
    float init_x, init_y;
    float init_w, init_h;
    bool open;
    bool last_open;
    bool valid;
    ui_chip_t chip;
} ui_m6520_t;

void ui_m6520_init(ui_m6520_t* win, const ui_m6520_desc_t* desc);
void ui_m6520_discard(ui_m6520_t* win);
void ui_m6520_draw(ui_m6520_t* win);
void ui_m6520_save_settings(ui_m6520_t* win, ui_settings_t* settings);
void ui_m6520_load_settings(ui_m6520_t* win, const ui_settings_t* settings);

#ifdef __cplusplus
} /* extern "C" */
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

void ui_m6520_init(ui_m6520_t* win, const ui_m6520_desc_t* desc) {
    CHIPS_ASSERT(win && desc);
    CHIPS_ASSERT(desc->title);
    CHIPS_ASSERT(desc->pia);
    memset(win, 0, sizeof(ui_m6520_t));
    win->title = desc->title;
    win->pia = desc->pia;
    win->regs_base = desc->regs_base;
    win->init_x = (float) desc->x;
    win->init_y = (float) desc->y;
    win->init_w = (float) ((desc->w == 0) ? 440 : desc->w);
    win->init_h = (float) ((desc->h == 0) ? 360 : desc->h);
    win->open = win->last_open = desc->open;
    win->valid = true;
    ui_chip_init(&win->chip, &desc->chip_desc);
}

void ui_m6520_discard(ui_m6520_t* win) {
    CHIPS_ASSERT(win && win->valid);
    win->valid = false;
}

static void _ui_m6520_draw_registers(ui_m6520_t* win) {
    if (ImGui::CollapsingHeader("Registers", ImGuiTreeNodeFlags_DefaultOpen)) {
        const m6520_t* pia = win->pia;
        uint16_t rb = win->regs_base;
        const char* ra = (pia->pa.cr & M6520_CR_PORT_SELECT) ? "ORA " : "DDRA";
        const char* rbn = (pia->pb.cr & M6520_CR_PORT_SELECT) ? "ORB " : "DDRB";
        ImGui::Text("%s ($%04X/%5d): %02X", ra, rb+0, rb+0, (pia->pa.cr & M6520_CR_PORT_SELECT) ? pia->pa.outr : pia->pa.ddr);
        ImGui::Text("CRA  ($%04X/%5d): %02X", rb+1, rb+1, pia->pa.cr);
        ImGui::Text("%s ($%04X/%5d): %02X", rbn, rb+2, rb+2, (pia->pb.cr & M6520_CR_PORT_SELECT) ? pia->pb.outr : pia->pb.ddr);
        ImGui::Text("CRB  ($%04X/%5d): %02X", rb+3, rb+3, pia->pb.cr);
    }
}

static void _ui_m6520_draw_port(const char* name, const m6520_port_t* p) {
    ui_util_b8("DDR:  ", p->ddr);  ImGui::SameLine(); ImGui::Text("(%02X)", p->ddr);
    ui_util_b8("Inp:  ", p->inpr); ImGui::SameLine(); ImGui::Text("(%02X)", p->inpr);
    ui_util_b8("Out:  ", p->outr); ImGui::SameLine(); ImGui::Text("(%02X)", p->outr);
    ui_util_b8("Pins: ", p->pins); ImGui::SameLine(); ImGui::Text("(%02X)", p->pins);
    ImGui::Text("C1: in=%s", p->c1_in?"ON ":"OFF");
    ImGui::Text("C2: in=%s, out=%s", p->c2_in?"ON ":"OFF", p->c2_out?"ON ":"OFF");
    (void)name;
}

static void _ui_m6520_draw_ports(ui_m6520_t* win) {
    if (ImGui::CollapsingHeader("Ports", ImGuiTreeNodeFlags_DefaultOpen)) {
        const m6520_t* pia = win->pia;
        ImGui::Text("Port A");
        _ui_m6520_draw_port("A", &pia->pa);
        ImGui::Separator();
        ImGui::Text("Port B");
        _ui_m6520_draw_port("B", &pia->pb);
    }
}

static void _ui_m6520_draw_cr(const char* name, uint8_t cr) {
    ImGui::Text("%s: %02X", name, cr);
    ImGui::Text("  C1 IRQ:    %s", (cr & M6520_CR_C1_ENABLE_IRQ) ? "Enabled" : "Disabled");
    ImGui::Text("  C1 Edge:   %s", (cr & M6520_CR_C1_POS_EDGE) ? "Rising" : "Falling");
    ImGui::Text("  Port Sel:  %s", (cr & M6520_CR_PORT_SELECT) ? "Peripheral" : "DDR");
    ImGui::Text("  C2 Dir:    %s", (cr & M6520_CR_C2_OUTPUT) ? "Output" : "Input");
    ImGui::Text("  IRQ1 flag: %s", (cr & M6520_CR_IRQ1) ? "ON " : "OFF");
    ImGui::Text("  IRQ2 flag: %s", (cr & M6520_CR_IRQ2) ? "ON " : "OFF");
}

static void _ui_m6520_draw_int_ctrl(ui_m6520_t* win) {
    if (ImGui::CollapsingHeader("Control & Interrupts", ImGuiTreeNodeFlags_DefaultOpen)) {
        const m6520_t* pia = win->pia;
        _ui_m6520_draw_cr("CRA", pia->pa.cr);
        ImGui::Separator();
        _ui_m6520_draw_cr("CRB", pia->pb.cr);
    }
}

void ui_m6520_draw(ui_m6520_t* win) {
    CHIPS_ASSERT(win && win->valid && win->pia);
    ui_util_handle_window_open_dirty(&win->open, &win->last_open);
    if (!win->open) {
        return;
    }
    ImGui::SetNextWindowPos(ImVec2(win->init_x, win->init_y), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(win->init_w, win->init_h), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(win->title, &win->open)) {
        ImGui::BeginChild("##m6520_chip", ImVec2(176, 0), true);
        ui_chip_draw(&win->chip, win->pia->pins);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##m6520_state", ImVec2(0, 0), true);
        _ui_m6520_draw_registers(win);
        _ui_m6520_draw_ports(win);
        _ui_m6520_draw_int_ctrl(win);
        ImGui::EndChild();
    }
    ImGui::End();
}

void ui_m6520_save_settings(ui_m6520_t* win, ui_settings_t* settings) {
    CHIPS_ASSERT(win && settings);
    ui_settings_add(settings, win->title, win->open);
}

void ui_m6520_load_settings(ui_m6520_t* win, const ui_settings_t* settings) {
    CHIPS_ASSERT(win && settings);
    win->open = ui_settings_isopen(settings, win->title);
}

#endif /* CHIPS_UI_IMPL */
