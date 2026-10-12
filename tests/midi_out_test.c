/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Andrix Bley (@andrixb) */
/* OPQuencer M0: MIDI OUT to any MIDI-CV converter or DAW, over USB (firmware/src/seq.c: the note
 * tracker, G_MIDI OUT mode, the TX channels): the sequencer's notes at their step velocity, drum hits
 * (accented at 127), the arp, ratchet parts retriggered, slides into the same note as one MIDI note,
 * and every hanging note ended by STOP, a panic, a song section change or MIDI OUT back to KEYS.
 * Built and run by tests/run_tests.sh. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

#define OUTQ 256
static uint8_t oq_st[OUTQ], oq_ch[OUTQ], oq_nt[OUTQ], oq_vl[OUTQ];
static uint32_t out_drain(void)              /* the MIDI OUT queue since the last drain, decoded */
{
    uint32_t n = 0;
    while (mo_r != mo_w && n < OUTQ) {
        uint32_t pkt = midi_out_q[mo_r % MQ];
        mo_r++;
        oq_st[n] = (uint8_t)((pkt >> 8) & 0xF0u);
        oq_ch[n] = (uint8_t)((pkt >> 8) & 0x0Fu);
        oq_nt[n] = (uint8_t)((pkt >> 16) & 0x7Fu);
        oq_vl[n] = (uint8_t)((pkt >> 24) & 0x7Fu);
        n++;
    }
    return n;
}
static uint32_t out_count(uint32_t n, uint32_t st, uint32_t ch, uint32_t note)
{
    uint32_t i, k = 0;
    for (i = 0; i < n; i++)
        k += oq_st[i] == st && oq_ch[i] == ch && oq_nt[i] == note;
    return k;
}
static uint32_t out_vel(uint32_t n, uint32_t ch, uint32_t note)   /* velocity of the first note-on */
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (oq_st[i] == 0x90u && oq_ch[i] == ch && oq_nt[i] == note)
            return oq_vl[i];
    return 0;
}

static void out_reset(void)
{
    seq_stop();
    ui_power_on();
    usb.config = 1;
    mo_w = mo_r = 0;
    memset(mo_set, 0, sizeof mo_set);
    memset(mo_ch, 0, sizeof mo_ch);
    mo_any = 0;
    seq_quiet = 0;
    song.g[G_MIDI] = 0;
    song.g[G_DRLVL] = 0;
    song.g[G_DRREV] = 0;
    song.g[G_ROUTE] = 0;
    song.g[G_SWING] = 0;
    events_block(CTL);
}
/* track 1 alone, a one-step NOTE loop; the transport started */
static track_t *out_track(uint32_t vel, uint32_t flags)
{
    track_t *t = &trk[0];
    track_defaults_steps(t);
    t->p[P_SLEN] = 1;
    t->p[P_SGATE] = 64;
    t->p[P_SSWING] = 0;
    t->step[0] = (step_t){{60}, 1, ST_NOTE, (uint8_t)flags, (uint8_t)vel};
    seq_start();
    return t;
}
static uint32_t out_step_len(const track_t *t) { return step_samples(t, div_samples((uint32_t)t->p[P_SDIV]), 0); }
static void out_run(track_t *t, uint32_t blocks)
{
    for (uint32_t b = 0; b < blocks; b++)
        seq_tick(t, CTL);
}
static uint32_t out_passes(const track_t *t, uint32_t n) { return n * out_step_len(t) / CTL - 1u; }
static int out_hanging(void)                 /* a MIDI OUT note still on, on any track */
{
    for (uint32_t i = 0; i < NTRK; i++)
        for (uint32_t w = 0; w < 4u; w++)
            if (mo_set[i][w])
                return 1;
    return 0;
}

static int mode_test(void)
{
    int bad = 0;
    uint32_t n;
    out_reset();
    track_t *t = out_track(100, 0);
    out_run(t, out_passes(t, 2));
    n = out_drain();
    bad += check("OUT KEYS (default): the sequencer plays internally, nothing to MIDI OUT", n == 0 && t->seq_n == 0);
    song.g[G_MIDI] = 1;                                  /* SEQ */
    out_run(t, out_passes(t, 2));
    n = out_drain();
    bad += check("OUT SEQ: two passes, two note-ons and two note-offs of step velocity on ch 1",
                 out_count(n, 0x90u, 0, 60) == 2u && out_count(n, 0x80u, 0, 60) == 2u && out_vel(n, 0, 60) == 100u);
    seq_stop();
    n = out_drain();
    bad += check("  STOP ends nothing hanging (gates ended in their steps)", n == 0 && !out_hanging());
    return bad;
}

