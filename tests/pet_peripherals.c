/* SPDX-License-Identifier: Zlib */
#define CHIPS_IMPL
#include "../chips/m6520.h"
#include "../chips/m6522.h"
#include <assert.h>

/* Tick a PIA bus transaction with stable, independently controllable inputs. */
static uint64_t pia_access(m6520_t* p, uint8_t rs, uint8_t data, bool read, uint64_t inputs) {
    uint64_t pins = inputs | M6520_CS | rs | (read ? M6520_RW : 0);
    M6520_SET_DATA(pins, data);
    return m6520_tick(p, pins);
}

/* Tick a VIA access while retaining the supplied external pin levels. */
static uint64_t via_access_with_inputs(m6522_t* v, uint8_t rs, uint8_t data, bool read, uint64_t inputs) {
    uint64_t pins = inputs | M6522_CS1 | rs | (read ? M6522_RW : 0);
    M6522_SET_DATA(pins, data);
    return m6522_tick(v, pins);
}

/* Tick a VIA bus transaction with inactive control-input levels. */
static uint64_t via_access(m6522_t* v, uint8_t rs, uint8_t data, bool read) {
    return via_access_with_inputs(v, rs, data, read, M6522_CA_PINS | M6522_CB_PINS);
}

/* Exercise DDR isolation, mixed GPIO, read-only flags, and all C2 modes. */
static void test_pia(void) {
    m6520_t p;
    m6520_init(&p);
    uint64_t inputs = M6520_CA_PINS | M6520_CB_PINS;
    M6520_SET_PAB(inputs, 0xA5, 0x5A);
    pia_access(&p, 0, 0x0F, false, inputs);
    pia_access(&p, 1, 0x04, false, inputs);
    pia_access(&p, 0, 0x33, false, inputs);
    assert(p.pa.ddr == 0x0F && p.pa.outr == 0x33);
    assert(m6520_peek(&p, inputs) == 0xA3);
    pia_access(&p, 1, 0x00, false, inputs);
    assert(M6520_GET_DATA(pia_access(&p, 0, 0, true, inputs)) == 0x0F);

    pia_access(&p, 1, 0x34, false, inputs);
    assert(!p.pa.c2_out);
    pia_access(&p, 1, 0x3C, false, inputs);
    assert(p.pa.c2_out);
    pia_access(&p, 3, 0x34, false, inputs);
    assert(!p.pb.c2_out);
    pia_access(&p, 3, 0x3C, false, inputs);
    assert(p.pb.c2_out);

    pia_access(&p, 1, 0x24, false, inputs);
    pia_access(&p, 0, 0xFF, false, inputs);
    assert(p.pa.c2_out);
    pia_access(&p, 0, 0, true, inputs);
    assert(!p.pa.c2_out);
    m6520_tick(&p, inputs & ~M6520_CA1);
    assert(p.pa.c2_out);
    pia_access(&p, 3, 0x24, false, inputs);
    pia_access(&p, 2, 0, true, inputs);
    assert(p.pb.c2_out);
    pia_access(&p, 2, 0xFF, false, inputs);
    assert(!p.pb.c2_out);
    m6520_tick(&p, inputs & ~M6520_CB1);
    assert(p.pb.c2_out);

    pia_access(&p, 1, 0x2C, false, inputs);
    pia_access(&p, 0, 0, true, inputs);
    assert(!p.pa.c2_out);
    m6520_tick(&p, inputs);
    assert(p.pa.c2_out);
    pia_access(&p, 3, 0x2C, false, inputs);
    pia_access(&p, 2, 0, false, inputs);
    assert(!p.pb.c2_out);
    m6520_tick(&p, inputs);
    assert(p.pb.c2_out);

    m6520_reset(&p);
    m6520_tick(&p, inputs);
    pia_access(&p, 1, 0x0D, false, inputs);
    uint64_t pins = m6520_tick(&p, inputs & ~(M6520_CA1 | M6520_CA2));
    assert((p.pa.cr & 0xC0) == 0xC0 && (pins & M6520_IRQ));
    assert(!(pins & M6520_CA2));
    assert((m6520_peek(&p, inputs | 1) & 0xC0) == 0xC0);
    assert((p.pa.cr & 0xC0) == 0xC0);
    pia_access(&p, 1, 0x04, false, inputs);
    assert((p.pa.cr & 0xC0) == 0xC0);
    assert(!(p.pins & M6520_IRQ));
    pia_access(&p, 0, 0, true, inputs);
    assert((p.pa.cr & 0xC0) == 0);
    m6520_reset(&p);
    assert(p.pa.cr == 0 && p.pa.ddr == 0 && p.pa.outr == 0);
}

