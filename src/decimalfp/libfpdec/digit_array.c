/*
------------------------------------------------------------------------------
Name:        digit_array.c

Author:      Michael Amrhein (michael@adrhinum.de)

Copyright:   (c) 2020 ff. Michael Amrhein
License:     This program is part of a larger application or library.
             For license details please read the file LICENSE provided
             together with the application or library.
------------------------------------------------------------------------------
$Source$
$Revision$
*/

#include <assert.h>
#include <malloc.h>
#include <string.h>

#include "basemath.h"
#include "digit_array_.h"
#include "rounding_.h"


/*****************************************************************************
*  Macros
*****************************************************************************/

#define BITS_PER_DIGIT 64
#define BYTES_PER_DIGIT 8                       // BITS_PER_DIGIT / 8
#define DEC_DIGITS_PER_DIGIT 19                 // int(log10(2 ** 64))
#define RADIX 10000000000000000000UL            // 10 ** DEC_DIGITS_PER_DIGIT
#define MAX_DIGIT 9999999999999999999UL         // RADIX - 1


/*****************************************************************************
*  Functions
*****************************************************************************/

// Digit array allocator

static inline fpdec_digit_array_t *
digits_alloc(size_t n_digits) {
    fpdec_digit_array_t *digit_array = (fpdec_digit_array_t *) \
        calloc(n_digits + 1, sizeof(fpdec_digit_t));
    if (digit_array != NULL) {
        digit_array->n_alloc = n_digits;
    }
    return digit_array;
}

// Tests

static inline bool
digits_all_zero(fpdec_digit_t *digits, fpdec_n_digits_t n) {
    for (fpdec_digit_t *digit = digits; digit < digits + n; ++digit)
        if (*digit != 0)
            return false;
    return true;
}

// Constructors

static inline void
digits_set_zero(fpdec_digit_t *digits, fpdec_n_digits_t n) {
    for (fpdec_digit_t *digit = digits; digit < digits + n; ++digit)
        *digit = 0;
}

fpdec_digit_array_t *
digits_copy(const fpdec_digit_array_t *src, fpdec_n_digits_t n_shift,
            fpdec_n_digits_t n_add_leading_zeros) {
    fpdec_digit_array_t *result;

    assert(src != NULL);

    result = digits_alloc(src->n_signif + n_shift + n_add_leading_zeros);
    if (result != NULL) {
        result->n_signif = src->n_signif + n_shift;
        memcpy(result->digits + n_shift, src->digits,
               src->n_signif * sizeof(fpdec_digit_t));
    }
    return result;
}

static inline fpdec_digit_t
dec_digits_to_digit(const dec_digit_t *start, const dec_digit_t *stop) {
    fpdec_digit_t digit = 0;
    for (; start < stop; ++start) {
        digit *= 10;
        digit += *start;
    }
    return digit;
}

error_t
digits_from_dec_coeff_exp(fpdec_digit_array_t **digit_array, fpdec_exp_t *exp,
                          size_t n_dec_digits, const dec_digit_t *coeff,
                          int dec_exp) {
    size_t n_digits, n_dec_shift;
    fpdec_digit_t *digit;
    const dec_digit_t *chunk_start, *chunk_stop;

    assert(n_dec_digits > 0);

    *exp = FLOOR(dec_exp, DEC_DIGITS_PER_DIGIT);
    n_dec_shift = (size_t) MOD(dec_exp, DEC_DIGITS_PER_DIGIT);
    n_digits = CEIL(n_dec_digits + n_dec_shift, DEC_DIGITS_PER_DIGIT);
    *digit_array = digits_alloc(n_digits);
    if (*digit_array == NULL) MEMERROR

    digit = (*digit_array)->digits;
    chunk_stop = coeff + n_dec_digits;
    chunk_start = MAX(chunk_stop + n_dec_shift - DEC_DIGITS_PER_DIGIT, coeff);
    while (chunk_stop > coeff) {
        *digit = dec_digits_to_digit(chunk_start, chunk_stop);
        digit++;
        chunk_stop = chunk_start;
        chunk_start = MAX(chunk_start - DEC_DIGITS_PER_DIGIT, coeff);
    }
    // least significant digit to be shifted?
    if (n_dec_shift > 0) {
        (*digit_array)->digits[0] *= _10_POW_N(n_dec_shift);
    }
    // cut-off leading zeroes
    digit = (*digit_array)->digits + n_digits - 1;
    while (n_digits > 0 && *digit == 0) {
        n_digits--;
        digit--;
    }
    (*digit_array)->n_signif = n_digits;
    return FPDEC_OK;
}

