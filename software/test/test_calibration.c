/* Host tests for software/calibration.c.
 *
 * This file is NOT part of the firmware build (see software/test/Makefile).
 * It compiles the real module against a RAM-simulated EEPROM and checks the
 * behaviour that the firmware depends on:
 *
 *   1. the CCR1 table of the compile-time defaults
 *   2. the two-point solver against the Appendix B vectors
 *   3. the regression vectors that must be rejected as unrealizable
 *   4. the invalid input rejection
 *   5. the CRC detection of every single bit flip in the protected area
 *   6. that booting never writes the EEPROM
 *   7. the applied-current error of an accepted fit
 *   8. the property "every accepted fit is realizable at both points"
 *
 * The firmware itself is 32-bit integer only. The host test uses double for
 * the simulated plant of test 7 only.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>

#include "calibration.h"
#include "load.h"     /* cal_m / cal_t, provided by load.c on the target */
#include "config.h"
#include "systick.h"  /* systick / systick_flag */

/* ---- RAM-simulated EEPROM ---- */
#define EE_SIZE 128
static uint8_t ee[EE_SIZE];
static uint32_t write_count;

uint8_t eeprom_read(uint16_t address)
{
    return ee[address];
}

void eeprom_write(uint16_t address, uint8_t data)
{
    if (ee[address] != data) ee[address] = data;
    write_count++;
}

/* ---- globals the module expects from systick.c / load.c ---- */
volatile uint32_t systick;
volatile uint8_t systick_flag;
uint32_t cal_m;
int32_t cal_t;

