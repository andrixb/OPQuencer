/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Andrix Bley (@andrixb) */
/* OPQuencer M1: controlled chance (Eloquencer-style, track-level; SEQ > CHANCE): the CV (pitch)
 * variation probability + range, the ratchet probability + feel, on the real sequencer and UI
 * (with the stubs of tests/ui_test.c):
 *   ENGINE    CV variation rolled once per pass (one delta for the step's notes, snapped to the track's
 *             scale; lane hits never vary; locks shape the roll), ratchet probability (else x1), PUSH/DRAG
 *             part timing; MIDI OUT carries the varied notes; STEP PREVIEW sounds the step as written.
 *   UI        SEQ goes STEP -> DETAIL -> CHANCE -> AUTOMATION; KNOB 1..4 the track's CV-P CV-R RT-P RT-T
 *             (a sound page: values, locks on held steps, AUTOMATION rows, HELP hint; knob turns are the
 *             sound's, like ARP's, while locks undo with SAVE held).
 *   STORE     the four take FUN10's last spare (P_COUNT 115, P_E0 107; old projects load them OFF, new ones
 *             refuse on old firmware as 1.5's did); user presets map by count.
 * Built and run by tests/run_tests.sh. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

#define LG_MAX 2048
static uint8_t lg_st[LG_MAX], lg_nt[LG_MAX];
static uint32_t lg_n;
/* blocks run, every MIDI packet logged; the transport stopped after (hanging notes end: offs, not ons) */
static void run_logged(track_t *t, uint32_t blocks)
{
    uint32_t b;
    lg_n = 0;
    for (b = 0; b < blocks; b++) {
        seq_tick(t, CTL);
        while (mo_r != mo_w && lg_n < LG_MAX) {
            uint32_t pkt = midi_out_q[mo_r % MQ];
            mo_r++;
            lg_st[lg_n] = (uint8_t)((pkt >> 8) & 0xF0u);
            lg_nt[lg_n] = (uint8_t)((pkt >> 16) & 0x7Fu);
            lg_n++;
        }
    }
    seq_stop();
    while (mo_r != mo_w && lg_n < LG_MAX) {
        uint32_t pkt = midi_out_q[mo_r % MQ];
        mo_r++;
        lg_st[lg_n] = (uint8_t)((pkt >> 8) & 0xF0u);
        lg_nt[lg_n] = (uint8_t)((pkt >> 16) & 0x7Fu);
        lg_n++;
    }
}
/* first `k` note-on pitches of the log (one per pass, in order); returns how many found */
static uint32_t logged_ons(uint32_t *out, uint32_t k)
{
    uint32_t i, n = 0;
    for (i = 0; i < lg_n && n < k; i++)
        if (lg_st[i] == 0x90u)
            out[n++] = lg_nt[i];
    return n;
}

/* track 1 alone, a one-step NOTE loop, MIDI OUT quiet, transport stopped (each test starts it
 * after setup, so counting starts at a clean pass boundary) */
static track_t *cone_step(uint32_t vel)
{
    track_t *t;
    ui_power_on();
    seq_stop();
    usb.config = 1;
    mo_w = mo_r = 0;
    memset(mo_set, 0, sizeof mo_set);
    memset(mo_ch, 0, sizeof mo_ch);
    mo_any = 0;
    song.g[G_MIDI] = 0;
    song.g[G_DRLVL] = 0;
    song.g[G_DRREV] = 0;
    song.g[G_SWING] = 0;
    t = &trk[0];
    track_defaults_steps(t);
    t->p[P_SLEN] = 1;
    t->p[P_SGATE] = 64;
    t->p[P_SSWING] = 0;
    t->step[0] = (step_t){{60}, 1, ST_NOTE, 0, (uint8_t)vel};
    return t;
}
static uint32_t cone_len(const track_t *t) { return step_samples(t, div_samples((uint32_t)t->p[P_SDIV]), 0); }
/* blocks for npass passes plus one step of slack */
static uint32_t cone_blocks(const track_t *t, uint32_t npass) { return (npass + 1u) * cone_len(t) / CTL; }
/* npass passes of a one-step loop; ons[k] = voice starts in pass k (ratchet_test's idiom) */
static void run_passes(track_t *t, uint32_t npass, uint32_t *ons)
{
    uint32_t p = 0, n = 0, v0;
    for (uint32_t k = 0; k < npass; k++)
        ons[k] = 0;
    while (p <= npass) {
        v0 = vage;
        seq_tick(t, CTL);
        if (t->seq_pos < CTL) {                  /* (this block started a pass: count the one before) */
            if (p > 0u && p - 1u < npass)
                ons[p - 1u] = n;
            p++;
            n = 0;
        }
        n += vage - v0;
    }
}
/* voice-start sample offsets over `blocks` blocks; at[k] the k-th */
static uint32_t run_onsets(track_t *t, uint32_t blocks, uint32_t *at, uint32_t nat)
{
    uint32_t b, n = 0, v0;
    for (b = 0; b < blocks; b++) {
        v0 = vage;
        seq_tick(t, CTL);
        for (; v0 < vage; v0++, n++)
            if (at && n < nat)
                at[n] = b * CTL;
    }
    return n;
}
static int in_scale(uint32_t n)   /* C MAJOR mask */
{
    return (0xAB5u >> (n % 12u)) & 1u;
}

