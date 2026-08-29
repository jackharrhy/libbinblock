#include "checked_math.h"

#include <stdint.h>

bool bb_size_add(size_t left, size_t right, size_t *out_result) {
  if (out_result == NULL || left > SIZE_MAX - right) {
    return false;
  }
  *out_result = left + right;
  return true;
}

bool bb_size_multiply(size_t left, size_t right, size_t *out_result) {
  if (out_result == NULL || (right != 0 && left > SIZE_MAX / right)) {
    return false;
  }
  *out_result = left * right;
  return true;
}
