#pragma once
/*#
    # m6520.h

    Header-only MOS 6520/6521 PIA (Peripheral Interface Adapter) emulator
    written in C.

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

    ## Emulated Pins
    *************************************
    *           +-----------+           *
    *   RS0 --->|           |<--- CA1   *
    *   RS1 --->|           |<--> CA2   *
    *           |           |           *
    *   CS  --->|           |<--> PA0   *
    *           |           |...        *
    *    D0 <-->|           |<--> PA7   *
    *        ...|   m6520   |           *
    *    D7 <-->|           |<--- CB1   *
    *           |           |<--> CB2   *
    *    RW --->|           |           *
    *   IRQ <---|           |<--> PB0   *
    *           |           |...        *
    *           |           |<--> PB7   *
    *           +-----------+           *
    *************************************

    The 6520 is the simpler companion of the 6522 VIA used in the
    Commodore PET and other 6502-based machines. It provides two
    8-bit bidirectional ports (A and B), each with two control lines
    (CA1/CA2 and CB1/CB2) and an interrupt output.

    Unlike the 6522 the 6520 has no timers and no shift register.

    Call m6520_init() to initialize a new m6520_t instance (note that
    there is no m6520_desc_t struct).

    In each system tick, call the m6520_tick() function, this takes
    an input pin mask, and returns a (potentially modified) output
    pin mask.

    The control pins, register select and data bus pins share the same
    pin positions as the m6522, so both chips can be ticked with the
    same shared pin mask.

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

// register select pins, same positions as the lower address bus bits
#define M6520_RS0       (1ULL<<0)
#define M6520_RS1       (1ULL<<1)
#define M6520_RS_PINS   (0x03ULL)

// data bus pins shared with CPU
#define M6520_D0        (1ULL<<16)
#define M6520_D1        (1ULL<<17)
#define M6520_D2        (1ULL<<18)
#define M6520_D3        (1ULL<<19)
#define M6520_D4        (1ULL<<20)
#define M6520_D5        (1ULL<<21)
#define M6520_D6        (1ULL<<22)
#define M6520_D7        (1ULL<<23)
#define M6520_DB_PINS   (0xFF0000ULL)

// control pins shared with CPU
#define M6520_RW        (1ULL<<24)      // same as M6502_RW
#define M6520_IRQ       (1ULL<<26)      // same as M6502_IRQ

// chip-select (chip active when CS is set)
#define M6520_CS        (1ULL<<40)

// control pins, same positions as the m6522 CA/CB pins
#define M6520_CA1       (1ULL<<42)      // peripheral A control 1 (input)
#define M6520_CA2       (1ULL<<43)      // peripheral A control 2
#define M6520_CB1       (1ULL<<44)      // peripheral B control 1 (input)
#define M6520_CB2       (1ULL<<45)      // peripheral B control 2
#define M6520_CA_PINS   (M6520_CA1|M6520_CA2)
#define M6520_CB_PINS   (M6520_CB1|M6520_CB2)

// peripheral A port
#define M6520_PA0       (1ULL<<48)
#define M6520_PA1       (1ULL<<49)
#define M6520_PA2       (1ULL<<50)
#define M6520_PA3       (1ULL<<51)
#define M6520_PA4       (1ULL<<52)
#define M6520_PA5       (1ULL<<53)
#define M6520_PA6       (1ULL<<54)
#define M6520_PA7       (1ULL<<55)
#define M6520_PA_PINS   (0xFFULL<<48)

// peripheral B port
#define M6520_PB0       (1ULL<<56)
#define M6520_PB1       (1ULL<<57)
#define M6520_PB2       (1ULL<<58)
#define M6520_PB3       (1ULL<<59)
#define M6520_PB4       (1ULL<<60)
#define M6520_PB5       (1ULL<<61)
#define M6520_PB6       (1ULL<<62)
#define M6520_PB7       (1ULL<<63)
#define M6520_PB_PINS   (0xFFULL<<56)

// register indices (addressed by RS1=A/B side, RS0=data-or-ctrl)
#define M6520_REG_RA    (0)     // output/input register A or DDRA (depending on CRA bit 2)
#define M6520_REG_CRA   (1)     // control register A
#define M6520_REG_RB    (2)     // output/input register B or DDRB (depending on CRB bit 2)
#define M6520_REG_CRB   (3)     // control register B

// control register bit masks
#define M6520_CR_C1_ENABLE_IRQ  (1<<0)  // C1 interrupt enable
#define M6520_CR_C1_POS_EDGE    (1<<1)  // C1 active transition (1: low->high, 0: high->low)
#define M6520_CR_PORT_SELECT    (1<<2)  // 1: access peripheral register, 0: access DDR
#define M6520_CR_C2_MODE        (1<<3)  // C2 control (meaning depends on C2_OUTPUT)
#define M6520_CR_C2_EDGE        (1<<4)  // C2 active transition / output level select
#define M6520_CR_C2_OUTPUT      (1<<5)  // 1: C2 is output, 0: C2 is input
#define M6520_CR_IRQ2           (1<<6)  // C2 interrupt flag (read only)
#define M6520_CR_IRQ1           (1<<7)  // C1 interrupt flag (read only)

// I/O port state
typedef struct {
    uint8_t inpr;       // input register (pin levels)
    uint8_t outr;       // output register
    uint8_t ddr;        // data direction register (1: output)
    uint8_t cr;         // control register
    uint8_t pins;       // last port pin state
    bool c1_in;
    bool c1_triggered;
    bool c2_in;
    bool c2_out;
    bool c2_triggered;
} m6520_port_t;

// m6520 state
typedef struct {
    m6520_port_t pa;
    m6520_port_t pb;
    uint64_t pins;
} m6520_t;

// extract 8-bit data bus from 64-bit pins
#define M6520_GET_DATA(p) ((uint8_t)((p)>>16))
// merge 8-bit data bus value into 64-bit pins
#define M6520_SET_DATA(p,d) {p=(((p)&~0xFF0000ULL)|(((d)&0xFFULL)<<16));}
// extract port A pins
#define M6520_GET_PA(p) ((uint8_t)((p)>>48))
// extract port B pins
#define M6520_GET_PB(p) ((uint8_t)((p)>>56))
// merge port A pins into pin mask
#define M6520_SET_PA(p,a) {p=((p)&0xFF00FFFFFFFFFFFFULL)|(((a)&0xFFULL)<<48);}
// merge port B pins into pin mask
#define M6520_SET_PB(p,b) {p=((p)&0x00FFFFFFFFFFFFFFULL)|(((b)&0xFFULL)<<56);}
// merge port A and B pins into pin mask
#define M6520_SET_PAB(p,a,b) {p=((p)&0x0000FFFFFFFFFFFFULL)|(((a)&0xFFULL)<<48)|(((b)&0xFFULL)<<56);}

// initialize a new m6520 instance
void m6520_init(m6520_t* c);
// reset an existing m6520 instance
void m6520_reset(m6520_t* c);
// tick the m6520
uint64_t m6520_tick(m6520_t* c, uint64_t pins);

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

static void _m6520_init_port(m6520_port_t* p) {
    p->inpr = 0xFF;
    p->outr = 0;
    p->ddr = 0;
    p->cr = 0;
    p->pins = 0;
    p->c1_in = false;
    p->c1_triggered = false;
    p->c2_in = false;
    p->c2_out = true;
    p->c2_triggered = false;
}

void m6520_init(m6520_t* c) {
    CHIPS_ASSERT(c);
    memset(c, 0, sizeof(*c));
    _m6520_init_port(&c->pa);
    _m6520_init_port(&c->pb);
}

void m6520_reset(m6520_t* c) {
    CHIPS_ASSERT(c);
    _m6520_init_port(&c->pa);
    _m6520_init_port(&c->pb);
    c->pins = 0;
}

// detect active control-line transitions and latch input register
static inline void _m6520_read_port_pins(m6520_t* c, uint64_t pins) {
    const bool new_ca1 = 0 != (pins & M6520_CA1);
    const bool new_ca2 = 0 != (pins & M6520_CA2);
    const bool new_cb1 = 0 != (pins & M6520_CB1);
    const bool new_cb2 = 0 != (pins & M6520_CB2);

    // CA1 / CB1 are always inputs
    const bool ca1_pos = 0 != (c->pa.cr & M6520_CR_C1_POS_EDGE);
    const bool cb1_pos = 0 != (c->pb.cr & M6520_CR_C1_POS_EDGE);
    c->pa.c1_triggered = (c->pa.c1_in != new_ca1) && (new_ca1 == ca1_pos);
    c->pb.c1_triggered = (c->pb.c1_in != new_cb1) && (new_cb1 == cb1_pos);

    // CA2 / CB2 only trigger when configured as input
    if (0 == (c->pa.cr & M6520_CR_C2_OUTPUT)) {
        const bool ca2_pos = 0 != (c->pa.cr & M6520_CR_C2_EDGE);
        c->pa.c2_triggered = (c->pa.c2_in != new_ca2) && (new_ca2 == ca2_pos);
    }
    if (0 == (c->pb.cr & M6520_CR_C2_OUTPUT)) {
        const bool cb2_pos = 0 != (c->pb.cr & M6520_CR_C2_EDGE);
        c->pb.c2_triggered = (c->pb.c2_in != new_cb2) && (new_cb2 == cb2_pos);
    }

    c->pa.c1_in = new_ca1;
    c->pa.c2_in = new_ca2;
    c->pb.c1_in = new_cb1;
    c->pb.c2_in = new_cb2;

    c->pa.inpr = M6520_GET_PA(pins);
    c->pb.inpr = M6520_GET_PB(pins);

    // set the interrupt flags in the control registers
    if (c->pa.c1_triggered) { c->pa.cr |= M6520_CR_IRQ1; }
    if (c->pa.c2_triggered && (0 == (c->pa.cr & M6520_CR_C2_OUTPUT))) { c->pa.cr |= M6520_CR_IRQ2; }
    if (c->pb.c1_triggered) { c->pb.cr |= M6520_CR_IRQ1; }
    if (c->pb.c2_triggered && (0 == (c->pb.cr & M6520_CR_C2_OUTPUT))) { c->pb.cr |= M6520_CR_IRQ2; }
}

static inline uint64_t _m6520_write_port_pins(m6520_t* c, uint64_t pins) {
    c->pa.pins = (c->pa.inpr & ~c->pa.ddr) | (c->pa.outr & c->pa.ddr);
    c->pb.pins = (c->pb.inpr & ~c->pb.ddr) | (c->pb.outr & c->pb.ddr);
    M6520_SET_PAB(pins, c->pa.pins, c->pb.pins);

    // CA2 / CB2 output level (CA1/CB1 are input-only)
    pins &= ~(M6520_CA2|M6520_CB2);
    if (c->pa.c2_out) { pins |= M6520_CA2; }
    if (c->pb.c2_out) { pins |= M6520_CB2; }
    return pins;
}

static inline bool _m6520_irq_active(const m6520_port_t* p) {
    bool irq = false;
    if ((p->cr & M6520_CR_IRQ1) && (p->cr & M6520_CR_C1_ENABLE_IRQ)) {
        irq = true;
    }
    // C2 interrupt only when C2 is configured as input and its IRQ is enabled (CR bit 3)
    if (0 == (p->cr & M6520_CR_C2_OUTPUT)) {
        if ((p->cr & M6520_CR_IRQ2) && (p->cr & M6520_CR_C2_MODE)) {
            irq = true;
        }
    }
    return irq;
}

static inline uint64_t _m6520_update_irq(m6520_t* c, uint64_t pins) {
    if (_m6520_irq_active(&c->pa) || _m6520_irq_active(&c->pb)) {
        pins |= M6520_IRQ;
    }
    else {
        pins &= ~M6520_IRQ;
    }
    return pins;
}

// update the fixed C2 output level based on the control register
static inline void _m6520_update_c2_out(m6520_port_t* p) {
    if (p->cr & M6520_CR_C2_OUTPUT) {
        if (p->cr & M6520_CR_C2_MODE) {
            // manual output mode: C2 follows CR bit 4
            p->c2_out = 0 != (p->cr & M6520_CR_C2_EDGE);
        }
        // (handshake/pulse output modes default the line high)
    }
    else {
        p->c2_out = true;
    }
}

static uint8_t _m6520_read(m6520_t* c, uint8_t addr) {
    uint8_t data = 0;
    switch (addr) {
        case M6520_REG_RA:
            if (c->pa.cr & M6520_CR_PORT_SELECT) {
                // read peripheral register A, clears the IRQ flags
                data = (c->pa.inpr & ~c->pa.ddr) | (c->pa.outr & c->pa.ddr);
                c->pa.cr &= ~(M6520_CR_IRQ1|M6520_CR_IRQ2);
            }
            else {
                data = c->pa.ddr;
            }
            break;
        case M6520_REG_CRA:
            data = c->pa.cr;
            break;
        case M6520_REG_RB:
            if (c->pb.cr & M6520_CR_PORT_SELECT) {
                data = (c->pb.inpr & ~c->pb.ddr) | (c->pb.outr & c->pb.ddr);
                c->pb.cr &= ~(M6520_CR_IRQ1|M6520_CR_IRQ2);
            }
            else {
                data = c->pb.ddr;
            }
            break;
        case M6520_REG_CRB:
            data = c->pb.cr;
            break;
    }
    return data;
}

static void _m6520_write(m6520_t* c, uint8_t addr, uint8_t data) {
    switch (addr) {
        case M6520_REG_RA:
            if (c->pa.cr & M6520_CR_PORT_SELECT) {
                c->pa.outr = data;
            }
            else {
                c->pa.ddr = data;
            }
            break;
        case M6520_REG_CRA:
            // bits 6 and 7 are read-only interrupt flags
            c->pa.cr = (c->pa.cr & (M6520_CR_IRQ1|M6520_CR_IRQ2)) | (data & 0x3F);
            _m6520_update_c2_out(&c->pa);
            break;
        case M6520_REG_RB:
            if (c->pb.cr & M6520_CR_PORT_SELECT) {
                c->pb.outr = data;
            }
            else {
                c->pb.ddr = data;
            }
            break;
        case M6520_REG_CRB:
            c->pb.cr = (c->pb.cr & (M6520_CR_IRQ1|M6520_CR_IRQ2)) | (data & 0x3F);
            _m6520_update_c2_out(&c->pb);
            break;
    }
}

uint64_t m6520_tick(m6520_t* c, uint64_t pins) {
    _m6520_read_port_pins(c, pins);
    if (pins & M6520_CS) {
        uint8_t addr = pins & M6520_RS_PINS;
        if (pins & M6520_RW) {
            uint8_t data = _m6520_read(c, addr);
            M6520_SET_DATA(pins, data);
        }
        else {
            uint8_t data = M6520_GET_DATA(pins);
            _m6520_write(c, addr, data);
        }
    }
    pins = _m6520_update_irq(c, pins);
    pins = _m6520_write_port_pins(c, pins);
    c->pins = pins;
    return pins;
}

#endif /* CHIPS_IMPL */
