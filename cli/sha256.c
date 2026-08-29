#include "sha256.h"

#include <string.h>

typedef struct bb_cli_sha256_state {
  uint8_t block[64];
  uint32_t block_length;
  uint64_t bit_length;
  uint32_t words[8];
} bb_cli_sha256_state;

static const uint32_t bb_cli_sha256_constants[64] = {
  0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
  0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
  0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
  0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
  0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
  0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
  0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
  0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

static uint32_t bb_cli_sha256_rotate(uint32_t value, uint32_t count) {
  return (value >> count) | (value << (32 - count));
}

static void bb_cli_sha256_transform(bb_cli_sha256_state *state, const uint8_t block[64]) {
  uint32_t schedule[64];
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint32_t d;
  uint32_t e;
  uint32_t f;
  uint32_t g;
  uint32_t h;
  uint32_t index;
  for (index = 0; index < 16; index += 1) {
    const size_t offset = (size_t)index * 4;
    schedule[index] = ((uint32_t)block[offset] << 24) | ((uint32_t)block[offset + 1] << 16) |
                      ((uint32_t)block[offset + 2] << 8) | block[offset + 3];
  }
  for (index = 16; index < 64; index += 1) {
    const uint32_t left = schedule[index - 15];
    const uint32_t right = schedule[index - 2];
    const uint32_t small0 = bb_cli_sha256_rotate(left, 7) ^ bb_cli_sha256_rotate(left, 18) ^ (left >> 3);
    const uint32_t small1 = bb_cli_sha256_rotate(right, 17) ^ bb_cli_sha256_rotate(right, 19) ^ (right >> 10);
    schedule[index] = schedule[index - 16] + small0 + schedule[index - 7] + small1;
  }
  a = state->words[0];
  b = state->words[1];
  c = state->words[2];
  d = state->words[3];
  e = state->words[4];
  f = state->words[5];
  g = state->words[6];
  h = state->words[7];
  for (index = 0; index < 64; index += 1) {
    const uint32_t big1 = bb_cli_sha256_rotate(e, 6) ^ bb_cli_sha256_rotate(e, 11) ^ bb_cli_sha256_rotate(e, 25);
    const uint32_t choose = (e & f) ^ (~e & g);
    const uint32_t temporary1 = h + big1 + choose + bb_cli_sha256_constants[index] + schedule[index];
    const uint32_t big0 = bb_cli_sha256_rotate(a, 2) ^ bb_cli_sha256_rotate(a, 13) ^ bb_cli_sha256_rotate(a, 22);
    const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t temporary2 = big0 + majority;
    h = g;
    g = f;
    f = e;
    e = d + temporary1;
    d = c;
    c = b;
    b = a;
    a = temporary1 + temporary2;
  }
  state->words[0] += a;
  state->words[1] += b;
  state->words[2] += c;
  state->words[3] += d;
  state->words[4] += e;
  state->words[5] += f;
  state->words[6] += g;
  state->words[7] += h;
}

static void bb_cli_sha256_init(bb_cli_sha256_state *state) {
  memset(state, 0, sizeof(*state));
  state->words[0] = 0x6a09e667u;
  state->words[1] = 0xbb67ae85u;
  state->words[2] = 0x3c6ef372u;
  state->words[3] = 0xa54ff53au;
  state->words[4] = 0x510e527fu;
  state->words[5] = 0x9b05688cu;
  state->words[6] = 0x1f83d9abu;
  state->words[7] = 0x5be0cd19u;
}

static void bb_cli_sha256_update(bb_cli_sha256_state *state, const uint8_t *data, size_t length) {
  size_t index;
  for (index = 0; index < length; index += 1) {
    state->block[state->block_length++] = data[index];
    if (state->block_length == 64) {
      bb_cli_sha256_transform(state, state->block);
      state->bit_length += 512;
      state->block_length = 0;
    }
  }
}

static void bb_cli_sha256_finish(bb_cli_sha256_state *state, uint8_t digest[32]) {
  uint32_t index = state->block_length;
  state->block[index++] = 0x80;
  if (index > 56) {
    while (index < 64) state->block[index++] = 0;
    bb_cli_sha256_transform(state, state->block);
    index = 0;
  }
  while (index < 56) state->block[index++] = 0;
  state->bit_length += (uint64_t)state->block_length * 8;
  for (index = 0; index < 8; index += 1)
    state->block[63 - index] = (uint8_t)(state->bit_length >> (index * 8));
  bb_cli_sha256_transform(state, state->block);
  for (index = 0; index < 32; index += 1)
    digest[index] = (uint8_t)(state->words[index / 4] >> (24 - (index % 4) * 8));
}

void bb_cli_sha256(const uint8_t *data, size_t length, uint8_t digest[32]) {
  bb_cli_sha256_state state;
  bb_cli_sha256_init(&state);
  if (length != 0) bb_cli_sha256_update(&state, data, length);
  bb_cli_sha256_finish(&state, digest);
}

void bb_cli_sha256_hex(const uint8_t *data, size_t length, char hex[65]) {
  static const char digits[] = "0123456789abcdef";
  uint8_t digest[32];
  size_t index;
  bb_cli_sha256(data, length, digest);
  for (index = 0; index < sizeof(digest); index += 1) {
    hex[index * 2] = digits[digest[index] >> 4];
    hex[index * 2 + 1] = digits[digest[index] & 15];
  }
  hex[64] = '\0';
}
