#ifndef BINBLOCK_SYNTAX_H
#define BINBLOCK_SYNTAX_H

#include <binblock/binblock.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bb_syntax_tree bb_syntax_tree;
typedef uint32_t bb_syntax_node;

#define BB_SYNTAX_NODE_NONE ((bb_syntax_node)0)
#define BB_SYNTAX_TOKEN_NONE UINT32_MAX

typedef enum bb_syntax_token_kind {
  BB_SYNTAX_TOKEN_WHITESPACE = 1,
  BB_SYNTAX_TOKEN_COMMENT = 2,
  BB_SYNTAX_TOKEN_IDENTIFIER = 3,
  BB_SYNTAX_TOKEN_NUMBER = 4,
  BB_SYNTAX_TOKEN_COLOR = 5,
  BB_SYNTAX_TOKEN_STRING = 6,
  BB_SYNTAX_TOKEN_SYMBOL = 7,
  BB_SYNTAX_TOKEN_ERROR = 8,
  BB_SYNTAX_TOKEN_EOF = 9
} bb_syntax_token_kind;

typedef enum bb_syntax_number_unit {
  BB_SYNTAX_UNIT_NONE = 0,
  BB_SYNTAX_UNIT_DEGREES = 1,
  BB_SYNTAX_UNIT_PERCENTAGE = 2
} bb_syntax_number_unit;

enum {
  BB_SYNTAX_TOKEN_TRIVIA = 1u << 0,
  BB_SYNTAX_TOKEN_STARTS_LINE = 1u << 1
};

typedef struct bb_syntax_token_info {
  bb_syntax_token_kind kind;
  bb_syntax_number_unit unit;
  uint32_t flags;
  bb_span span;
  /* Raw source bytes, including quotes or comment delimiters. */
  bb_string_view text;
} bb_syntax_token_info;

typedef enum bb_syntax_node_kind {
  BB_SYNTAX_PROGRAM = 1,
  BB_SYNTAX_IMPORT_STATEMENT = 2,
  BB_SYNTAX_BINDING_STATEMENT = 3,
  BB_SYNTAX_EXPRESSION_STATEMENT = 4,
  BB_SYNTAX_IDENTIFIER_EXPRESSION = 5,
  BB_SYNTAX_NUMBER_LITERAL = 6,
  BB_SYNTAX_STRING_LITERAL = 7,
  BB_SYNTAX_COLOR_LITERAL = 8,
  BB_SYNTAX_ARRAY_EXPRESSION = 9,
  BB_SYNTAX_CALL_EXPRESSION = 10,
  BB_SYNTAX_MEMBER_CALL_EXPRESSION = 11,
  BB_SYNTAX_NAMED_ARGUMENT = 12,
  BB_SYNTAX_GRADIENT_STOP = 13,
  BB_SYNTAX_GROUP_EXPRESSION = 14,
  BB_SYNTAX_ERROR_NODE = 15
} bb_syntax_node_kind;

typedef struct bb_syntax_node_info {
  bb_syntax_node_kind kind;
  bb_span span;
  /* Token carrying the node's name or literal; NONE for structural nodes. */
  uint32_t primary_token;
  size_t child_count;
} bb_syntax_node_info;

typedef enum bb_syntax_diagnostic_code {
  BB_SYNTAX_DIAGNOSTIC_UNEXPECTED_CHARACTER = 1000,
  BB_SYNTAX_DIAGNOSTIC_INVALID_COLOR = 1001,
  BB_SYNTAX_DIAGNOSTIC_UNTERMINATED_STRING = 1002,
  BB_SYNTAX_DIAGNOSTIC_UNTERMINATED_COMMENT = 1003,
  BB_SYNTAX_DIAGNOSTIC_EXPECTED_IMPORT_PATH = 1100,
  BB_SYNTAX_DIAGNOSTIC_EXPECTED_EXPRESSION = 1101,
  BB_SYNTAX_DIAGNOSTIC_EXPECTED_METHOD = 1102,
  BB_SYNTAX_DIAGNOSTIC_EXPECTED_OPEN_PAREN = 1103,
  BB_SYNTAX_DIAGNOSTIC_EXPECTED_CLOSE_PAREN = 1104,
  BB_SYNTAX_DIAGNOSTIC_EXPECTED_CLOSE_BRACKET = 1105,
  BB_SYNTAX_DIAGNOSTIC_EXPECTED_SEPARATOR = 1106,
  BB_SYNTAX_DIAGNOSTIC_DEPTH_LIMIT = 1107
} bb_syntax_diagnostic_code;

/* The context and source record must outlive the returned tree. Syntax errors are
 * returned as ordered diagnostics while this function itself returns OK. */
BB_API bb_status bb_syntax_parse(
  bb_context *context,
  bb_source_id source_id,
  bb_syntax_tree **out_tree
);
BB_API void bb_syntax_tree_destroy(bb_syntax_tree *tree);
BB_API bb_source_id bb_syntax_tree_source_id(const bb_syntax_tree *tree);
BB_API bb_syntax_node bb_syntax_tree_root(const bb_syntax_tree *tree);
BB_API size_t bb_syntax_tree_token_count(const bb_syntax_tree *tree);
BB_API bb_status bb_syntax_tree_token(
  const bb_syntax_tree *tree,
  size_t index,
  bb_syntax_token_info *out_token
);
BB_API size_t bb_syntax_tree_node_count(const bb_syntax_tree *tree);
BB_API bb_status bb_syntax_tree_node(
  const bb_syntax_tree *tree,
  bb_syntax_node node,
  bb_syntax_node_info *out_node
);
BB_API bb_status bb_syntax_tree_child(
  const bb_syntax_tree *tree,
  bb_syntax_node node,
  size_t child_index,
  bb_syntax_node *out_child
);
BB_API size_t bb_syntax_tree_diagnostic_count(const bb_syntax_tree *tree);
BB_API bb_status bb_syntax_tree_diagnostic(
  const bb_syntax_tree *tree,
  size_t index,
  bb_diagnostic *out_diagnostic
);

#ifdef __cplusplus
}
#endif

#endif
