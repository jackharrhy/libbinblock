#include "test_support.h"

#include <binblock/syntax.h>

#include <stdint.h>
#include <string.h>

static int syntax_text_equal(bb_string_view text, const char *expected) {
  return bb_test_string_equal(text, expected);
}

static size_t syntax_count_nodes(const bb_syntax_tree *tree, bb_syntax_node_kind kind) {
  size_t count = 0;
  size_t index;
  for (index = 1; index <= bb_syntax_tree_node_count(tree); index += 1) {
    bb_syntax_node_info node;
    if (bb_syntax_tree_node(tree, (bb_syntax_node)index, &node) == BB_STATUS_OK && node.kind == kind) count += 1;
  }
  return count;
}

static int test_representative_program_is_lossless_and_spanned(void) {
  static const char source[] =
    "import \"binblock/basic\"\n"
    "// retained notebook comment\n"
    "size := 64\n"
    "colors := palette(red: #ff0000, blue: #0000ff,)\n"
    "blocks := colors.map(fill,).size(size)\n"
    "fade := lg(\n"
    "  180deg,\n"
    "  white 0%,\n"
    "  transparent-white 100%,\n"
    ")\n"
    "outputs := blocks.mask(fade)\n"
    "outputs\n";
  bb_context *context = NULL;
  bb_source_id source_id = BB_SOURCE_ID_NONE;
  bb_syntax_tree *tree = NULL;
  bb_syntax_node_info root;
  size_t token_count;
  size_t token_index;
  uint32_t previous_end = 0;
  int saw_comment = 0;
  int saw_degrees = 0;
  int saw_percentage = 0;
  BB_TEST_ASSERT(bb_context_create(NULL, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(
    bb_context_add_source(
      context,
      BB_TEST_STRING("starter.bb"),
      (bb_bytes){(const uint8_t *)source, sizeof(source) - 1},
      &source_id
    ) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(bb_syntax_parse(context, source_id, &tree) == BB_STATUS_OK);
  BB_TEST_ASSERT(tree != NULL && bb_syntax_tree_source_id(tree) == source_id);
  BB_TEST_ASSERT(bb_syntax_tree_diagnostic_count(tree) == 0);
  BB_TEST_ASSERT(bb_syntax_tree_node(tree, bb_syntax_tree_root(tree), &root) == BB_STATUS_OK);
  BB_TEST_ASSERT(root.kind == BB_SYNTAX_PROGRAM && root.child_count == 7);
  BB_TEST_ASSERT(root.span.byte_start == 0 && root.span.byte_end == sizeof(source) - 1);
  BB_TEST_ASSERT(syntax_count_nodes(tree, BB_SYNTAX_IMPORT_STATEMENT) == 1);
  BB_TEST_ASSERT(syntax_count_nodes(tree, BB_SYNTAX_BINDING_STATEMENT) == 5);
  BB_TEST_ASSERT(syntax_count_nodes(tree, BB_SYNTAX_EXPRESSION_STATEMENT) == 1);
  BB_TEST_ASSERT(syntax_count_nodes(tree, BB_SYNTAX_GRADIENT_STOP) == 2);

  token_count = bb_syntax_tree_token_count(tree);
  BB_TEST_ASSERT(token_count > 1);
  for (token_index = 0; token_index < token_count; token_index += 1) {
    bb_syntax_token_info token;
    BB_TEST_ASSERT(bb_syntax_tree_token(tree, token_index, &token) == BB_STATUS_OK);
    BB_TEST_ASSERT(token.span.source_id == source_id);
    BB_TEST_ASSERT(token.span.byte_start == previous_end);
    BB_TEST_ASSERT(token.span.byte_end >= token.span.byte_start && token.span.byte_end <= sizeof(source) - 1);
    if (token.kind == BB_SYNTAX_TOKEN_COMMENT) {
      saw_comment = syntax_text_equal(token.text, "// retained notebook comment");
      BB_TEST_ASSERT((token.flags & BB_SYNTAX_TOKEN_TRIVIA) != 0);
    }
    if (token.kind == BB_SYNTAX_TOKEN_NUMBER && token.unit == BB_SYNTAX_UNIT_DEGREES) {
      saw_degrees = syntax_text_equal(token.text, "180deg");
    }
    if (token.kind == BB_SYNTAX_TOKEN_NUMBER && token.unit == BB_SYNTAX_UNIT_PERCENTAGE) saw_percentage = 1;
    previous_end = token.span.byte_end;
  }
  BB_TEST_ASSERT(previous_end == sizeof(source) - 1);
  BB_TEST_ASSERT(saw_comment && saw_degrees && saw_percentage);
  for (token_index = 1; token_index <= bb_syntax_tree_node_count(tree); token_index += 1) {
    bb_syntax_node_info node;
    BB_TEST_ASSERT(bb_syntax_tree_node(tree, (bb_syntax_node)token_index, &node) == BB_STATUS_OK);
    BB_TEST_ASSERT(node.span.source_id == source_id);
    BB_TEST_ASSERT(node.span.byte_start <= node.span.byte_end && node.span.byte_end <= sizeof(source) - 1);
    if (node.primary_token != BB_SYNTAX_TOKEN_NONE) BB_TEST_ASSERT(node.primary_token < token_count);
  }
  BB_TEST_ASSERT(bb_syntax_tree_token(tree, token_count, &(bb_syntax_token_info){0}) == BB_STATUS_NOT_FOUND);
  BB_TEST_ASSERT(
    bb_syntax_tree_node(tree, (bb_syntax_node)(bb_syntax_tree_node_count(tree) + 1), &(bb_syntax_node_info){0}) ==
    BB_STATUS_NOT_FOUND
  );
  bb_syntax_tree_destroy(tree);
  bb_context_destroy(context);
  return 1;
}

static int test_parser_recovers_and_orders_diagnostics(void) {
  static const char source[] =
    "import 42;\n"
    "a := [1, 2;\n"
    "b := foo(3\n"
    "c := #12;\n"
    "d := bar(,);\n"
    "e := ok\n";
  bb_context *context = NULL;
  bb_source_id source_id = BB_SOURCE_ID_NONE;
  bb_syntax_tree *tree = NULL;
  bb_syntax_node_info root;
  size_t diagnostic_count;
  size_t index;
  uint32_t previous_start = 0;
  uint32_t codes[5] = {
    BB_SYNTAX_DIAGNOSTIC_EXPECTED_IMPORT_PATH,
    BB_SYNTAX_DIAGNOSTIC_EXPECTED_CLOSE_BRACKET,
    BB_SYNTAX_DIAGNOSTIC_EXPECTED_CLOSE_PAREN,
    BB_SYNTAX_DIAGNOSTIC_INVALID_COLOR,
    BB_SYNTAX_DIAGNOSTIC_EXPECTED_EXPRESSION,
  };
  BB_TEST_ASSERT(bb_context_create(NULL, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(
    bb_context_add_source(
      context,
      BB_TEST_STRING("malformed.bb"),
      (bb_bytes){(const uint8_t *)source, sizeof(source) - 1},
      &source_id
    ) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(bb_syntax_parse(context, source_id, &tree) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_syntax_tree_node(tree, bb_syntax_tree_root(tree), &root) == BB_STATUS_OK);
  BB_TEST_ASSERT(root.child_count == 6);
  diagnostic_count = bb_syntax_tree_diagnostic_count(tree);
  BB_TEST_ASSERT(diagnostic_count == 5);
  for (index = 0; index < diagnostic_count; index += 1) {
    bb_diagnostic diagnostic;
    BB_TEST_ASSERT(bb_syntax_tree_diagnostic(tree, index, &diagnostic) == BB_STATUS_OK);
    BB_TEST_ASSERT(diagnostic.code == codes[index]);
    BB_TEST_ASSERT(diagnostic.primary_span.source_id == source_id);
    BB_TEST_ASSERT(index == 0 || diagnostic.primary_span.byte_start >= previous_start);
    BB_TEST_ASSERT(diagnostic.message.length != 0);
    previous_start = diagnostic.primary_span.byte_start;
  }
  BB_TEST_ASSERT(syntax_count_nodes(tree, BB_SYNTAX_ERROR_NODE) >= 2);
  BB_TEST_ASSERT(
    bb_syntax_tree_diagnostic(tree, diagnostic_count, &(bb_diagnostic){0}) == BB_STATUS_NOT_FOUND
  );
  bb_syntax_tree_destroy(tree);
  bb_context_destroy(context);
  return 1;
}

static int test_syntax_limits_and_empty_program(void) {
  {
    bb_context *context = NULL;
    bb_source_id source_id = BB_SOURCE_ID_NONE;
    bb_syntax_tree *tree = NULL;
    bb_syntax_node_info root;
    BB_TEST_ASSERT(bb_context_create(NULL, &context) == BB_STATUS_OK);
    BB_TEST_ASSERT(
      bb_context_add_source(context, BB_TEST_STRING("empty.bb"), (bb_bytes){NULL, 0}, &source_id) == BB_STATUS_OK
    );
    BB_TEST_ASSERT(bb_syntax_parse(context, source_id, &tree) == BB_STATUS_OK);
    BB_TEST_ASSERT(bb_syntax_tree_token_count(tree) == 1);
    BB_TEST_ASSERT(bb_syntax_tree_node(tree, bb_syntax_tree_root(tree), &root) == BB_STATUS_OK);
    BB_TEST_ASSERT(root.kind == BB_SYNTAX_PROGRAM && root.child_count == 0);
    bb_syntax_tree_destroy(tree);
    bb_context_destroy(context);
  }
  {
    static const char source[] = "x := [[[[[1]]]]]";
    bb_context_desc desc;
    bb_context *context = NULL;
    bb_source_id source_id = BB_SOURCE_ID_NONE;
    bb_syntax_tree *tree = NULL;
    size_t index;
    int saw_depth = 0;
    bb_context_desc_init(&desc);
    desc.limits.max_syntax_depth = 3;
    BB_TEST_ASSERT(bb_context_create(&desc, &context) == BB_STATUS_OK);
    BB_TEST_ASSERT(
      bb_context_add_source(
        context,
        BB_TEST_STRING("deep.bb"),
        (bb_bytes){(const uint8_t *)source, sizeof(source) - 1},
        &source_id
      ) == BB_STATUS_OK
    );
    BB_TEST_ASSERT(bb_syntax_parse(context, source_id, &tree) == BB_STATUS_OK);
    for (index = 0; index < bb_syntax_tree_diagnostic_count(tree); index += 1) {
      bb_diagnostic diagnostic;
      BB_TEST_ASSERT(bb_syntax_tree_diagnostic(tree, index, &diagnostic) == BB_STATUS_OK);
      if (diagnostic.code == BB_SYNTAX_DIAGNOSTIC_DEPTH_LIMIT) saw_depth = 1;
    }
    BB_TEST_ASSERT(saw_depth);
    bb_syntax_tree_destroy(tree);
    bb_context_destroy(context);
  }
  {
    static const char source[] = "x := 1";
    bb_context_desc desc;
    bb_context *context = NULL;
    bb_source_id source_id = BB_SOURCE_ID_NONE;
    bb_syntax_tree *tree = (bb_syntax_tree *)(uintptr_t)1;
    bb_context_desc_init(&desc);
    desc.limits.max_syntax_tokens = 2;
    BB_TEST_ASSERT(bb_context_create(&desc, &context) == BB_STATUS_OK);
    BB_TEST_ASSERT(
      bb_context_add_source(
        context,
        BB_TEST_STRING("limited.bb"),
        (bb_bytes){(const uint8_t *)source, sizeof(source) - 1},
        &source_id
      ) == BB_STATUS_OK
    );
    BB_TEST_ASSERT(bb_syntax_parse(context, source_id, &tree) == BB_STATUS_LIMIT_EXCEEDED);
    BB_TEST_ASSERT(tree == NULL);
    bb_context_destroy(context);
  }
  return 1;
}

static int test_syntax_allocation_failures_are_clean(void) {
  static const char source[] = "x := fill(#ff0000)\nx";
  size_t successful_attempts;
  size_t fail_at;
  {
    bb_test_allocator_state state = {0};
    bb_context_desc desc = bb_test_context_desc(&state);
    bb_context *context = NULL;
    bb_source_id source_id = BB_SOURCE_ID_NONE;
    bb_syntax_tree *tree = NULL;
    BB_TEST_ASSERT(bb_context_create(&desc, &context) == BB_STATUS_OK);
    BB_TEST_ASSERT(
      bb_context_add_source(
        context,
        BB_TEST_STRING("allocation.bb"),
        (bb_bytes){(const uint8_t *)source, sizeof(source) - 1},
        &source_id
      ) == BB_STATUS_OK
    );
    BB_TEST_ASSERT(bb_syntax_parse(context, source_id, &tree) == BB_STATUS_OK);
    successful_attempts = state.attempt_count;
    bb_syntax_tree_destroy(tree);
    bb_context_destroy(context);
    BB_TEST_ASSERT(state.outstanding_allocations == 0);
  }
  BB_TEST_ASSERT(successful_attempts >= 4);
  for (fail_at = 4; fail_at <= successful_attempts; fail_at += 1) {
    bb_test_allocator_state state = {0};
    bb_context_desc desc = bb_test_context_desc(&state);
    bb_context *context = NULL;
    bb_source_id source_id = BB_SOURCE_ID_NONE;
    bb_syntax_tree *tree = (bb_syntax_tree *)(uintptr_t)1;
    state.fail_at_attempt = fail_at;
    BB_TEST_ASSERT(bb_context_create(&desc, &context) == BB_STATUS_OK);
    BB_TEST_ASSERT(
      bb_context_add_source(
        context,
        BB_TEST_STRING("allocation.bb"),
        (bb_bytes){(const uint8_t *)source, sizeof(source) - 1},
        &source_id
      ) == BB_STATUS_OK
    );
    BB_TEST_ASSERT(bb_syntax_parse(context, source_id, &tree) == BB_STATUS_OUT_OF_MEMORY);
    BB_TEST_ASSERT(tree == NULL);
    bb_context_destroy(context);
    BB_TEST_ASSERT(state.outstanding_allocations == 0);
    BB_TEST_ASSERT(state.outstanding_bytes == 0);
  }
  return 1;
}

const bb_test_case bb_syntax_tests[] = {
  {"representative syntax is lossless and spanned", test_representative_program_is_lossless_and_spanned},
  {"parser recovery and ordered diagnostics", test_parser_recovers_and_orders_diagnostics},
  {"syntax depth token limits and empty input", test_syntax_limits_and_empty_program},
  {"syntax allocation failures are clean", test_syntax_allocation_failures_are_clean},
};

const size_t bb_syntax_test_count = sizeof(bb_syntax_tests) / sizeof(bb_syntax_tests[0]);