static int params_store_test(void)
{
    int bad = 0, ok;
    track_t *t = cone_step(100);
    ok = P_E0 == 107u && P_COUNT == 115u && P_E0 < ML_E0;
    ok &= t->p[P_CVVP] == 0 && t->p[P_CVVR] == 0 && t->p[P_RTPR] == 0 && t->p[P_RTTY] == 0;
    t->p[P_CVVP] = 50; t->p[P_CVVR] = 5; t->p[P_RTPR] = 25; t->p[P_RTTY] = 2;
    project_save(2);
    t->p[P_CVVP] = 0; t->p[P_CVVR] = 0; t->p[P_RTPR] = 0; t->p[P_RTTY] = 0;
    project_load(2);
    bad += check("M1 params (P_E0 107, P_COUNT 115): OFF defaults, project round trip", ok &&
                 t->p[P_CVVP] == 50 && t->p[P_CVVR] == 5 && t->p[P_RTPR] == 25 && t->p[P_RTTY] == 2);
    return bad;
}

static int cv_engine_test(void)
{
    int bad = 0, ok;
    uint32_t i, lo = 127, hi = 0, distinct = 0, last = 300, got[8];
    track_t *t = cone_step(100);                     /* OFF: every pass the written note */
    song.g[G_MIDI] = 1;
    seq_start();
    run_logged(t, cone_blocks(t, 4));
    bad += check("CV variation OFF: every pass plays 60", logged_ons(got, 4) == 4u && got[0] == 60u &&
                 got[1] == 60u && got[2] == 60u && got[3] == 60u);
    t->p[P_CVVP] = 100; t->p[P_CVVR] = 5;            /* always, +-1..5 (CHR: as-is) */
    seq_start();
    run_logged(t, cone_blocks(t, 8));
    if (logged_ons(got, 8) != 8u)
        ok = 0;
    else {
        ok = 1;
        for (i = 0; i < 8u; i++) {
            lo = got[i] < lo ? got[i] : lo;
            hi = got[i] > hi ? got[i] : hi;
            distinct += got[i] != last;
            last = got[i];
        }
        ok &= lo >= 55u && hi <= 65u && lo < 60u && distinct >= 2u;
    }
    bad += check("CV variation always: MIDI OUT carries varied pitches (55..65, never 60: mag 1..5)", ok);
    t->p[P_CVVP] = 0;                                /* a lock shapes the roll: step 1 varies, step 0 does not */
    t->p[P_SLEN] = 2;
    t->step[1] = (step_t){{60}, 1, ST_NOTE, 0, 100};
    motion_set_lock(t, 1, P_CVVP, 100);
    motion_set_lock(t, 1, P_CVVR, 4);
    seq_start();
    run_logged(t, 3u * cone_len(t) / CTL * 2u);
    if (logged_ons(got, 6) != 6u)
        ok = 0;
    else {
        ok = got[0] == 60u && got[2] == 60u && got[4] == 60u;
        lo = 127; hi = 0;
        for (i = 1; i < 6u; i += 2) {
            lo = got[i] < lo ? got[i] : lo;
            hi = got[i] > hi ? got[i] : hi;
        }
        ok &= lo >= 56u && hi <= 64u && lo < 60u;
    }
    bad += check("  a lock varies its step only (step 0 stays 60, step 1 varied 56..64)", ok);
    return bad;
}