static int channels_test(void)
{
    int bad = 0, ok;
    uint32_t n;
    track_t *t;
    out_reset();
    tx_pack(0, 5);                                       /* TX T1: fixed channel 5 */
    song.g[G_MIDI] = 1;
    t = out_track(90, 0);
    out_run(t, out_passes(t, 1));
    n = out_drain();
    bad += check("TX fixed: the step goes out on channel 5 at its velocity",
                 out_count(n, 0x90u, 4, 60) == 1u && out_vel(n, 4, 60) == 90u && out_count(n, 0x90u, 0, 60) == 0u);
    tx_pack(0, TX_OFF);                                  /* TX T1 OFF */
    out_run(t, out_passes(t, 1));
    n = out_drain();
    ok = n == 0;
    {   /* the track still plays internally (its voice starts) */
        uint32_t v0 = vage;
        out_run(t, out_passes(t, 1));
        ok &= vage > v0;
        n = out_drain();
    }
    bad += check("TX OFF: nothing sent, the track still plays internally", ok && n == 0);
    out_reset();                                         /* FOLLOW + ROUTE CH5-8 */
    song.g[G_ROUTE] = 2;
    song.g[G_MIDI] = 1;
    t = out_track(100, 0);
    out_run(t, out_passes(t, 1));
    n = out_drain();
    bad += check("TX FOLLOW: ROUTE CH5-8 puts track 1 on channel 5, as the keys",
                 out_count(n, 0x90u, 4, 60) == 1u);
    return bad;
}

static int notes_test(void)
{
    int bad = 0;
    uint32_t n;
    track_t *t;
    out_reset();                                         /* accent: 127 out */
    song.g[G_MIDI] = 1;
    t = out_track(80, SF_ACCENT);
    out_run(t, out_passes(t, 1));
    n = out_drain();
    bad += check("an accented step goes out at 127 (its hits would too)",
                 out_count(n, 0x90u, 0, 60) == 1u && out_vel(n, 0, 60) == 127u);
    out_reset();                                         /* chance 0: nothing plays, nothing sent */
    song.g[G_MIDI] = 1;
    t = out_track(100, 0);
    step_set_chance(&t->step[0], 0);
    out_run(t, out_passes(t, 2));
    n = out_drain();
    bad += check("a step the chance drops: no MIDI OUT either", n == 0);
    out_reset();                                         /* a slide into the same note: one MIDI note */
    song.g[G_MIDI] = 1;                                  /* (direct step calls: no gate timing in the way) */
    t = &trk[0];
    track_defaults_steps(t);
    seq_out_on(t, 60, 100);                              /* the step playing now */
    (void)out_drain();
    t->seq_hold = 1; t->seq_n = 1; t->seq_notes[0] = 60;
    {   /* the next step slides into the same note: held over, no retrigger */
        step_t s1 = {{60}, 1, ST_NOTE, 0, 100};
        seq_step(t, &s1, 1, div_samples(2), SEQ_ROLLED);
    }
    n = out_drain();
    bad += check("a slide into the same note: held over, no new note-on, still on",
                 n == 0 && (mo_set[0][60u >> 5] & (1u << (60u & 31u))));
    t->seq_hold = 1; t->seq_n = 1; t->seq_notes[0] = 60;
    {   /* .. into another note: the old one ends, the new one starts */
        step_t s2 = {{62}, 1, ST_NOTE, 0, 100};
        seq_step(t, &s2, 1, div_samples(2), SEQ_ROLLED);
    }
    n = out_drain();
    bad += check("a slide into another note: the old note ends, the new one starts",
                 out_count(n, 0x80u, 0, 60) == 1u && out_count(n, 0x90u, 0, 62) == 1u);
    out_reset();                                         /* ratchet x2: two thrown pairs a pass */
    song.g[G_MIDI] = 1;
    t = out_track(100, 0);
    step_set_ratchet(&t->step[0], 2);
    out_run(t, out_passes(t, 2));
    n = out_drain();
    bad += check("a ratchet x2: two note-ons and two note-offs a pass",
                 out_count(n, 0x90u, 0, 60) == 4u && out_count(n, 0x80u, 0, 60) == 4u);
    return bad;
}