error_t
digits_from_digits(fpdec_digit_array_t **digit_array,
                   const fpdec_digit_t *digits, size_t n_digits) {
    assert(n_digits > 0);
    assert(*digits != 0);

    for (; n_digits > 0 && digits[n_digits - 1] == 0; --n_digits);
    *digit_array = digits_alloc(n_digits);
    if (*digit_array == NULL) MEMERROR

    (*digit_array)->n_signif = n_digits;
    for (size_t i = 0; i < n_digits; ++i)
        (*digit_array)->digits[i] = digits[i];
    return FPDEC_OK;
}

// Normalization

static inline fpdec_digit_t *
least_signif_digit(fpdec_digit_array_t *digit_array) {
    fpdec_digit_t *digit = digit_array->digits;
    fpdec_digit_t *stop = digit_array->digits + digit_array->n_signif;

    while (digit < stop && *digit == 0)
        digit++;
    return digit;
}

fpdec_n_digits_t
digits_eliminate_trailing_zeros(fpdec_digit_array_t *digit_array) {
    fpdec_digit_t *lsd = least_signif_digit(digit_array);
    fpdec_n_digits_t n_trailing_zeros = lsd - digit_array->digits;

    if (n_trailing_zeros > 0) {
        digit_array->n_signif -= n_trailing_zeros;
        for (int i = 0; i < digit_array->n_signif; ++i) {
            digit_array->digits[i] = lsd[i];
        }
    }
    return n_trailing_zeros;
}

// Rounding

bool
digits_round(fpdec_digit_array_t *digit_array, fpdec_sign_t sign,
             size_t n_dec_shift,
             enum FPDEC_ROUNDING_MODE rounding) {
    const fpdec_digit_t *stop = digit_array->digits + digit_array->n_signif;
    fpdec_n_digits_t digit_idx;
    fpdec_digit_t *digit;
    size_t n_dec_digits;

    assert(digit_array->n_signif > 0);
    assert(n_dec_shift > 0);
    assert(n_dec_shift <= digit_array->n_signif * DEC_DIGITS_PER_DIGIT);

    /* digit points to the digit to be rounded, n_dec_digits is the number
       of decimal digits to be rounded */
    digit_idx = FLOOR(n_dec_shift, DEC_DIGITS_PER_DIGIT);
    digit = digit_array->digits + digit_idx;
    n_dec_digits = MOD(n_dec_shift, DEC_DIGITS_PER_DIGIT);
    if (n_dec_digits == 0) {
        // need to round the previous digit ...
        fpdec_digit_t quot = (digit_idx < digit_array->n_signif) ? *digit : 0;
        digit--;
        digit_idx--;
        // ... but only if there is a remainder
        if (*digit != 0 || digit_idx > 0) {
            *digit = RADIX * round_qr(sign, quot, *digit, digit_idx > 0,
                                      RADIX, rounding);
        }
    }
    else {
        *digit = round_to_multiple(sign, *digit, digit_idx > 0,
                                   _10_POW_N(n_dec_digits),
                                   rounding);
    }
    // set all skiped digits to zero
    if (digit_idx > 0)
        digits_set_zero(digit_array->digits, digit_idx);
    // check and propagate carry
    if (*digit == RADIX) {
        do
            *digit = 0;
        while (++digit < stop && ++(*digit) == RADIX);
    }
    return (digit == stop);
}

// Comparison

// Pre-condition: radix points adjusted
int digits_cmp(fpdec_digit_t *x_digits, fpdec_n_digits_t x_n_digits,
               fpdec_digit_t *y_digits, fpdec_n_digits_t y_n_digits) {
    fpdec_digit_t *x_digit = x_digits + x_n_digits - 1;
    fpdec_digit_t *y_digit = y_digits + y_n_digits - 1;
    while (x_digit >= x_digits && y_digit >= y_digits) {
        if (*x_digit != *y_digit)
            return CMP(*x_digit, *y_digit);
        x_digit--;
        y_digit--;
    }
    if (x_n_digits != y_n_digits)
        return CMP(x_n_digits, y_n_digits);
    // x == y
    return 0;
}