static int cv_scale_drums_test(void)
{
    int bad = 0, ok = 1;
    uint32_t i, seen[128] = {0}, distinct = 0, got[12];
    track_t *t = cone_step(100);
    t->p[P_SCALE] = 1;                               /* MAJ, root C */
    t->p[P_CVVP] = 100; t->p[P_CVVR] = 12;
    song.g[G_MIDI] = 1;
    seq_start();
    run_logged(t, cone_blocks(t, 12));
    if (logged_ons(got, 12) != 12u)
        ok = 0;
    else
        for (i = 0; i < 12u; i++) {
            if (got[i] < 48u || got[i] > 72u || !in_scale(got[i])) {
                ok = 0;
                break;
            }
            distinct += !seen[got[i]];
            seen[got[i]] = 1;
        }
    bad += check("CV variation snaps to the track's scale (MAJ, 48..72)", ok && distinct >= 2u);
    t = cone_step(100);                              /* lane hits never vary, on any engine */
    set_engine_of(t, ENGI_DRUM);
    t->engine = t->eng_req;
    t->step[0] = (step_t){{0}, 0, ST_NOTE, 0, 100, 1u << DV_KICK, 0};
    t->p[P_CVVP] = 100; t->p[P_CVVR] = 12;
    song.g[G_MIDI] = 1;
    seq_start();
    run_logged(t, cone_blocks(t, 4));
    if (logged_ons(got, 4) != 4u)
        ok = 0;
    else
        for (i = 0; i < 4u; i++)
            ok &= got[i] == DRUM_LANE_NOTE[DV_KICK];
    bad += check("  drum hits play their GM note through any variation", ok);
    return bad;
}

static int ratchet_prob_test(void)
{
    int bad = 0, ok = 1;
    uint32_t ons[200], i, full = 0, single = 0, v0, vvoices = 0, midi_ons = 0;
    track_t *t = cone_step(100);
    step_set_ratchet(&t->step[0], 3);
    seq_start();
    run_passes(t, 4, ons);
    for (i = 0; i < 4u; i++)
        ok &= ons[i] == 3u;
    bad += check("ratchet probability OFF (0): x3 plays all parts, as before", ok);
    t->p[P_RTPR] = 50;
    song.g[G_MIDI] = 1;
    seq_start();
    {   /* 200 passes: per-pass voice starts (split) and every MIDI note-on (mirror); totals must match.
         * A boundary tick starts the next pass: its packets belong to the new pass, so the exit tick's
         * packets are dropped and its voices uncounted (both describe pass 200, outside the window). */
        uint32_t p = 0, n = 0, midi_n = 0, b, nb = 210u * cone_len(t) / CTL;
        for (i = 0; i < 200u; i++)
            ons[i] = 0;
        for (b = 0; b < nb; b++) {
            uint32_t tickm = 0;
            v0 = vage;
            seq_tick(t, CTL);
            while (mo_r != mo_w) {
                uint32_t pkt = midi_out_q[mo_r % MQ];
                mo_r++;
                tickm += ((pkt >> 8) & 0xF0u) == 0x90u;
            }
            if (t->seq_pos < CTL) {
                if (p > 0u && p - 1u < 200u) {
                    ons[p - 1u] = n;
                    vvoices += n;
                    midi_ons += midi_n;
                }
                p++;
                n = 0;
                midi_n = 0;
                if (p > 200u)
                    break;
            }
            n += vage - v0;
            midi_n += tickm;
        }
        seq_stop();
    }
    for (i = 0; i < 200u; i++) {
        if (ons[i] == 3u)
            full++;
        else if (ons[i] == 1u)
            single++;
    }
    bad += check("ratchet probability 50: full and single passes both happen; MIDI OUT mirrors every trigger",
                 full > 0u && single > 0u && full + single == 200u && midi_ons == vvoices);
    return bad;
}

static int ratchet_type_test(void)
{
    int bad = 0, ok;
    uint32_t at[8], n, len;
    track_t *t = cone_step(100);
    step_set_ratchet(&t->step[0], 2);
    seq_start();
    len = cone_len(t);
    n = run_onsets(t, len / CTL - 1u, at, 8);
    ok = n == 2u && at[1] > len / 2u - 2u * CTL && at[1] < len / 2u + 2u * CTL;
    bad += check("EQUAL x2: parts at 0 and 1/2 (as before)", ok);
    t->p[P_RTTY] = 1;                                /* PUSH: long first, tight tail */
    seq_start();
    n = run_onsets(t, len / CTL - 1u, at, 8);
    ok = n == 2u && at[1] > len * 3u / 4u - 2u * CTL && at[1] < len * 3u / 4u + 2u * CTL;
    bad += check("PUSH x2: parts at 0 and 3/4", ok);
    t->p[P_RTTY] = 2;                                /* DRAG: tight head, long last */
    seq_start();
    n = run_onsets(t, len / CTL - 1u, at, 8);
    ok = n == 2u && at[1] > len / 4u - 2u * CTL && at[1] < len / 4u + 2u * CTL;
    bad += check("DRAG x2: parts at 0 and 1/4", ok);
    step_set_ratchet(&t->step[0], 4);
    t->p[P_RTTY] = 1;
    seq_start();
    n = run_onsets(t, len / CTL - 1u, at, 8);
    ok = n == 4u && at[1] > len * 3u / 4u - 2u * CTL && at[1] < len * 3u / 4u + 2u * CTL;
    bad += check("PUSH x4: four parts, the second at 3/4", ok);
    return bad;
}

