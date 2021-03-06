/*
 * Copyright (c) 2014, Prajjwal Bhandari <pbhandari@pbhandari.ca>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdint.h>
#include <string.h>
#include <aes.h>

static void
add_round_key(uint32_t* state, const uint32_t* round_key)
{
    state[0] ^= round_key[0];
    state[1] ^= round_key[1];
    state[2] ^= round_key[2];
    state[3] ^= round_key[3];
}

static void
sub_bytes(uint32_t *state, const sbox_t sb)
{
    state[0] = SBOX_AT(state[0], sb);
    state[1] = SBOX_AT(state[1], sb);
    state[2] = SBOX_AT(state[2], sb);
    state[3] = SBOX_AT(state[3], sb);
}

static uint8_t
mult(uint8_t a, uint8_t b)
{
    uint8_t m[3] = {mult2[a], mult2[m[0]], mult2[m[1]]};
    return (a * !!(b & 1)) ^ (m[0] * !!(b & 2)) ^ (m[1] * !!(b & 4)) ^
           (m[2] * !!(b & 8));
}

static void
inv_mix_columns(uint32_t *state)
{
    int i;
    for (i = 0; i < 4; i++) {
        uint8_t st[] = {state[i] >> 24, state[i] >> 16, state[i] >> 8,
                        state[i]};
        uint8_t tmp[] = {
            /* XXX: if I don't cast to uint8_t, values are wrong. */
            (uint8_t)(mult(st[0], 0xe) ^ mult(st[1], 0xb) ^ mult(st[2], 0xd) ^
                      mult(st[3], 0x9)),
            (uint8_t)(mult(st[0], 0x9) ^ mult(st[1], 0xe) ^ mult(st[2], 0xb) ^
                      mult(st[3], 0xd)),
            (uint8_t)(mult(st[0], 0xd) ^ mult(st[1], 0x9) ^ mult(st[2], 0xe) ^
                      mult(st[3], 0xb)),
            (uint8_t)(mult(st[0], 0xb) ^ mult(st[1], 0xd) ^ mult(st[2], 0x9) ^
                      mult(st[3], 0xe)),
        };

        state[i] = tmp[0] << 24 | tmp[1] << 16 | tmp[2] << 8 | tmp[3];
    }
}

static void
mix_columns(uint32_t *state)
{
    int i;
    for (i = 0; i < 4; i++) {
        uint8_t s[] = {state[i] >> 24, state[i] >> 16, state[i] >> 8, state[i]};
        uint8_t m[] = {mult2[s[0]],    mult2[s[1]],    mult2[s[2]],   mult2[s[3]]};

        /* mt[x] ^ st[x] == mult(st[x] , 3) */
        state[i] = (m[0]      ^ m[1]^s[1] ^ s[2]      ^ s[3])      << 24 |
                   (s[0]      ^ m[1]      ^ m[2]^s[2] ^ s[3])      << 16 |
                   (s[0]      ^ s[1]      ^ m[2]      ^ m[3]^s[3]) << 8 |
                   (m[0]^s[0] ^ s[1]      ^ s[2]      ^ m[3]);
    }
}

static void
shift_rows(uint32_t *state)
{
    uint32_t tmp[] = {
        (state[0] & 0xff000000) | (state[1] & 0x00ff0000) |
        (state[2] & 0x0000ff00) | (state[3] & 0x000000ff),

        (state[1] & 0xff000000) | (state[2] & 0x00ff0000) |
        (state[3] & 0x0000ff00) | (state[0] & 0x000000ff),

        (state[2] & 0xff000000) | (state[3] & 0x00ff0000) |
        (state[0] & 0x0000ff00) | (state[1] & 0x000000ff),

        (state[3] & 0xff000000) | (state[0] & 0x00ff0000) |
        (state[1] & 0x0000ff00) | (state[2] & 0x000000ff),
    };

    state[0] = tmp[0];
    state[1] = tmp[1];
    state[2] = tmp[2];
    state[3] = tmp[3];
}

static void
inv_shift_rows(uint32_t *state)
{
    uint32_t tmp[] = {
        (state[0] & 0xff000000) | (state[3] & 0x00ff0000) |
        (state[2] & 0x0000ff00) | (state[1] & 0x000000ff),

        (state[1] & 0xff000000) | (state[0] & 0x00ff0000) |
        (state[3] & 0x0000ff00) | (state[2] & 0x000000ff),

        (state[2] & 0xff000000) | (state[1] & 0x00ff0000) |
        (state[0] & 0x0000ff00) | (state[3] & 0x000000ff),

        (state[3] & 0xff000000) | (state[2] & 0x00ff0000) |
        (state[1] & 0x0000ff00) | (state[0] & 0x000000ff),
    };

    state[0] = tmp[0];
    state[1] = tmp[1];
    state[2] = tmp[2];
    state[3] = tmp[3];
}

void
aes128_expand_key(const uint8_t *keytext, uint32_t *key)
{
    uint8_t i, j;  /* using uint8 for better packing */
    STR_TO_WORD_ARRAY(keytext, key);

    for (i = 1, j = 4; i < 11; i++, j += 4) {
        key[j] = SBOX_AT(LSHIFT(key[j - 1], 1), sbox) ^ (RCON[i] << 24) ^ key[j - 4];

        key[j + 1] = key[j - 3] ^ key[j];
        key[j + 2] = key[j - 2] ^ key[j + 1];
        key[j + 3] = key[j - 1] ^ key[j + 2];
    }
}

void
aes128_encrypt_block(const uint8_t* input, const uint32_t* key, uint8_t* output)
{
    uint32_t plaintext[4];
    int i;
    STR_TO_WORD_ARRAY(input, plaintext);

    add_round_key(plaintext, &key[0]);
    for (i = 4; i < 40; i += 4) {
        shift_rows(plaintext);

        sub_bytes(plaintext, sbox);
        mix_columns(plaintext);
        add_round_key(plaintext, &key[i]);
    }
    shift_rows(plaintext);
    sub_bytes(plaintext, sbox);
    add_round_key(plaintext, &key[40]);

    WORD_ARRAY_TO_STR(plaintext, output);
}

void
aes128_decrypt_block(const uint8_t* input, const uint32_t* key, uint8_t* output)
{
    uint32_t ciphertext[4];
    int i;
    STR_TO_WORD_ARRAY(input, ciphertext);

    add_round_key(ciphertext, &key[40]);

    for (i = 36; i > 0; i -= 4) {
        inv_shift_rows(ciphertext);

        sub_bytes(ciphertext, rev_sbox);
        add_round_key(ciphertext, &key[i]);
        inv_mix_columns(ciphertext);
    }
    inv_shift_rows(ciphertext);

    sub_bytes(ciphertext, rev_sbox);
    add_round_key(ciphertext, &key[0]);

    WORD_ARRAY_TO_STR(ciphertext, output);
}
