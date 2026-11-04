#include "ay.h"

#include <string.h>

/* AY-3-8912 DAC levels (measured, normalised to 0..32767). */
static const int32_t ay_vol[16] = {
    0, 449, 672, 954, 1386, 2025, 2775, 4486,
    5541, 8673, 11557, 14742, 18691, 22521, 27793, 32767
};

static const uint8_t reg_mask[16] = {
    0xff, 0x0f, 0xff, 0x0f, 0xff, 0x0f, 0x1f, 0xff,
    0x1f, 0x1f, 0x1f, 0xff, 0xff, 0x0f, 0xff, 0xff
};

static void env_start(ay_chip *ay)
{
    uint8_t shape = ay->reg[13];
    ay->env_attack = (shape & 4) ? 1 : 0;
    if (!(shape & 8)) {
        /* Shapes 0-7 behave as "continue, hold at 0". */
        ay->env_hold = 1;
        ay->env_alt = ay->env_attack;
    } else {
        ay->env_hold = shape & 1;
        ay->env_alt = (shape & 2) ? 1 : 0;
    }
    ay->env_step = 15;
    ay->env_holding = 0;
    ay->env_cnt = 0;
}

void ay_reset(ay_chip *ay)
{
    memset(ay, 0, sizeof *ay);
    ay->lfsr = 1;
    ay->reg[7] = 0xff;
    env_start(ay);
}

void ay_write(ay_chip *ay, uint8_t val)
{
    uint8_t r = ay->sel & 0x0f;
    if (ay->sel > 15)
        return;
    ay->reg[r] = val & reg_mask[r];
    if (r == 13)
        env_start(ay);
}

uint8_t ay_read(const ay_chip *ay)
{
    if (ay->sel > 15)
        return 0xff;
    return ay->reg[ay->sel];
}

static int env_level(const ay_chip *ay)
{
    return ay->env_attack ? 15 - ay->env_step : ay->env_step;
}

/* One chip tick = 8 input clocks. */
static void tick(ay_chip *ay)
{
    int ch;
    uint8_t mixer = ay->reg[7];
    uint32_t np, ep;

    for (ch = 0; ch < 3; ch++) {
        uint16_t tp = (uint16_t)(ay->reg[ch * 2] | (ay->reg[ch * 2 + 1] << 8));
        if (!tp)
            tp = 1;
        if (++ay->tone_cnt[ch] >= tp) {
            ay->tone_cnt[ch] = 0;
            ay->tone_out[ch] ^= 1;
        }
    }

    ay->noise_half ^= 1;
    if (ay->noise_half) {
        np = ay->reg[6] ? ay->reg[6] : 1;
        if (++ay->noise_cnt >= np) {
            ay->noise_cnt = 0;
            ay->lfsr = (ay->lfsr >> 1) | (((ay->lfsr ^ (ay->lfsr >> 3)) & 1) << 16);
        }
    }

    ay->env_half ^= 1;
    if (ay->env_half) {
        ep = (uint32_t)(ay->reg[11] | (ay->reg[12] << 8));
        if (!ep)
            ep = 1;
        if (++ay->env_cnt >= ep) {
            ay->env_cnt = 0;
            if (!ay->env_holding) {
                if (--ay->env_step < 0) {
                    if (ay->env_hold) {
                        if (ay->env_alt)
                            ay->env_attack ^= 1;
                        ay->env_holding = 1;
                        ay->env_step = 0;
                    } else {
                        if (ay->env_alt)
                            ay->env_attack ^= 1;
                        ay->env_step = 15;
                    }
                }
            }
        }
    }

    for (ch = 0; ch < 3; ch++) {
        int tone = ay->tone_out[ch] | ((mixer >> ch) & 1);
        int noise = (int)(ay->lfsr & 1) | ((mixer >> (ch + 3)) & 1);
        if (tone && noise) {
            uint8_t v = ay->reg[8 + ch];
            ay->acc[ch] += ay_vol[(v & 0x10) ? env_level(ay) : (v & 0x0f)];
        }
    }
    ay->acc_n++;
}

/* Advance a counter that counts up to period by k ticks (as ++cnt >=
 * period, then 0, once per tick) and return how many times it wrapped. */
static uint32_t count_on(uint32_t *cnt, uint32_t period, uint32_t k)
{
    uint32_t c = *cnt, wraps = 0;
    if (!k)
        return 0;
    if (c + 1 >= period) {          /* wraps on the first tick */
        wraps = 1;
        c = 0;
        k--;
    }
    if (k >= period - c) {
        k -= period - c;
        wraps += 1 + k / period;
        c = k % period;
    } else {
        c += k;
    }
    *cnt = c;
    return wraps;
}

/* n chip ticks while every channel's volume is a fixed 0 (no envelope):
 * the chip adds nothing to its output, so only the counters, the noise
 * shift register and the envelope need to move on, and they can in bulk.
 * Gives the same state as n calls of tick(). */
static void ticks_silent(ay_chip *ay, uint32_t n)
{
    int ch;
    uint32_t k, w, c;

    for (ch = 0; ch < 3; ch++) {
        uint32_t tp = (uint32_t)(ay->reg[ch * 2] | (ay->reg[ch * 2 + 1] << 8));
        if (!tp)
            tp = 1;
        c = ay->tone_cnt[ch];
        w = count_on(&c, tp, n);
        ay->tone_cnt[ch] = (uint16_t)c;
        ay->tone_out[ch] ^= (uint8_t)(w & 1);
    }

    /* noise steps on the ticks where noise_half turns 1 */
    k = ay->noise_half ? n / 2 : (n + 1) / 2;
    ay->noise_half ^= (uint8_t)(n & 1);
    {
        uint32_t np = ay->reg[6] ? ay->reg[6] : 1;
        c = ay->noise_cnt;
        w = count_on(&c, np, k);
        ay->noise_cnt = (uint16_t)c;
        while (w--)
            ay->lfsr = (ay->lfsr >> 1) | (((ay->lfsr ^ (ay->lfsr >> 3)) & 1) << 16);
    }

    k = ay->env_half ? n / 2 : (n + 1) / 2;
    ay->env_half ^= (uint8_t)(n & 1);
    {
        uint32_t ep = (uint32_t)(ay->reg[11] | (ay->reg[12] << 8));
        if (!ep)
            ep = 1;
        c = ay->env_cnt;
        w = count_on(&c, ep, k);
        ay->env_cnt = c;
        while (w-- && !ay->env_holding) {
            if (--ay->env_step < 0) {
                if (ay->env_alt)
                    ay->env_attack ^= 1;
                if (ay->env_hold) {
                    ay->env_holding = 1;
                    ay->env_step = 0;
                } else {
                    ay->env_step = 15;
                }
            }
        }
    }
    ay->acc_n += (int32_t)n;
}

void ay_run(ay_chip *ay, uint32_t clocks)
{
    ay->clk_div += clocks;
#ifndef AY_NO_BULK
    if (ay->clk_div >= 16 && !((ay->reg[8] | ay->reg[9] | ay->reg[10]) & 0x1f)) {
        ticks_silent(ay, ay->clk_div >> 3);
        ay->clk_div &= 7;
        return;
    }
#endif
    while (ay->clk_div >= 8) {
        ay->clk_div -= 8;
        tick(ay);
    }
}

void ay_take(ay_chip *ay, int32_t out[3])
{
    int ch;
    for (ch = 0; ch < 3; ch++) {
        out[ch] = ay->acc_n ? ay->acc[ch] / ay->acc_n : 0;
        ay->acc[ch] = 0;
    }
    ay->acc_n = 0;
}
