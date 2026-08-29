#include <binblock/program.h>

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  bb_context_desc desc;
  bb_context *context = NULL;
  bb_source_id source_id = BB_SOURCE_ID_NONE;
  bb_syntax_tree *syntax = NULL;
  bb_program *program = NULL;
  if (size > 65536) return 0;
  bb_context_desc_init(&desc);
  desc.utf8_policy = BB_UTF8_ALLOW_INVALID;
  desc.limits.max_source_bytes = size == 0 ? 1 : size;
  desc.limits.max_syntax_tokens = 16384;
  desc.limits.max_syntax_nodes = 16384;
  desc.limits.max_graph_nodes = 4096;
  desc.limits.max_collection_cardinality = 65536;
  if (bb_context_create(&desc, &context) == BB_STATUS_OK &&
      bb_context_add_source(
        context,
        (bb_string_view){"semantic-fuzz.binscript", sizeof("semantic-fuzz.binscript") - 1},
        (bb_bytes){data, size},
        &source_id
      ) == BB_STATUS_OK &&
      bb_syntax_parse(context, source_id, &syntax) == BB_STATUS_OK)
    (void)bb_program_compile(context, syntax, &program);
  bb_program_destroy(program);
  bb_syntax_tree_destroy(syntax);
  bb_context_destroy(context);
  return 0;
}