// Basic arithmetic operations

void
digits_iadd_digit(fpdec_digit_array_t *x, const fpdec_digit_t y) {
    fpdec_digit_t *digit = x->digits;
    fpdec_digit_t *total_carry_over_digit = x->digits + x->n_signif;

    assert(x->n_signif > 0);
    assert(x->n_alloc > x->n_signif);
    assert(*total_carry_over_digit == 0);

    *digit += y;
    if (*digit < y || *digit >= RADIX) {        // carry-over?
        *digit -= RADIX;
        // propagate carry
        while (*(++digit) == MAX_DIGIT)
            *digit = 0;
        (*digit)++;
        if (*total_carry_over_digit != 0)
            x->n_signif++;
    }
}

void
digits_iadd_digits(fpdec_digit_array_t *x, const fpdec_digit_array_t *y) {
    fpdec_digit_t *x_digit = x->digits;
    fpdec_digit_t *total_carry_over_digit = x->digits + x->n_signif;
    const fpdec_digit_t *y_digit = y->digits;
    const fpdec_digit_t *y_over = y->digits + y->n_signif;
    unsigned carry = 0;

    assert(y->n_signif > 0);
    assert(x->n_alloc > y->n_signif);
    assert(x->n_alloc > x->n_signif);
    assert(*total_carry_over_digit == 0);

    x->n_signif = MAX(x->n_signif, y->n_signif);
    while (y_digit < y_over) {
        *x_digit += *y_digit + carry;
        carry = (*x_digit < *y_digit || *x_digit >= RADIX);
        if (carry)
            *x_digit -= RADIX;
        x_digit++;
        y_digit++;
    }
    // propagate carry
    if (carry) {
        while (*x_digit == MAX_DIGIT) {
            *x_digit = 0;
            x_digit++;
        }
        (*x_digit)++;
        if (*total_carry_over_digit != 0)
            x->n_signif++;
    }
}

void
digits_isub_digits(fpdec_digit_array_t *x, const fpdec_digit_array_t *y) {
    fpdec_digit_t *x_digit = x->digits;
    const fpdec_digit_t *y_digit = y->digits;
    const fpdec_digit_t *y_over = y->digits + y->n_signif;
    fpdec_digit_t d;
    unsigned borrow = 0;

    assert(y->n_signif > 0);
    assert(x->n_signif >= y->n_signif);

    while (y_digit < y_over) {
        d = *x_digit - (*y_digit + borrow);
        borrow = (d > *x_digit);
        *x_digit = borrow ? d + RADIX : d;
        x_digit++;
        y_digit++;
    }
    // propagate borrow
    if (borrow) {
        while (*x_digit == 0) {
            *x_digit = MAX_DIGIT;
            x_digit++;
        }
        (*x_digit)--;
    }
    // total borrow?
    if (x->digits[x->n_signif - 1] == 0)
        x->n_signif--;
}

void
digits_imul_digit(fpdec_digit_array_t *x, fpdec_digit_t y) {
    fpdec_digit_t *x_over = x->digits + x->n_signif;
    uint128_t t;
    fpdec_digit_t k = 0;

    assert(x->n_signif > 0);
    assert(x->n_alloc > x->n_signif);

    for (fpdec_digit_t *d = x->digits; d < x_over; ++d) {
        // *d <= RADIX - 1 and y <= RADIX - 1 and k <= RADIX - 1
        u64_mul_u64(&t, *d, y);
        // t <= RADIX * RADIX - 2 * RADIX + 1
        u128_iadd_u64(&t, k);
        // t <= RADIX * RADIX - RADIX
        *d = u128_idiv_u64(&t, RADIX);
        // t <= RADIX - 1
        // *d <= RADIX - 1
        k = t.lo;
        // k <=RADIX - 1
    }
    if (k > 0) {
        *x_over = k;
        x->n_signif++;
    }
}