/* ---- CRC-8 helper, copy of cal_crc8() in calibration.c ---- */
static uint8_t test_crc8(const cal_block_t *block)
{
    const uint8_t *data = (const uint8_t*)block;
    uint8_t crc = 0;
    for (uint8_t i = 1; i < CAL_EEPROM_SIZE; i++) {
        if (i == 3) continue; /* the crc byte itself is not covered */
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
    return crc;
}

/* ---- Unclamped PWM, copy of cal_pwm_raw() in calibration.c ---- */
static int32_t test_pwm_raw(uint32_t m, int32_t t, uint16_t current)
{
    uint16_t m_q4 = (uint16_t)((m + 8) >> 4);
    int32_t t_q4 = t >> 4;
    return (((int32_t)current * (int32_t)m_q4) - t_q4) >> 12;
}

/* ---- Mirror of the documented two-point fit, used to show why a fit is
 *      unrealizable. Test only, the module has its own implementation. ---- */
static void test_fit(uint16_t i_lo, uint16_t i_hi, uint16_t a_lo, uint16_t a_hi,
                      uint32_t *m_out, int32_t *t_out)
{
    int32_t a12 = ((int32_t)(a_hi - a_lo) * 4096) / (int32_t)(i_hi - i_lo);
    int32_t b = a_lo - ((a12 * (int32_t)i_lo) / 4096);
    uint32_t m_new = ((uint32_t)cal_m * 4096UL + (uint32_t)(a12 / 2)) / (uint32_t)a12;
    *m_out = m_new;
    *t_out = cal_t + b * (int32_t)m_new;
}

/* ---- test bookkeeping ---- */
static int failures;

static void check(bool ok, const char *what)
{
    printf("  %-62s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) failures++;
}

static void ee_fill(uint8_t value)
{
    memset(ee, value, sizeof(ee));
    write_count = 0;
}

/* Store a block in the simulated EEPROM (as a previous firmware version or
 * the wizard would have written it). */
static void store_block(uint8_t flags, uint32_t m, int32_t t, uint16_t lo, uint16_t hi)
{
    cal_block_t b;
    memset(&b, 0, sizeof(b));
    b.magic = CAL_MAGIC;
    b.version = CAL_VERSION;
    b.flags = flags;
    b.curr_lo = lo;
    b.curr_hi = hi;
    b.curr_m = m;
    b.curr_t = t;
    b.crc = test_crc8(&b);
    memcpy(ee + CAL_EEPROM_OFFSET, &b, sizeof(b));
}

/* ======================================================================== */
/* 1. CCR1 table                                                            */
/* ======================================================================== */
static int test_ccr1_table(void)
{
    printf("=== 1. CCR1 table (blank EEPROM -> compile-time defaults) ===\n");
    int before = failures;

    ee_fill(0x00);
    cal_m = 0; cal_t = 0; /* poison: init must replace it */
    calibration_init();
    check(cal_m == (uint32_t)LOAD_CAL_M && cal_t == LOAD_CAL_T,
          "blank EEPROM: init installs the compile-time defaults");
    check(write_count == 0, "blank EEPROM: init writes nothing");

    static const uint16_t req_i[] = {0, 200, 500, 1000, 3000, 5000, 10000};
    static const uint16_t want[]  = {0, 934, 2539, 5212, 15907, 26602, 53339};
    bool table_ok = true;
    for (unsigned k = 0; k < sizeof(req_i) / sizeof(req_i[0]); k++) {
        uint16_t pwm = calibration_current_to_pwm(req_i[k]);
        printf("    I=%5u -> PWM=%5u (want %5u)\n", req_i[k], pwm, want[k]);
        if (pwm != want[k]) table_ok = false;
    }
    check(table_ok, "CCR1 table matches for all 7 setpoints");

    bool mono = true;
    uint16_t prev = calibration_current_to_pwm(CUR_MIN);
    for (uint32_t i = (uint32_t)CUR_MIN + 1; i <= (uint32_t)CUR_MAX; i++) {
        uint16_t pwm = calibration_current_to_pwm((uint16_t)i);
        if (pwm < prev) { mono = false; break; }
        prev = pwm;
    }
    check(mono, "PWM is monotonic non-decreasing over 200..10000 mA");

    return failures > before ? 1 : 0;
}

/* ======================================================================== */
/* 2. Appendix B vectors                                                    */
/* ======================================================================== */
struct vector {
    const char *name;
    uint16_t i1, i2;      /* applied setpoints (mA) */
    uint16_t a1, a2;      /* currents measured on the meter (mA) */
    uint32_t m;           /* expected slope after the fit */
    int32_t t;            /* expected offset after the fit */
    int a_num, a_den;     /* simulated plant gain */
    int b;                /* simulated plant offset (mA) */
};

static const struct vector vectors[] = {
    {"perfect    (a=1.0, b=0)   ", 500, 3000,  500, 3000,
     350445L,   8821987L, 10, 10,    0},
    {"gain +10%  (a=1.1, b=0)   ", 500, 3000,  550, 3300,
     318629L,   9140616L, 11, 10,    0},
    {"offset 200 (a=1.0, b=200) ", 500, 3000,  700, 3200,
     350445L,  78910987L, 10, 10,  200},
    {"gain -10%, off -100       ", 500, 3000,  350, 2600,
     389426L, -29731187L,  9, 10, -100},
};

static int test_appendix_b(void)
{
    printf("=== 2. Appendix B vectors (exact m'/t') ===\n");
    int before = failures;

    for (unsigned v = 0; v < sizeof(vectors) / sizeof(vectors[0]); v++) {
        const struct vector *vec = &vectors[v];
        cal_m = LOAD_CAL_M; cal_t = LOAD_CAL_T;
        uint32_t m_new = 0; int32_t t_new = 0;
        cal_result_t r = calibration_solve(vec->i1, vec->i2, vec->a1, vec->a2,
                                           &m_new, &t_new);
        printf("    %s -> result=%d m'=%lu t'=%ld\n", vec->name, (int)r,
               (unsigned long)m_new, (long)t_new);
        if (r != CAL_ERR_NONE) {
            printf("      FAIL: expected CAL_ERR_NONE\n");
            failures++;
            continue;
        }
        if (m_new != vec->m || t_new != vec->t) {
            printf("      FAIL: expected m'=%lu t'=%ld\n",
                   (unsigned long)vec->m, (long)vec->t);
            failures++;
        }
    }
    check(failures == before, "4 vectors: fit accepted, exact m'/t' unchanged");

    return failures > before ? 1 : 0;
}

/* ======================================================================== */
/* 3. Regression: unrealizable fits must be rejected                       */
/* ======================================================================== */
struct unrealizable {
    const char *name;
    uint16_t i1, i2, a1, a2;
    uint32_t m;           /* the fit the module used to accept */
    int32_t t;
    int32_t p_lo;         /* unclamped PWM at the low point */
    int32_t p_hi;         /* unclamped PWM at the high point */
};

static const struct unrealizable bad_fits[] = {
    {"(205, 413, 2385, 2777)     ", 205, 413, 2385, 2777,
     185960L,  380556027L, -5226,  -4635},
    {"(1000, 10000, 224, 4937)   ", 1000, 10000, 224, 4937,
     669507L, -191360606L, 13135, 105078},
};

static int test_unrealizable(void)
{
    printf("=== 3. Regression: unrealizable fits are rejected ===\n");
    int before = failures;

    for (unsigned k = 0; k < sizeof(bad_fits) / sizeof(bad_fits[0]); k++) {
        const struct unrealizable *bad = &bad_fits[k];
        cal_m = LOAD_CAL_M; cal_t = LOAD_CAL_T;
        uint32_t m_new = 0xDEAD; int32_t t_new = 0xDEAD;
        cal_result_t r = calibration_solve(bad->i1, bad->i2, bad->a1, bad->a2,
                                           &m_new, &t_new);
        printf("    %s -> result=%d (want CAL_ERR_PWM=%d)\n",
               bad->name, (int)r, (int)CAL_ERR_PWM);
        if (r != CAL_ERR_PWM) {
            printf("      FAIL: expected CAL_ERR_PWM\n");
            failures++;
        }
        if (m_new != 0xDEAD || t_new != 0xDEAD) {
            printf("      FAIL: outputs must stay untouched on error\n");
            failures++;
        }
        /* Show why: the fit the module used to accept needs PWM codes
         * outside the range the plant can produce. */
        uint32_t m_fit; int32_t t_fit;
        test_fit(bad->i1, bad->i2, bad->a1, bad->a2, &m_fit, &t_fit);
        int32_t p_lo = test_pwm_raw(m_fit, t_fit, bad->i1);
        int32_t p_hi = test_pwm_raw(m_fit, t_fit, bad->i2);
        printf("      fit m'=%lu t'=%ld -> PWM at points %ld / %ld (want %ld / %ld)\n",
               (unsigned long)m_fit, (long)t_fit, (long)p_lo, (long)p_hi,
               (long)bad->p_lo, (long)bad->p_hi);
        if (m_fit != bad->m || t_fit != bad->t) {
            printf("      FAIL: fit is not the one of the review\n");
            failures++;
        }
        if (p_lo != bad->p_lo || p_hi != bad->p_hi) {
            printf("      FAIL: PWM values differ from the review\n");
            failures++;
        }
        /* With those constants the runtime conversion would clamp. */
        cal_m = m_fit; cal_t = t_fit;
        uint16_t p_lo_clamped = calibration_current_to_pwm(bad->i1);
        uint16_t p_hi_clamped = calibration_current_to_pwm(bad->i2);
        printf("      clamped at runtime: %u / %u\n", p_lo_clamped, p_hi_clamped);
        if (p_lo_clamped != 0 && p_hi_clamped != 0xFFFF) {
            printf("      FAIL: the points must not be realizable\n");
            failures++;
        }
    }
    check(failures == before, "both counterexamples now return CAL_ERR_PWM");

    return failures > before ? 1 : 0;
}

/* ======================================================================== */
/* 4. Invalid inputs                                                        */
/* ======================================================================== */
static int test_invalid(void)
{
    printf("=== 4. Invalid inputs (rejection list unchanged) ===\n");
    int before = failures;

    struct { const char *name; uint16_t i1, i2, a1, a2; cal_result_t want; } cases[] = {
        {"I2 == I1 duplicate            ",   500,   500,   500,   500, CAL_ERR_RANGE},
        {"I2 < I1 descending            ",  3000,   500,  3000,   500, CAL_ERR_RANGE},
        {"I gap 100 < 200               ",   500,   600,   500,   600, CAL_ERR_RANGE},
        {"I lo < CUR_MIN (100)          ",   100,  3000,   100,  3000, CAL_ERR_RANGE},
        {"I hi > CUR_MAX (11000)        ",   500, 11000,   500, 11000, CAL_ERR_RANGE},
        {"A lo < CUR_MIN (150)          ",   500,  3000,   150,  3000, CAL_ERR_RANGE},
        {"A hi > CUR_MAX (10500)        ",   500,  3000,   500, 10500, CAL_ERR_RANGE},
        {"A2 == A1 duplicate            ",   500,  3000,   500,   500, CAL_ERR_RANGE},
        {"A2 < A1 descending            ",   500,  3000,  3000,   500, CAL_ERR_RANGE},
        {"A gap 50 < 100                ",   500,  3000,   500,   550, CAL_ERR_RANGE},
        {"gain 0.40 (a12=1638)          ",   500,  3000,   200,  1200, CAL_ERR_GAIN},
        {"gain 2.20 (a12=9011)          ",   500,  3000,  1100,  6600, CAL_ERR_GAIN},
        {"gain 0.50 border (a12=2048)   ",   500,  3500,   250,  1750, CAL_ERR_SLOPE},
        {"|b|=2500 > 2000               ",   500,  3000,  3000,  5500, CAL_ERR_OFFSET},
        {"t'=534e6 > 5e8 (b=1500)       ",   500,  3000,  2000,  4500, CAL_ERR_OFFSET},
        {"control (perfectly valid)     ",   500,  3000,   500,  3000, CAL_ERR_NONE},
    };

    bool ok = true;
    for (unsigned k = 0; k < sizeof(cases) / sizeof(cases[0]); k++) {
        cal_m = LOAD_CAL_M; cal_t = LOAD_CAL_T;
        uint32_t m_new = 0; int32_t t_new = 0;
        uint32_t m_ref = cal_m; int32_t t_ref = cal_t;
        cal_result_t r = calibration_solve(cases[k].i1, cases[k].i2,
                                           cases[k].a1, cases[k].a2,
                                           &m_new, &t_new);
        bool untouched = (cal_m == m_ref && cal_t == t_ref);
        printf("    %s got=%d want=%d\n", cases[k].name, (int)r, (int)cases[k].want);
        if (r != cases[k].want) ok = false;
        if (cases[k].want != CAL_ERR_NONE && !untouched) ok = false;
    }
    check(ok, "16 cases: rejected as before, reference calibration untouched");

    return failures > before ? 1 : 0;
}

/* ======================================================================== */
/* 5. CRC: every single bit flip in the protected area is detected          */
/* ======================================================================== */
static int test_crc_bits(void)
{
    printf("=== 5. CRC: single bit flips in bytes 1..39 ===\n");
    int before = failures;

    const uint32_t m = 318629L;
    const int32_t t = 9140616L;

    ee_fill(0xFF);
    store_block(CAL_FLAG_CURRENT, m, t, 500, 3000);
    cal_m = 0; cal_t = 0;
    calibration_init();
    check(cal_m == m && cal_t == t, "setup: the valid block is loaded");

    unsigned detected = 0, total = 0;
    for (unsigned byte = 1; byte < CAL_EEPROM_SIZE; byte++) {
        for (unsigned bit = 0; bit < 8; bit++) {
            uint8_t mask = (uint8_t)(1u << bit);
            uint8_t saved = ee[CAL_EEPROM_OFFSET + byte];
            ee[CAL_EEPROM_OFFSET + byte] = (uint8_t)(saved ^ mask);
            cal_m = 0; cal_t = 0;
            calibration_init();
            total++;
            if (cal_m == (uint32_t)LOAD_CAL_M && cal_t == LOAD_CAL_T) detected++;
            ee[CAL_EEPROM_OFFSET + byte] = saved;
        }
    }
    printf("    %u of %u bit flips detected\n", detected, total);
    check(total == 312, "312 bit flips in bytes 1..39 checked");
    check(detected == total, "every single bit flip is detected");

    return failures > before ? 1 : 0;
}

/* ======================================================================== */
/* 6. Booting never writes the EEPROM                                       */
/* ======================================================================== */
static int test_boot_no_write(void)
{
    printf("=== 6. Boot never writes the EEPROM ===\n");
    int before = failures;

    ee_fill(0x00);
    cal_m = 0; cal_t = 0;
    calibration_init();
    check(cal_m == (uint32_t)LOAD_CAL_M && cal_t == LOAD_CAL_T && write_count == 0,
          "blank block: defaults installed, no write");

    ee_fill(0xFF);
    cal_m = 0; cal_t = 0;
    calibration_init();
    check(cal_m == (uint32_t)LOAD_CAL_M && cal_t == LOAD_CAL_T && write_count == 0,
          "erased block: defaults installed, no write");

    ee_fill(0xFF);
    store_block(CAL_FLAG_CURRENT, 318629L, 9140616L, 500, 3000);
    ee[CAL_EEPROM_OFFSET + 8] ^= 0x40; /* corrupt the slope */
    cal_m = 0; cal_t = 0;
    calibration_init();
    check(cal_m == (uint32_t)LOAD_CAL_M && cal_t == LOAD_CAL_T && write_count == 0,
          "corrupt block: defaults installed, no write");

    return failures > before ? 1 : 0;
}

/* ======================================================================== */
/* 7. Applied-current error of an accepted fit                              */
/* ======================================================================== */
/* Simulated plant: a PWM code p delivers the current
 *   I_stock(p) = (p * 2^16 + LOAD_CAL_T) / LOAD_CAL_M      (stock constants)
 *   A(p)       = a * I_stock(p) + b                        (plant gain/offset)
 * The firmware asks for the setpoint, the plant delivers A: the error is
 * |A - setpoint| / setpoint.
 */
static int test_applied_error(void)
{
    printf("=== 7. Applied-current error at both points (<= 0.5%) ===\n");
    int before = failures;

    for (unsigned v = 0; v < sizeof(vectors) / sizeof(vectors[0]); v++) {
        const struct vector *vec = &vectors[v];
        cal_m = LOAD_CAL_M; cal_t = LOAD_CAL_T;
        uint32_t m_new = 0; int32_t t_new = 0;
        cal_result_t r = calibration_solve(vec->i1, vec->i2, vec->a1, vec->a2,
                                           &m_new, &t_new);
        if (r != CAL_ERR_NONE) {
            printf("    %s -> not accepted (%d)\n", vec->name, (int)r);
            failures++;
            continue;
        }
        /* Install the fitted constants, as the wizard would do. */
        cal_m = m_new; cal_t = t_new;
        const uint16_t points[2] = {vec->i1, vec->i2};
        for (unsigned p = 0; p < 2; p++) {
            uint16_t setpoint = points[p];
            uint16_t pwm = calibration_current_to_pwm(setpoint);
            double i_stock = ((double)pwm * 65536.0 + (double)LOAD_CAL_T) /
                             (double)LOAD_CAL_M;
            double actual = (double)vec->a_num / (double)vec->a_den * i_stock +
                            (double)vec->b;
            double err = (actual - (double)setpoint < 0.0 ? (double)setpoint - actual
                                                          : actual - (double)setpoint) /
                         (double)setpoint * 100.0;
            printf("    %s setpoint=%5u PWM=%5u -> %8.2f mA, error %.4f%%\n",
                   vec->name, setpoint, pwm, actual, err);
            if (err > 0.5) {
                printf("      FAIL: applied-current error > 0.5%%\n");
                failures++;
            }
        }
    }
    check(failures == before, "accepted vectors: error <= 0.5% at both points");

    return failures > before ? 1 : 0;
}

/* ======================================================================== */
/* 8. Property: every accepted fit is realizable at both points            */
/* ======================================================================== */
static uint32_t rng_state = 0x12345678u;

static uint32_t rng_next(void)
{
    /* xorshift32, seeded: the run is reproducible */
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

static uint32_t rng_range(uint32_t lo, uint32_t hi)
{
    return lo + rng_next() % (hi - lo + 1);
}

static int test_property(void)
{
    printf("=== 8. Property: every accepted fit is realizable (10000 cases) ===\n");
    int before = failures;

    unsigned accepted = 0, rejected = 0, skipped = 0;
    for (unsigned n = 0; n < 10000; n++) {
        /* Plausible calibration points: the measured gain is inside the
         * documented range, so most cases are accepted. */
        uint16_t i1 = (uint16_t)rng_range(CUR_MIN, CUR_MAX - 2000);
        uint16_t i2 = (uint16_t)(i1 + rng_range(200, 2000));
        if (i2 > CUR_MAX) { skipped++; continue; }
        uint16_t a1 = (uint16_t)rng_range(CUR_MIN, CUR_MAX - 3000);
        uint32_t gap = rng_range(100, 2000);
        uint32_t a2 = (uint32_t)a1 + gap;
        if (a2 > CUR_MAX) { skipped++; continue; }
        /* keep the gain inside 0.5 .. 2.0 */
        uint32_t max_gap = (uint32_t)(i2 - i1) * 2;
        if (gap > max_gap) gap = max_gap;
        if (gap < 100) { skipped++; continue; }
        a2 = (uint32_t)a1 + gap;

        cal_m = LOAD_CAL_M; cal_t = LOAD_CAL_T;
        uint32_t m_new = 0; int32_t t_new = 0;
        cal_result_t r = calibration_solve(i1, i2, a1, (uint16_t)a2,
                                           &m_new, &t_new);
        if (r != CAL_ERR_NONE) { rejected++; continue; }
        accepted++;
        int32_t p_lo = test_pwm_raw(m_new, t_new, i1);
        int32_t p_hi = test_pwm_raw(m_new, t_new, i2);
        if (!(p_lo > 0 && p_lo <= 0xFFFF && p_hi > 0 && p_hi <= 0xFFFF)) {
            printf("    FAIL: i=(%u,%u) a=(%u,%u) m'=%lu t'=%ld -> PWM %ld / %ld\n",
                   i1, i2, a1, a2, (unsigned long)m_new, (long)t_new,
                   (long)p_lo, (long)p_hi);
            failures++;
            break;
        }
    }
    printf("    %u accepted, %u rejected, %u skipped\n", accepted, rejected, skipped);
    check(accepted >= 100, "the property test is not vacuous");
    check(failures == before, "every accepted fit is realizable at both points");

    return failures > before ? 1 : 0;
}

/* ======================================================================== */
int main(void)
{
    printf("ZPB30A1 runtime calibration host tests\n");
    printf("defaults: LOAD_CAL_M=%ld LOAD_CAL_T=%ld\n\n",
           (long)LOAD_CAL_M, (long)LOAD_CAL_T);

    int failed = 0;
    failed += test_ccr1_table();
    failed += test_appendix_b();
    failed += test_unrealizable();
    failed += test_invalid();
    failed += test_crc_bits();
    failed += test_boot_no_write();
    failed += test_applied_error();
    failed += test_property();

    printf("\nRESULT: %d failure(s)\n", failures);
    return failed;
}