static void go_chance(void)
{
    uint32_t i;
    for (i = 0; i < NPAGES && !str_eq(PAGES[i].title, "CHANCE"); i++)
        ;
    ui.home = 0;
    ui.page = (uint8_t)i;
    page_entered();
    frame();
}
static void ckt(uint32_t role, int32_t s)            /* one knob detent (no accel burst) */
{
    memset(ui.enc_t, 0, sizeof ui.enc_t);
    turn(role, s);
}
static void cktn(uint32_t role, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        ckt(role, 1);
}
static int rec_listed(uint32_t step, uint32_t id)    /* AUTOMATION lists a lock of id on step */
{
    uint16_t rw[EV_ROWS];
    uint32_t n = ev_list(rw), r;
    for (r = 0; r < n; r++) {
        uint32_t k = rw[r] >> 8, a = rw[r] & 0xFFu;
        if (k == EVK_REC && (motion.event[a].place & 63u) == step && MOTION_ID(&motion.event[a]) == id)
            return 1;
    }
    return 0;
}

static int ui_test_fn(void)
{
    int bad = 0, ok;
    int16_t v;
    uint32_t i, chance_idx = 0;
    track_t *t = cone_step(100);
    go_chance();
    ok = str_eq(cur_page()->title, "CHANCE") && song.seq_mode;
    {   /* the footer title counts the SEQ pages (STEP DETAIL CHANCE AUTOMATION) */
        char ti[20];
        page_title(ti);
        ok &= str_eq(ti, "CHANCE 3/4");
    }
    ok &= help_page() && str_eq(help_page(), "[K1]CV VAR[K2]RANGE[K4]RTYPE");
    bad += check("SEQ > CHANCE: the page, its 3/4 title, its HELP hint", ok);
    cktn(EN_K1, 30); cktn(EN_K2, 5); cktn(EN_K3, 40); ckt(EN_K4, 1);
    ok = t->p[P_CVVP] == 30 && t->p[P_CVVR] == 5 && t->p[P_RTPR] == 40 && t->p[P_RTTY] == 1;
    bad += check("  KNOB 1..4 set CV-P CV-R RT-P RT-T", ok);
    for (i = 0; i < NPAGES; i++)
        if (str_eq(PAGES[i].title, "CHANCE"))
            chance_idx = i;
    ok = ui.plk_src == chance_idx + 1u;              /* (visiting arms held-step locks of its params) */
    lock_turn(0, 1, 1u << 2);                        /* hold step 2, turn KNOB 1: its CV-P lock +1 */
    ok &= motion_lock_get(t, 2, P_CVVP, &v) && v == t->p[P_CVVP] + 1;
    bad += check("  a held step + KNOB 1 locks CV-P there (the CHANCE page arms it)", ok);
    ok = rec_listed(2, P_CVVP);
    bad += check("  SEQ > AUTOMATION lists the lock", ok);
    hold(B_SAVE);                                    /* (locks undo with SAVE held) */
    ok = !motion_lock_get(t, 2, P_CVVP, &v);
    bad += check("  SAVE held undoes it", ok);
    return bad;
}

static int upreset_test_fn(void)
{
    int bad = 0, ok;
    track_t *t = cone_step(100);
    song.sel = 0;
    seq_stop();
    t->p[P_CVVP] = 77; t->p[P_CVVR] = 9; t->p[P_RTPR] = 33; t->p[P_RTTY] = 2;
    up_store(3, "CHANCETST");
    t->p[P_CVVP] = 0; t->p[P_CVVR] = 0; t->p[P_RTPR] = 0; t->p[P_RTTY] = 0;
    up_load(3);
    ok = t->p[P_CVVP] == 77 && t->p[P_CVVR] == 9 && t->p[P_RTPR] == 33 && t->p[P_RTTY] == 2;
    bad += check("user presets keep the chance by count (old records load it OFF)", ok);
    return bad;
}

int main(void)
{
    int bad = params_store_test() + cv_engine_test() + cv_scale_drums_test() + ratchet_prob_test() +
              ratchet_type_test() + ui_test_fn() + upreset_test_fn();
    printf("%s\n", bad ? "CHANCE TEST FAILED" : "CHANCE (OPQuencer M1) tests passed");
    return bad != 0;
}