// See D. E. Knuth, The Art of Computer Programming, Vol. 2, Ch. 4.3.1,
// Algorithm M
fpdec_digit_array_t *
digits_mul(const fpdec_digit_array_t *x, const fpdec_digit_array_t *y) {
    fpdec_digit_array_t *z;
    fpdec_digit_t *z_digit;
    fpdec_digit_t *z_carry;
    uint128_t t;

    assert(x->n_signif > 0);
    assert(y->n_signif > 0);

    z = digits_alloc(x->n_signif + y->n_signif);
    if (z == NULL) MEMERROR_RETVAL(NULL)

    z_carry = z->digits + x->n_signif;
    for (int j = 0; j < y->n_signif; ++j) {
        z_digit = z->digits + j;
        for (int i = 0; i < x->n_signif; ++i) {
            // x->digits[i] <= RADIX - 1 and y->digits[j] <= RADIX - 1 and
            // *z_digit <= RADIX - 1 and *z_carry <= RADIX - 1
            u64_mul_u64(&t, x->digits[i], y->digits[j]);
            // t <= RADIX * RADIX - 2 * RADIX + 1
            u128_iadd_u64(&t, *z_digit);
            // t <= RADIX * RADIX - RADIX
            u128_iadd_u64(&t, *z_carry);
            // t <= RADIX * RADIX - 1
            *z_digit = u128_idiv_u64(&t, RADIX);
            // t <= RADIX - 1
            // *z_digit <= RADIX - 1
            *z_carry = t.lo;
            // *z_carry <= RADIX - 1
            z_digit++;
        }
        z_carry++;
    }
    z->n_signif = z->n_alloc;
    return z;
}

// See D. E. Knuth, The Art of Computer Programming, Vol. 2, Ch. 4.3.1,
// Exercise 16
fpdec_digit_array_t *
digits_div_digit(const fpdec_digit_array_t *x,
                 const fpdec_n_digits_t n_shift_x,
                 const fpdec_digit_t y, fpdec_digit_t *rem) {
    const fpdec_digit_array_t *xhat;
    fpdec_digit_array_t *q;
    fpdec_digit_t r = 0;
    uint128_t t;

    assert(x->n_signif > 0);
    assert(y > 0);

    q = digits_alloc(x->n_signif + n_shift_x);
    if (q == NULL) MEMERROR_RETVAL(NULL)

    if (n_shift_x > 0) {
        xhat = digits_copy(x, n_shift_x, 0);
        if (xhat == NULL) MEMERROR_RETVAL(NULL)
    }
    else
        xhat = x;
    for (int i = xhat->n_signif - 1; i >= 0; --i) {
        u64_mul_u64(&t, r, RADIX);
        u128_iadd_u64(&t, xhat->digits[i]);
        r = u128_idiv_u64(&t, y);
        assert(t.hi == 0);
        q->digits[i] = t.lo;
    }
    q->n_signif = q->n_alloc;
    if (rem != NULL)
        *rem = r;
    if (xhat != x)
        free((void*) xhat);
    return q;
}