/* Verify exact T1 load/expiry/reload periods, IRQ masking and control pins. */
static void test_via(void) {
    m6522_t v;
    m6522_init(&v);
    via_access(&v, 2, 0x0F, false);
    via_access(&v, 0, 0x33, false);
    uint64_t inputs = M6522_CA_PINS | M6522_CB_PINS;
    M6522_SET_PAB(inputs, 0xFF, 0xA5);
    assert(m6522_peek(&v, inputs) == 0xA3);
    via_access(&v, 13, 0x7F, false);
    via_access(&v, 14, 0xC0, false);
    via_access(&v, 4, 3, false);
    via_access(&v, 5, 0, false);
    assert(v.t1.counter == 3);
    for (int i = 0; i < 4; ++i) {
        m6522_tick(&v, inputs);
        assert(!(v.intr.ifr & 0x40));
    }
    m6522_tick(&v, inputs);
    assert((v.intr.ifr & 0xC0) == 0xC0 && (v.pins & M6522_IRQ));
    via_access(&v, 13, 0x80, false);
    assert((v.intr.ifr & 0xC0) == 0xC0);
    via_access(&v, 14, 0x40, false);
    assert((v.intr.ifr & 0x40) && !(v.intr.ifr & 0x80) && !(v.pins & M6522_IRQ));
    via_access(&v, 14, 0xC0, false);
    assert(v.pins & M6522_IRQ);
    m6522_peek(&v, inputs | 4);
    assert(v.intr.ifr & 0x40);
    via_access(&v, 4, 0, true);
    assert(!(v.intr.ifr & 0x40));
    for (int i = 0; i < 20; ++i) {
        m6522_tick(&v, inputs);
        assert(!(v.intr.ifr & 0x40));
    }
    via_access(&v, 11, 0x40, false);
    via_access(&v, 5, 0, false);
    for (int period = 0; period < 3; ++period) {
        for (int i = 0; i < 5; ++i) {
            m6522_tick(&v, inputs);
        }
        assert(v.intr.ifr & 0x40);
        /* Clear without advancing a timer cycle, to measure the reload period. */
        v.intr.ifr &= 0x3F;
    }

    m6522_reset(&v);
    m6522_tick(&v, inputs);
    m6522_tick(&v, inputs & ~(M6522_CB1 | M6522_CB2));
    assert((v.intr.ifr & 0x18) == 0x18);
    assert(!(v.pins & (M6522_CB1 | M6522_CB2)));
    via_access(&v, 13, 0x7F, false);
    m6522_tick(&v, inputs & ~M6522_CA2);
    assert(v.intr.ifr & 0x01);
    via_access(&v, 13, 0x7F, false);
    m6522_tick(&v, inputs);
    assert(!(v.intr.ifr & 0x01));

    m6522_reset(&v);
    for (int i = 0; i < 20; ++i) {
        m6522_tick(&v, inputs);
        assert(!(v.intr.ifr & 0x60));
    }
    via_access(&v, 4, 0xFF, false);
    via_access(&v, 5, 0xFF, false);
    via_access(&v, 8, 0xFF, false);
    via_access(&v, 9, 0xFF, false);
    assert(!(v.intr.ifr & 0x60));
    for (int i = 0; i < 100; ++i) {
        m6522_tick(&v, inputs);
        assert(!(v.intr.ifr & 0x60));
    }
}