static int drums_arp_test(void)
{
    int bad = 0;
    uint32_t n;
    track_t *t;
    out_reset();                                         /* drum hits: GM notes, accent 127 */
    song.g[G_MIDI] = 1;
    t = out_track(100, 0);
    set_engine_of(t, ENGI_DRUM);
    t->engine = t->eng_req;
    t->step[0] = (step_t){{0}, 0, ST_NOTE, 0, 100, 1u << DV_KICK | 1u << DV_HATC, 1u << DV_KICK};
    seq_start();
    out_run(t, out_passes(t, 1));
    n = out_drain();
    bad += check("drum hits go out as their GM notes (kick 36 accented 127, hat 42 at 100)",
                 out_count(n, 0x90u, 0, DRUM_LANE_NOTE[DV_KICK]) == 1u &&
                 out_vel(n, 0, DRUM_LANE_NOTE[DV_KICK]) == 127u &&
                 out_count(n, 0x90u, 0, DRUM_LANE_NOTE[DV_HATC]) == 1u &&
                 out_vel(n, 0, DRUM_LANE_NOTE[DV_HATC]) == 100u);
    out_reset();                                         /* the arp goes out too, and ends */
    song.g[G_MIDI] = 1;
    t = &trk[0];
    track_defaults_steps(t);
    t->p[P_AMODE] = AM_UP;
    t->p[P_ARATE] = 2;
    t->held[0] = 60; t->held[1] = 64; t->nheld = 2;
    for (uint32_t b = 0; b < 400u && !t->arp_note; b++)
        arp_step(t, CTL, CTL);
    n = out_drain();
    bad += check("the arp's note goes out on the track's channel",
                 t->arp_note && out_count(n, 0x90u, 0, t->arp_note) >= 1u);
    t->nheld = 0;
    arp_step(t, CTL, CTL);
    n = out_drain();
    bad += check("  letting go ends it", out_count(n, 0x80u, 0, 60) + out_count(n, 0x80u, 0, 64) >= 1u && !out_hanging());
    return bad;
}

static int ends_test(void)
{
    int bad = 0;
    uint32_t n;
    track_t *t;
    out_reset();                                         /* STOP with a hanging note ends it */
    song.g[G_MIDI] = 1;
    t = out_track(100, 0);
    t->p[P_SGATE] = 127;                                 /* a gate past the step: still on at STOP */
    out_run(t, 1);
    (void)out_drain();
    seq_stop();
    n = out_drain();
    bad += check("STOP ends a hanging MIDI OUT note, none left on any track",
                 out_count(n, 0x80u, 0, 60) == 1u && !out_hanging());
    out_reset();                                         /* back to KEYS with one hanging: it ends */
    song.g[G_MIDI] = 1;
    t = out_track(100, 0);
    t->p[P_SGATE] = 127;
    out_run(t, 1);
    (void)out_drain();
    song.g[G_MIDI] = 0;
    events_block(CTL);
    n = out_drain();
    bad += check("MIDI OUT back to KEYS ends what SEQ had sent", out_count(n, 0x80u, 0, 60) == 1u && !out_hanging());
    out_reset();                                         /* STEP PREVIEW sounds locally only */
    song.g[G_MIDI] = 1;
    t = out_track(100, 0);
    out_run(t, 1);
    (void)out_drain();
    seq_stop();
    (void)out_drain();
    aud_req = (uint16_t)(0u << 8 | 1u);                  /* step 0 of track 1 */
    events_block(CTL);
    n = out_drain();
    bad += check("STEP PREVIEW's audition sends nothing to MIDI OUT (it plays locally)",
                 n == 0 && t->seq_n > 0);
    seq_stop();
    return bad;
}

static int menu_store_test(void)
{
    int bad = 0, ok;
    out_reset();
    menu_put(MI_MIDIOUT, 1u);
    ok = song.g[G_MIDI] == 1 && menu_get(MI_MIDIOUT) == 1u && str_eq(menu_vname(MI_MIDIOUT, 1u), "SEQ");
    menu_put(MI_TXCH1, 5u);
    menu_put(MI_TXCH4, TX_OFF);
    ok &= tx_raw(0) == 5u && tx_raw(3) == TX_OFF && menu_get(MI_TXCH1) == 5u &&
          str_eq(menu_vname(MI_TXCH1, 5u), "5") && str_eq(menu_vname(MI_TXCH4, TX_OFF), "OFF") &&
          str_eq(menu_vname(MI_TXCH2, 0u), "FOLLOW");
    project_save(2);
    song.g[G_MIDI] = 0; song.g[G_DRLVL] = 0; song.g[G_DRREV] = 0;
    project_load(2);
    bad += check("MENU > MIDI > MIDI OUT / TX: the project's G_MIDI and packed TX channels, names, round trip",
                 ok && song.g[G_MIDI] == 1 && tx_raw(0) == 5u && tx_raw(3) == TX_OFF);
    return bad;
}

int main(void)
{
    int bad = mode_test() + channels_test() + notes_test() + drums_arp_test() + ends_test() + menu_store_test();
    printf("%s\n", bad ? "MIDI OUT TEST FAILED" : "MIDI OUT (OPQuencer M0) tests passed");
    return bad != 0;
}