// See D. E. Knuth, The Art of Computer Programming, Vol. 2, Ch. 4.3.1,
// Algorithm D
fpdec_digit_array_t *
digits_divmod(const fpdec_digit_array_t *x, const fpdec_n_digits_t n_shift_x,
              const fpdec_digit_array_t *y, const fpdec_n_digits_t n_shift_y,
              fpdec_digit_array_t **rem) {
    const fpdec_n_digits_t m = x->n_signif + n_shift_x;
    const fpdec_n_digits_t n = y->n_signif + n_shift_y;
    const fpdec_n_digits_t n_1 = n - 1;
    const fpdec_n_digits_t n_2 = n - 2;
    fpdec_digit_t d, qhat, rhat, carry, borrow;
    fpdec_digit_array_t *xd, *yd, *q;
    uint128_t t1, t2;

    assert(y->n_signif > 1 || (y->n_signif == 1 && n_shift_y > 0));
    assert(y->digits[y->n_signif - 1] > 0);
    assert(m >= n);

    q = digits_alloc(m - n + 1);
    if (q == NULL) MEMERROR_RETVAL(NULL)

    // D1: normalize, so that yd[n - 1] >= RADIX / 2
    d = RADIX / (y->digits[y->n_signif - 1] + 1);
    xd = digits_copy(x, n_shift_x, 1);
    if (xd == NULL) MEMERROR_RETVAL(NULL)
    digits_imul_digit(xd, d);
    yd = digits_copy(y, n_shift_y, 1);
    if (yd == NULL) MEMERROR_RETVAL(NULL)
    digits_imul_digit(yd, d);

    // D2: loop j from m - n to 0
    for (int j = m - n; j >= 0; --j) {
        // D3: calculate qhat and rhat
        u64_mul_u64(&t1, xd->digits[j + n], RADIX);
        u128_iadd_u64(&t1, xd->digits[j + n_1]);
        rhat = u128_idiv_u64(&t1, yd->digits[n_1]);
        assert(t1.hi == 0);
        qhat = t1.lo;
        u64_mul_u64(&t1, qhat, yd->digits[n_2]);
        u64_mul_u64(&t2, rhat, RADIX);
        u128_iadd_u64(&t2, xd->digits[j + n_2]);
        while (qhat >= RADIX || u128_cmp(&t1, &t2) == 1) {
            --qhat;
            rhat += yd->digits[n_1];
            if (rhat >= RADIX)
                break;
            u128_isub_u64(&t1, yd->digits[n_2]);
            u64_mul_u64(&t2, rhat, RADIX);
            u128_iadd_u64(&t2, xd->digits[j + n_2]);
        }
        // D4: multiply and subtract
        carry = 0;
        borrow = 0;
        for (int i = 0; i < n + 1; ++i) {
            u64_mul_u64(&t1, qhat, yd->digits[i]);
            u128_iadd_u64(&t1, carry);
            rhat = u128_idiv_u64(&t1, RADIX);
            carry = t1.lo;
            rhat = xd->digits[j + i] - (rhat + borrow);
            borrow = (rhat > xd->digits[j + i]);
            xd->digits[j + i] = borrow ? rhat + RADIX : rhat;
        }
        // D5: test remainder
        if (borrow == 0)
            q->digits[j] = qhat;
        else {
            // D6: add back
            q->digits[j] = qhat - 1;
            carry = 0;
            for (int i = 0; i < n_1; ++i) {
                xd->digits[j + i] += yd->digits[i] + carry;
                carry = (xd->digits[j + i] < yd->digits[i] ||
                        xd->digits[j + i] >= RADIX);
                if (carry)
                    xd->digits[j + i] -= RADIX;
            }
        }
    } // D7: loop j
    q->n_signif = q->n_alloc;
    // D8: unnormalize (if remainder is wanted)
    if (rem != NULL)
        *rem = digits_div_digit(xd, 0, d, NULL);
    // clean-up
    free((void *) xd);
    free((void *) yd);
    // return quotient
    return q;
}

fpdec_digit_array_t *
digits_div_max_prec(const fpdec_digit_array_t *x,
                    const fpdec_digit_array_t *y,
                    int *exp) {
    fpdec_digit_array_t *q;
    unsigned shift = MAX(0, y->n_signif + 1 - x->n_signif);
    unsigned accel = 1;
    int max_shift = -FPDEC_MIN_EXP + *exp;

    if (y->n_signif == 1) {
        fpdec_digit_t d = y->digits[0];
        fpdec_digit_t r;
        while (true) {
            q = digits_div_digit(x, shift, d, &r);
            if (shift >= max_shift || r == 0)
                break;
            accel++;
            shift = MIN(shift + accel * accel, max_shift);
        }
        if (r == 0)
            *exp -= shift;
        else
            *exp = FPDEC_MIN_EXP - 1;       // signal precision limit exceeded
    }
    else {
        fpdec_digit_array_t *r;
        while (true) {
            q = digits_divmod(x, shift, y, 0, &r);
            if (shift >= max_shift || digits_all_zero(r->digits, r->n_signif))
                break;
            accel++;
            shift = MIN(shift + accel * accel, max_shift);
        }
        if (digits_all_zero(r->digits, r->n_signif))
            *exp -= shift;
        else
            *exp = FPDEC_MIN_EXP - 1;       // signal precision limit exceeded
    }
    return q;
}

fpdec_digit_array_t *
digits_div_limit_prec(const fpdec_digit_array_t *x,
                      const fpdec_digit_array_t *y,
                      const int n_shift) {
    if (y->n_signif == 1 && n_shift <= 0)
        return digits_div_digit(x, MAX(0, -n_shift), y->digits[0], NULL);
    else
        return digits_divmod(x, MAX(0, -n_shift), y, MAX(0, n_shift), NULL);
}
