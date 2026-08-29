#include <binblock/syntax.h>

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  bb_context_desc desc;
  bb_context *context = NULL;
  bb_source_id source_id = BB_SOURCE_ID_NONE;
  bb_syntax_tree *syntax = NULL;
  bb_context_desc_init(&desc);
  desc.limits.max_source_bytes = size;
  if (bb_context_create(&desc, &context) == BB_STATUS_OK &&
      bb_context_add_source(
        context,
        (bb_string_view){"fuzz.binscript", sizeof("fuzz.binscript") - 1},
        (bb_bytes){data, size},
        &source_id
      ) == BB_STATUS_OK)
    (void)bb_syntax_parse(context, source_id, &syntax);
  bb_syntax_tree_destroy(syntax);
  bb_context_destroy(context);
  return 0;
}
