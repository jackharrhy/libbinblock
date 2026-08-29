#include <binblock/binblock.h>

static int bb_utf8_is_continuation(uint8_t byte) {
  return (byte & UINT8_C(0xc0)) == UINT8_C(0x80);
}

bb_status bb_utf8_validate(bb_bytes bytes) {
  size_t index = 0;
  if (bytes.length != 0 && bytes.data == NULL) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  while (index < bytes.length) {
    const uint8_t first = bytes.data[index];
    if (first <= UINT8_C(0x7f)) {
      index += 1;
      continue;
    }
    if (first >= UINT8_C(0xc2) && first <= UINT8_C(0xdf)) {
      if (index + 1 >= bytes.length || !bb_utf8_is_continuation(bytes.data[index + 1])) {
        return BB_STATUS_INVALID_UTF8;
      }
      index += 2;
      continue;
    }
    if (first >= UINT8_C(0xe0) && first <= UINT8_C(0xef)) {
      uint8_t second;
      if (index + 2 >= bytes.length) {
        return BB_STATUS_INVALID_UTF8;
      }
      second = bytes.data[index + 1];
      if (!bb_utf8_is_continuation(second) || !bb_utf8_is_continuation(bytes.data[index + 2])) {
        return BB_STATUS_INVALID_UTF8;
      }
      if ((first == UINT8_C(0xe0) && second < UINT8_C(0xa0)) ||
          (first == UINT8_C(0xed) && second >= UINT8_C(0xa0))) {
        return BB_STATUS_INVALID_UTF8;
      }
      index += 3;
      continue;
    }
    if (first >= UINT8_C(0xf0) && first <= UINT8_C(0xf4)) {
      uint8_t second;
      if (index + 3 >= bytes.length) {
        return BB_STATUS_INVALID_UTF8;
      }
      second = bytes.data[index + 1];
      if (!bb_utf8_is_continuation(second) || !bb_utf8_is_continuation(bytes.data[index + 2]) ||
          !bb_utf8_is_continuation(bytes.data[index + 3])) {
        return BB_STATUS_INVALID_UTF8;
      }
      if ((first == UINT8_C(0xf0) && second < UINT8_C(0x90)) ||
          (first == UINT8_C(0xf4) && second >= UINT8_C(0x90))) {
        return BB_STATUS_INVALID_UTF8;
      }
      index += 4;
      continue;
    }
    return BB_STATUS_INVALID_UTF8;
  }
  return BB_STATUS_OK;
}
