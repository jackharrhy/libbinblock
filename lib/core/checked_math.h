#ifndef BINBLOCK_CHECKED_MATH_H
#define BINBLOCK_CHECKED_MATH_H

#include <stdbool.h>
#include <stddef.h>

bool bb_size_add(size_t left, size_t right, size_t *out_result);
bool bb_size_multiply(size_t left, size_t right, size_t *out_result);

#endif