/* Count resolved PB6 falling edges, including latch and DDR transitions. */
static void test_via_t2_pb6_outputs(void) {
    const uint8_t pb6_mask = M6522_GET_PB(M6522_PB6);
    const uint8_t pulse_count_acr = 0x20;
    const uint8_t timer_load = 9;
    const int idle_cycles = 8;
    for (int external_high = 0; external_high < 2; ++external_high) {
        for (int output_high = 0; output_high < 2; ++output_high) {
            m6522_t v;
            m6522_init(&v);
            uint64_t inputs = M6522_CA_PINS | M6522_CB_PINS;
            M6522_SET_PB(inputs, external_high ? pb6_mask : 0);
            /* Stable outputs must override external levels without false pulses. */
            via_access_with_inputs(&v, M6522_REG_RB, output_high ? pb6_mask : 0, false, inputs);
            via_access_with_inputs(&v, M6522_REG_DDRB, pb6_mask, false, inputs);
            via_access_with_inputs(&v, M6522_REG_ACR, pulse_count_acr, false, inputs);
            via_access_with_inputs(&v, M6522_REG_T2CL, timer_load, false, inputs);
            via_access_with_inputs(&v, M6522_REG_T2CH, 0, false, inputs);
            assert(v.t2.counter == timer_load);
            for (int i = 0; i < idle_cycles; ++i) {
                m6522_tick(&v, inputs);
                assert(v.t2.counter == timer_load);
            }
            /* External transitions cannot change the level of an output pin. */
            for (int i = 0; i < idle_cycles; ++i) {
                inputs ^= M6522_PB6;
                m6522_tick(&v, inputs);
                assert(v.t2.counter == timer_load);
            }
            /* Output-latch transitions count only the high-to-low edge. */
            inputs &= ~M6522_PB6;
            via_access_with_inputs(&v, M6522_REG_RB, pb6_mask, false, inputs);
            for (int i = 0; i < idle_cycles; ++i) {
                m6522_tick(&v, inputs);
                assert(v.t2.counter == timer_load);
            }
            via_access_with_inputs(&v, M6522_REG_RB, 0, false, inputs);
            assert(v.t2.counter == timer_load - 1);
            for (int i = 0; i < idle_cycles; ++i) {
                m6522_tick(&v, inputs);
                assert(v.t2.counter == timer_load - 1);
            }
            /* Releasing a high output to an external low counts exactly once. */
            via_access_with_inputs(&v, M6522_REG_RB, pb6_mask, false, inputs);
            via_access_with_inputs(&v, M6522_REG_DDRB, 0, false, inputs);
            assert(v.t2.counter == timer_load - 2);
            for (int i = 0; i < idle_cycles; ++i) {
                m6522_tick(&v, inputs);
                assert(v.t2.counter == timer_load - 2);
            }
            assert(!(v.intr.ifr & M6522_IRQ_T2));
        }
    }
}

/* T2 samples live PB6 independently of the CPU-visible port input latch. */
static void test_via_t2_pb6_input_latch(void) {
    const uint8_t pb6_mask = M6522_GET_PB(M6522_PB6);
    const uint8_t latch_b_pulse_count_acr = 0x22;
    const uint8_t timer_load = 9;
    m6522_t v;
    m6522_init(&v);
    uint64_t inputs = M6522_CA_PINS | M6522_CB_PINS | M6522_PB6;
    /* Keep the port latch low while arming the timer with a live high pin. */
    m6522_tick(&v, inputs & ~M6522_PB6);
    via_access_with_inputs(&v, M6522_REG_ACR, latch_b_pulse_count_acr, false, inputs);
    via_access_with_inputs(&v, M6522_REG_T2CL, timer_load, false, inputs);
    via_access_with_inputs(&v, M6522_REG_T2CH, 0, false, inputs);
    assert(v.t2.counter == timer_load);
    assert(v.pb.inpr == 0 && (v.pins & M6522_PB6));
    /* Count input edges without updating the latch or inventing repeats. */
    m6522_tick(&v, inputs & ~M6522_PB6);
    assert(v.t2.counter == timer_load - 1);
    m6522_tick(&v, inputs & ~M6522_PB6);
    assert(v.t2.counter == timer_load - 1);
    m6522_tick(&v, inputs);
    assert(v.t2.counter == timer_load - 1);
    assert(m6522_peek(&v, inputs | M6522_REG_RB) == 0);
    /* Latch a high value, then count a falling pin while the latch stays high. */
    inputs &= ~M6522_CB1;
    m6522_tick(&v, inputs);
    assert(v.pb.inpr == pb6_mask);
    m6522_tick(&v, inputs & ~M6522_PB6);
    assert(v.t2.counter == timer_load - 2);
    m6522_tick(&v, inputs & ~M6522_PB6);
    assert(v.t2.counter == timer_load - 2);
    assert(m6522_peek(&v, (inputs & ~M6522_PB6) | M6522_REG_RB) == pb6_mask);
}

int main(void) {
    test_pia();
    test_via();
    test_via_t2_pb6_outputs();
    test_via_t2_pb6_input_latch();
    return 0;
}
