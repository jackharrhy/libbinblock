#include <binblock/module.h>

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  bb_precompiled_module_info info;
  bb_bytes source;
  (void)bb_precompiled_module_read((bb_bytes){data, size}, &info, &source);
  return 0;
}
