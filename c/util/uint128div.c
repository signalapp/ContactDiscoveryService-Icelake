
// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include "util.h"
#include <stdlib.h>

// Adapted from https://www.codeproject.com/Tips/785014/UInt-Division-Modulus
// To understand this, read Knuth, Art of Computer Programming Vol 2, 4.3.1 Algorthm D.
// The variable names follow Knuth's notation somewhat - rhat, u, and v map directly.
// the qs array plays the role of "q hat".
//
// Other key differences compared to Knuth: (1) the normalization step is different
// and uses shifts instead of multiplication, and (2) the outer loop
// only has two steps and is unrolled.
void divmod128by64(uint64_t u1, uint64_t u0, uint64_t v, uint64_t* q)
{
    // throughout the comments we will refer to the 128-bit number u = u1*2^64 + u0
    const uint64_t base = 1ul << 32;
    uint64_t vs[2];
    uint64_t qs[2];
    uint64_t low_us[2];
    uint64_t u32, u21, u10, rhat, left, right;
    size_t s;

    // shift the divisor, v, so that the most significant bit is set
    s = __builtin_clzll(v);
    v <<= s;

    // We'll work in base 2^32. Split the divisor into the high digit (v1) and low digit (v0)
    vs[1] = v >> 32;
    vs[0] = v & 0xffffffff;

    // Now shift the dividend by the same amount, and split it into its "upper" and ""lower" parts (u32 and u10)
    if (s > 0)
    {
        u32 = (u1 << s) | (u0 >> (64 - s));
        u10 = u0 << s;
    }
    else
    {
        u32 = u1;
        u10 = u0;
    }

    // Split the low part of u into 32-bit digits
    low_us[1] = u10 >> 32;
    low_us[0] = u10 & 0xffffffff;

    // If we think of u = u3*base^3 + u2*base^2 + u1*base + u0
    // (as in Knuth) then u32 = u3*base + u2, u10 = u1*base + u0

    
    qs[1] = u32 / vs[1];

    rhat = u32 % vs[1];

    left = qs[1] * vs[0];
    right = (rhat << 32) + low_us[1];

    while ((qs[1] >= base) || (left > right))
    {
        --qs[1];
        rhat += vs[1];
        if (rhat < base)
        {
            left -= vs[0];
            right = (rhat << 32) | low_us[1];
        } else {
            break;
        }
    }
 
    // Second iteration of Knuth's "outer loop".  REcall we can think of u = u3*base^3 + u2*base^2 + u1*base + u0
    // In the first iteration we set u <- u - qs[1]*v. After this change, u21 = u2*base + u1.
    u21 = (u32 << 32) + (low_us[1] - (qs[1] * v));

    qs[0] = u21 / vs[1];
    rhat = u21 % vs[1];

    left = qs[0] * vs[0];
    right = (rhat << 32) | low_us[0];

    while ((qs[0] >= base) || (left > right))
    {
        --qs[0];
        rhat += vs[1];
        if (rhat < base)
        {
            left -= vs[0];
            right = (rhat << 32) | low_us[0];
        } else {
            break;
        }
    }

    *q = (qs[1] << 32) | qs[0];
}


uint64_t div128by64(__uint128_t u, uint64_t v)
{
    uint64_t q;
    uint64_t u1 = u >> 64;
    uint64_t u2 = u & UINT64_MAX;
    divmod128by64(u1, u2, v, &q);
    return q;
}
