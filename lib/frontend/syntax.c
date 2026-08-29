#include <binblock/syntax.h>

#include "checked_math.h"
#include "context_internal.h"
#include "diagnostic.h"

#include <string.h>

typedef struct bb_syntax_token_record {
  bb_syntax_token_kind kind;
  bb_syntax_number_unit unit;
  uint32_t flags;
  bb_span span;
} bb_syntax_token_record;

typedef struct bb_syntax_node_record {
  bb_syntax_node_info info;
  uint32_t first_child;
  uint32_t last_child;
} bb_syntax_node_record;

typedef struct bb_syntax_child_record {
  bb_syntax_node node;
  uint32_t next;
} bb_syntax_child_record;

struct bb_syntax_tree {
  bb_context *context;
  bb_source_info source;
  bb_limits limits;
  bb_syntax_token_record *tokens;
  size_t token_count;
  size_t token_capacity;
  bb_syntax_node_record *nodes;
  size_t node_count;
  size_t node_capacity;
  bb_syntax_child_record *children;
  size_t child_count;
  size_t child_capacity;
  bb_diagnostic_store diagnostics;
  size_t *diagnostic_order;
  size_t diagnostic_order_bytes;
  bb_syntax_node root;
};

typedef struct bb_parser {
  bb_syntax_tree *tree;
  size_t cursor;
  bb_status status;
} bb_parser;

static int bb_ascii_is_space(uint8_t byte) {
  return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n' || byte == '\f' || byte == '\v';
}

static int bb_ascii_is_digit(uint8_t byte) {
  return byte >= '0' && byte <= '9';
}

static int bb_ascii_is_hex(uint8_t byte) {
  return bb_ascii_is_digit(byte) || (byte >= 'a' && byte <= 'f') || (byte >= 'A' && byte <= 'F');
}

static int bb_ascii_is_identifier_start(uint8_t byte) {
  return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || byte == '_';
}

static int bb_ascii_is_identifier_continue(uint8_t byte) {
  return bb_ascii_is_identifier_start(byte) || bb_ascii_is_digit(byte) || byte == '-';
}

static int bb_range_contains_newline(const uint8_t *bytes, size_t start, size_t end) {
  size_t index;
  for (index = start; index < end; index += 1) {
    if (bytes[index] == '\n' || bytes[index] == '\r') return 1;
  }
  return 0;
}

static bb_status bb_syntax_grow(
  bb_context *context,
  void **items,
  size_t *capacity,
  size_t item_size,
  size_t alignment,
  size_t required,
  size_t maximum
) {
  size_t new_capacity;
  size_t old_bytes;
  size_t new_bytes;
  void *replacement;
  bb_status status;
  if (required > maximum) return BB_STATUS_LIMIT_EXCEEDED;
  if (required <= *capacity) return BB_STATUS_OK;
  new_capacity = *capacity == 0 ? (maximum < 32 ? maximum : 32) : *capacity;
  while (new_capacity < required) {
    size_t doubled;
    if (!bb_size_multiply(new_capacity, 2, &doubled)) {
      new_capacity = maximum;
      break;
    }
    new_capacity = doubled > maximum ? maximum : doubled;
  }
  if (new_capacity < required || !bb_size_multiply(*capacity, item_size, &old_bytes) ||
      !bb_size_multiply(new_capacity, item_size, &new_bytes)) return BB_STATUS_OVERFLOW;
  status = bb_context_reallocate(context, *items, old_bytes, new_bytes, alignment, &replacement);
  if (status != BB_STATUS_OK) return status;
  *items = replacement;
  *capacity = new_capacity;
  return BB_STATUS_OK;
}

static bb_status bb_syntax_push_diagnostic(
  bb_syntax_tree *tree,
  bb_syntax_diagnostic_code code,
  const char *message,
  uint32_t start,
  uint32_t end
) {
  return bb_diagnostic_store_push(
    &tree->diagnostics,
    BB_DIAGNOSTIC_ERROR,
    (uint32_t)code,
    (bb_string_view){message, strlen(message)},
    (bb_span){tree->source.id, start, end},
    NULL,
    0
  );
}

static bb_status bb_syntax_add_token(
  bb_syntax_tree *tree,
  bb_syntax_token_kind kind,
  bb_syntax_number_unit unit,
  uint32_t flags,
  uint32_t start,
  uint32_t end
) {
  bb_syntax_token_record *token;
  bb_status status = bb_syntax_grow(
    tree->context,
    (void **)&tree->tokens,
    &tree->token_capacity,
    sizeof(*tree->tokens),
    _Alignof(bb_syntax_token_record),
    tree->token_count + 1,
    tree->limits.max_syntax_tokens
  );
  if (status != BB_STATUS_OK) return status;
  token = &tree->tokens[tree->token_count];
  token->kind = kind;
  token->unit = unit;
  token->flags = flags;
  token->span = (bb_span){tree->source.id, start, end};
  tree->token_count += 1;
  return BB_STATUS_OK;
}

static bb_status bb_syntax_lex(bb_syntax_tree *tree) {
  const uint8_t *bytes = tree->source.contents.data;
  const size_t length = tree->source.contents.length;
  size_t index = 0;
  int starts_line = 1;
  bb_status status;
  while (index < length) {
    size_t start = index;
    uint32_t significant_flags = starts_line ? BB_SYNTAX_TOKEN_STARTS_LINE : 0;
    const uint8_t byte = bytes[index];
    if (bb_ascii_is_space(byte)) {
      while (index < length && bb_ascii_is_space(bytes[index])) index += 1;
      status = bb_syntax_add_token(
        tree,
        BB_SYNTAX_TOKEN_WHITESPACE,
        BB_SYNTAX_UNIT_NONE,
        BB_SYNTAX_TOKEN_TRIVIA,
        (uint32_t)start,
        (uint32_t)index
      );
      if (status != BB_STATUS_OK) return status;
      if (bb_range_contains_newline(bytes, start, index)) starts_line = 1;
      continue;
    }
    if (byte == '/' && index + 1 < length && bytes[index + 1] == '/') {
      index += 2;
      while (index < length && bytes[index] != '\n' && bytes[index] != '\r') index += 1;
      status = bb_syntax_add_token(
        tree,
        BB_SYNTAX_TOKEN_COMMENT,
        BB_SYNTAX_UNIT_NONE,
        BB_SYNTAX_TOKEN_TRIVIA,
        (uint32_t)start,
        (uint32_t)index
      );
      if (status != BB_STATUS_OK) return status;
      continue;
    }
    if (byte == '/' && index + 1 < length && bytes[index + 1] == '*') {
      int terminated = 0;
      index += 2;
      while (index + 1 < length) {
        if (bytes[index] == '*' && bytes[index + 1] == '/') {
          index += 2;
          terminated = 1;
          break;
        }
        index += 1;
      }
      if (!terminated) index = length;
      status = bb_syntax_add_token(
        tree,
        BB_SYNTAX_TOKEN_COMMENT,
        BB_SYNTAX_UNIT_NONE,
        BB_SYNTAX_TOKEN_TRIVIA,
        (uint32_t)start,
        (uint32_t)index
      );
      if (status != BB_STATUS_OK) return status;
      if (bb_range_contains_newline(bytes, start, index)) starts_line = 1;
      if (!terminated) {
        status = bb_syntax_push_diagnostic(
          tree,
          BB_SYNTAX_DIAGNOSTIC_UNTERMINATED_COMMENT,
          "Unterminated block comment.",
          (uint32_t)start,
          (uint32_t)index
        );
        if (status != BB_STATUS_OK) return status;
      }
      continue;
    }
    if (byte == ':' && index + 1 < length && bytes[index + 1] == '=') {
      index += 2;
      status = bb_syntax_add_token(
        tree,
        BB_SYNTAX_TOKEN_SYMBOL,
        BB_SYNTAX_UNIT_NONE,
        significant_flags,
        (uint32_t)start,
        (uint32_t)index
      );
      if (status != BB_STATUS_OK) return status;
      starts_line = 0;
      continue;
    }
    if (byte == '[' || byte == ']' || byte == '(' || byte == ')' || byte == ',' || byte == ':' || byte == ';' ||
        (byte == '.' && (index + 1 >= length || !bb_ascii_is_digit(bytes[index + 1])))) {
      index += 1;
      status = bb_syntax_add_token(
        tree,
        BB_SYNTAX_TOKEN_SYMBOL,
        BB_SYNTAX_UNIT_NONE,
        significant_flags,
        (uint32_t)start,
        (uint32_t)index
      );
      if (status != BB_STATUS_OK) return status;
      starts_line = 0;
      continue;
    }
    if (byte == '#') {
      size_t digits;
      index += 1;
      while (index < length && bb_ascii_is_hex(bytes[index])) index += 1;
      digits = index - start - 1;
      if (digits == 6 || digits == 8) {
        status = bb_syntax_add_token(
          tree,
          BB_SYNTAX_TOKEN_COLOR,
          BB_SYNTAX_UNIT_NONE,
          significant_flags,
          (uint32_t)start,
          (uint32_t)index
        );
      } else {
        status = bb_syntax_add_token(
          tree,
          BB_SYNTAX_TOKEN_ERROR,
          BB_SYNTAX_UNIT_NONE,
          significant_flags,
          (uint32_t)start,
          (uint32_t)index
        );
        if (status == BB_STATUS_OK)
          status = bb_syntax_push_diagnostic(
            tree,
            BB_SYNTAX_DIAGNOSTIC_INVALID_COLOR,
            "Expected a color in #rrggbb or #rrggbbaa form.",
            (uint32_t)start,
            (uint32_t)index
          );
      }
      if (status != BB_STATUS_OK) return status;
      starts_line = 0;
      continue;
    }
    if (byte == '"' || byte == '\'') {
      const uint8_t quote = byte;
      int terminated = 0;
      index += 1;
      while (index < length) {
        if (bytes[index] == '\\') {
          index += 1;
          if (index < length) index += 1;
          continue;
        }
        if (bytes[index] == quote) {
          index += 1;
          terminated = 1;
          break;
        }
        index += 1;
      }
      status = bb_syntax_add_token(
        tree,
        terminated ? BB_SYNTAX_TOKEN_STRING : BB_SYNTAX_TOKEN_ERROR,
        BB_SYNTAX_UNIT_NONE,
        significant_flags,
        (uint32_t)start,
        (uint32_t)index
      );
      if (status == BB_STATUS_OK && !terminated)
        status = bb_syntax_push_diagnostic(
          tree,
          BB_SYNTAX_DIAGNOSTIC_UNTERMINATED_STRING,
          "Unterminated string literal.",
          (uint32_t)start,
          (uint32_t)index
        );
      if (status != BB_STATUS_OK) return status;
      starts_line = 0;
      continue;
    }
    if (bb_ascii_is_digit(byte) ||
        (byte == '.' && index + 1 < length && bb_ascii_is_digit(bytes[index + 1])) ||
        (byte == '-' && index + 1 < length &&
         (bb_ascii_is_digit(bytes[index + 1]) ||
          (bytes[index + 1] == '.' && index + 2 < length && bb_ascii_is_digit(bytes[index + 2]))))) {
      bb_syntax_number_unit unit = BB_SYNTAX_UNIT_NONE;
      if (bytes[index] == '-') index += 1;
      while (index < length && bb_ascii_is_digit(bytes[index])) index += 1;
      if (index < length && bytes[index] == '.') {
        index += 1;
        while (index < length && bb_ascii_is_digit(bytes[index])) index += 1;
      }
      if (index < length && (bytes[index] == 'e' || bytes[index] == 'E')) {
        const size_t exponent_start = index;
        size_t exponent = index + 1;
        if (exponent < length && (bytes[exponent] == '+' || bytes[exponent] == '-')) exponent += 1;
        if (exponent < length && bb_ascii_is_digit(bytes[exponent])) {
          index = exponent + 1;
          while (index < length && bb_ascii_is_digit(bytes[index])) index += 1;
        } else {
          index = exponent_start;
        }
      }
      if (index < length && bytes[index] == '%') {
        unit = BB_SYNTAX_UNIT_PERCENTAGE;
        index += 1;
      } else if (index + 3 <= length && memcmp(bytes + index, "deg", 3) == 0 &&
                 (index + 3 == length || !bb_ascii_is_identifier_continue(bytes[index + 3]))) {
        unit = BB_SYNTAX_UNIT_DEGREES;
        index += 3;
      }
      status = bb_syntax_add_token(
        tree,
        BB_SYNTAX_TOKEN_NUMBER,
        unit,
        significant_flags,
        (uint32_t)start,
        (uint32_t)index
      );
      if (status != BB_STATUS_OK) return status;
      starts_line = 0;
      continue;
    }
    if (bb_ascii_is_identifier_start(byte)) {
      index += 1;
      while (index < length && bb_ascii_is_identifier_continue(bytes[index])) index += 1;
      status = bb_syntax_add_token(
        tree,
        BB_SYNTAX_TOKEN_IDENTIFIER,
        BB_SYNTAX_UNIT_NONE,
        significant_flags,
        (uint32_t)start,
        (uint32_t)index
      );
      if (status != BB_STATUS_OK) return status;
      starts_line = 0;
      continue;
    }
    index += 1;
    status = bb_syntax_add_token(
      tree,
      BB_SYNTAX_TOKEN_ERROR,
      BB_SYNTAX_UNIT_NONE,
      significant_flags,
      (uint32_t)start,
      (uint32_t)index
    );
    if (status == BB_STATUS_OK)
      status = bb_syntax_push_diagnostic(
        tree,
        BB_SYNTAX_DIAGNOSTIC_UNEXPECTED_CHARACTER,
        "Unexpected character.",
        (uint32_t)start,
        (uint32_t)index
      );
    if (status != BB_STATUS_OK) return status;
    starts_line = 0;
  }
  return bb_syntax_add_token(
    tree,
    BB_SYNTAX_TOKEN_EOF,
    BB_SYNTAX_UNIT_NONE,
    starts_line ? BB_SYNTAX_TOKEN_STARTS_LINE : 0,
    (uint32_t)length,
    (uint32_t)length
  );
}

static bb_status bb_syntax_add_node(
  bb_syntax_tree *tree,
  bb_syntax_node_kind kind,
  uint32_t start,
  uint32_t end,
  uint32_t primary_token,
  bb_syntax_node *out_node
) {
  bb_syntax_node_record *record;
  bb_status status;
  if (out_node == NULL) return BB_STATUS_INVALID_ARGUMENT;
  *out_node = BB_SYNTAX_NODE_NONE;
  status = bb_syntax_grow(
    tree->context,
    (void **)&tree->nodes,
    &tree->node_capacity,
    sizeof(*tree->nodes),
    _Alignof(bb_syntax_node_record),
    tree->node_count + 1,
    tree->limits.max_syntax_nodes
  );
  if (status != BB_STATUS_OK) return status;
  record = &tree->nodes[tree->node_count];
  memset(record, 0, sizeof(*record));
  record->info.kind = kind;
  record->info.span = (bb_span){tree->source.id, start, end};
  record->info.primary_token = primary_token;
  tree->node_count += 1;
  *out_node = (bb_syntax_node)tree->node_count;
  return BB_STATUS_OK;
}

static bb_status bb_syntax_add_child(bb_syntax_tree *tree, bb_syntax_node parent, bb_syntax_node child) {
  bb_syntax_node_record *parent_record;
  bb_syntax_child_record *child_record;
  uint32_t handle;
  bb_status status;
  if (parent == BB_SYNTAX_NODE_NONE || parent > tree->node_count || child == BB_SYNTAX_NODE_NONE ||
      child > tree->node_count) return BB_STATUS_INVALID_ARGUMENT;
  status = bb_syntax_grow(
    tree->context,
    (void **)&tree->children,
    &tree->child_capacity,
    sizeof(*tree->children),
    _Alignof(bb_syntax_child_record),
    tree->child_count + 1,
    tree->limits.max_syntax_nodes
  );
  if (status != BB_STATUS_OK) return status;
  handle = (uint32_t)(tree->child_count + 1);
  child_record = &tree->children[tree->child_count];
  child_record->node = child;
  child_record->next = 0;
  tree->child_count += 1;
  parent_record = &tree->nodes[parent - 1];
  if (parent_record->last_child == 0) parent_record->first_child = handle;
  else tree->children[parent_record->last_child - 1].next = handle;
  parent_record->last_child = handle;
  parent_record->info.child_count += 1;
  return BB_STATUS_OK;
}

static void bb_syntax_finish_node(bb_syntax_tree *tree, bb_syntax_node node, uint32_t end) {
  if (node != BB_SYNTAX_NODE_NONE && node <= tree->node_count) tree->nodes[node - 1].info.span.byte_end = end;
}

static size_t bb_parser_significant_from(const bb_parser *parser, size_t cursor, size_t offset) {
  size_t found = 0;
  while (cursor < parser->tree->token_count) {
    if ((parser->tree->tokens[cursor].flags & BB_SYNTAX_TOKEN_TRIVIA) == 0) {
      if (found == offset) return cursor;
      found += 1;
    }
    cursor += 1;
  }
  return parser->tree->token_count - 1;
}

static size_t bb_parser_peek(const bb_parser *parser, size_t offset) {
  return bb_parser_significant_from(parser, parser->cursor, offset);
}

static size_t bb_parser_advance(bb_parser *parser) {
  size_t token = bb_parser_peek(parser, 0);
  parser->cursor = token + 1;
  return token;
}

static int bb_parser_token_text_is(const bb_parser *parser, size_t token, const char *text) {
  const bb_syntax_token_record *record = &parser->tree->tokens[token];
  const size_t length = strlen(text);
  return record->span.byte_end - record->span.byte_start == length &&
         (length == 0 || memcmp(parser->tree->source.contents.data + record->span.byte_start, text, length) == 0);
}

static int bb_parser_is_symbol(const bb_parser *parser, size_t token, const char *symbol) {
  return parser->tree->tokens[token].kind == BB_SYNTAX_TOKEN_SYMBOL && bb_parser_token_text_is(parser, token, symbol);
}

static int bb_parser_is_identifier(const bb_parser *parser, size_t token, const char *identifier) {
  return parser->tree->tokens[token].kind == BB_SYNTAX_TOKEN_IDENTIFIER &&
         bb_parser_token_text_is(parser, token, identifier);
}

static int bb_parser_match_symbol(bb_parser *parser, const char *symbol) {
  const size_t token = bb_parser_peek(parser, 0);
  if (!bb_parser_is_symbol(parser, token, symbol)) return 0;
  (void)bb_parser_advance(parser);
  return 1;
}

static void bb_parser_diagnostic(
  bb_parser *parser,
  bb_syntax_diagnostic_code code,
  const char *message,
  size_t token
) {
  const bb_span span = parser->tree->tokens[token].span;
  if (parser->status != BB_STATUS_OK) return;
  parser->status = bb_syntax_push_diagnostic(parser->tree, code, message, span.byte_start, span.byte_end);
}

static int bb_parser_is_nested_boundary(const bb_parser *parser) {
  const size_t token = bb_parser_peek(parser, 0);
  const bb_syntax_token_record *record = &parser->tree->tokens[token];
  return record->kind == BB_SYNTAX_TOKEN_EOF || bb_parser_is_symbol(parser, token, ";") ||
         ((record->flags & BB_SYNTAX_TOKEN_STARTS_LINE) != 0 && record->kind == BB_SYNTAX_TOKEN_IDENTIFIER &&
          (bb_parser_is_identifier(parser, token, "import") ||
           bb_parser_is_symbol(parser, bb_parser_peek(parser, 1), ":=")));
}

static bb_syntax_node bb_parser_error_node(bb_parser *parser, size_t token) {
  bb_syntax_node node = BB_SYNTAX_NODE_NONE;
  const bb_span span = parser->tree->tokens[token].span;
  if (parser->status == BB_STATUS_OK)
    parser->status = bb_syntax_add_node(
      parser->tree,
      BB_SYNTAX_ERROR_NODE,
      span.byte_start,
      span.byte_end,
      (uint32_t)token,
      &node
    );
  return node;
}

static bb_syntax_node bb_parser_parse_expression(bb_parser *parser, uint32_t depth);

static int bb_parser_is_gradient_name(const bb_parser *parser, size_t token) {
  return bb_parser_token_text_is(parser, token, "linear-gradient") ||
         bb_parser_token_text_is(parser, token, "lin-grad") || bb_parser_token_text_is(parser, token, "lg") ||
         bb_parser_token_text_is(parser, token, "radial-gradient") ||
         bb_parser_token_text_is(parser, token, "rad-grad") || bb_parser_token_text_is(parser, token, "rg");
}

static void bb_parser_parse_arguments(
  bb_parser *parser,
  bb_syntax_node call,
  uint32_t depth,
  int allow_gradient_stops
) {
  for (;;) {
    size_t token = bb_parser_peek(parser, 0);
    if (bb_parser_match_symbol(parser, ")")) {
      bb_syntax_finish_node(parser->tree, call, parser->tree->tokens[token].span.byte_end);
      return;
    }
    if (bb_parser_is_nested_boundary(parser)) {
      bb_parser_diagnostic(
        parser,
        BB_SYNTAX_DIAGNOSTIC_EXPECTED_CLOSE_PAREN,
        "Expected a closing parenthesis.",
        token
      );
      bb_syntax_finish_node(parser->tree, call, parser->tree->tokens[token].span.byte_start);
      return;
    }
    if (bb_parser_is_symbol(parser, token, ",")) {
      bb_parser_diagnostic(
        parser,
        BB_SYNTAX_DIAGNOSTIC_EXPECTED_EXPRESSION,
        "Expected an expression before the comma.",
        token
      );
      (void)bb_parser_advance(parser);
      continue;
    }
    if (parser->tree->tokens[token].kind == BB_SYNTAX_TOKEN_IDENTIFIER &&
        bb_parser_is_symbol(parser, bb_parser_peek(parser, 1), ":")) {
      const size_t name_token = bb_parser_advance(parser);
      bb_syntax_node name = BB_SYNTAX_NODE_NONE;
      bb_syntax_node value;
      bb_syntax_node named = BB_SYNTAX_NODE_NONE;
      const bb_span name_span = parser->tree->tokens[name_token].span;
      (void)bb_parser_advance(parser);
      if (parser->status == BB_STATUS_OK)
        parser->status = bb_syntax_add_node(
          parser->tree,
          BB_SYNTAX_IDENTIFIER_EXPRESSION,
          name_span.byte_start,
          name_span.byte_end,
          (uint32_t)name_token,
          &name
        );
      value = bb_parser_parse_expression(parser, depth + 1);
      if (parser->status == BB_STATUS_OK)
        parser->status = bb_syntax_add_node(
          parser->tree,
          BB_SYNTAX_NAMED_ARGUMENT,
          name_span.byte_start,
          value == BB_SYNTAX_NODE_NONE ? name_span.byte_end : parser->tree->nodes[value - 1].info.span.byte_end,
          (uint32_t)name_token,
          &named
        );
      if (parser->status == BB_STATUS_OK) parser->status = bb_syntax_add_child(parser->tree, named, name);
      if (parser->status == BB_STATUS_OK) parser->status = bb_syntax_add_child(parser->tree, named, value);
      if (parser->status == BB_STATUS_OK) parser->status = bb_syntax_add_child(parser->tree, call, named);
    } else {
      bb_syntax_node argument = bb_parser_parse_expression(parser, depth + 1);
      if (allow_gradient_stops && parser->status == BB_STATUS_OK) {
        const size_t offset_token = bb_parser_peek(parser, 0);
        if (parser->tree->tokens[offset_token].kind == BB_SYNTAX_TOKEN_NUMBER &&
            parser->tree->tokens[offset_token].unit == BB_SYNTAX_UNIT_PERCENTAGE) {
          bb_syntax_node offset = BB_SYNTAX_NODE_NONE;
          bb_syntax_node stop = BB_SYNTAX_NODE_NONE;
          const bb_span offset_span = parser->tree->tokens[offset_token].span;
          (void)bb_parser_advance(parser);
          parser->status = bb_syntax_add_node(
            parser->tree,
            BB_SYNTAX_NUMBER_LITERAL,
            offset_span.byte_start,
            offset_span.byte_end,
            (uint32_t)offset_token,
            &offset
          );
          if (parser->status == BB_STATUS_OK)
            parser->status = bb_syntax_add_node(
              parser->tree,
              BB_SYNTAX_GRADIENT_STOP,
              parser->tree->nodes[argument - 1].info.span.byte_start,
              offset_span.byte_end,
              BB_SYNTAX_TOKEN_NONE,
              &stop
            );
          if (parser->status == BB_STATUS_OK) parser->status = bb_syntax_add_child(parser->tree, stop, argument);
          if (parser->status == BB_STATUS_OK) parser->status = bb_syntax_add_child(parser->tree, stop, offset);
          argument = stop;
        }
      }
      if (parser->status == BB_STATUS_OK) parser->status = bb_syntax_add_child(parser->tree, call, argument);
    }
    if (parser->status != BB_STATUS_OK) return;
    token = bb_parser_peek(parser, 0);
    if (bb_parser_match_symbol(parser, ",")) continue;
    if (bb_parser_is_symbol(parser, token, ")")) continue;
    if (bb_parser_is_nested_boundary(parser)) continue;
    bb_parser_diagnostic(
      parser,
      BB_SYNTAX_DIAGNOSTIC_EXPECTED_SEPARATOR,
      "Expected a comma or closing parenthesis.",
      token
    );
    while (!bb_parser_is_symbol(parser, bb_parser_peek(parser, 0), ",") &&
           !bb_parser_is_symbol(parser, bb_parser_peek(parser, 0), ")") &&
           !bb_parser_is_nested_boundary(parser)) (void)bb_parser_advance(parser);
    (void)bb_parser_match_symbol(parser, ",");
  }
}

static bb_syntax_node bb_parser_parse_primary(bb_parser *parser, uint32_t depth) {
  size_t token;
  bb_span span;
  bb_syntax_node node = BB_SYNTAX_NODE_NONE;
  bb_syntax_token_kind kind;
  if (parser->status != BB_STATUS_OK) return BB_SYNTAX_NODE_NONE;
  token = bb_parser_peek(parser, 0);
  span = parser->tree->tokens[token].span;
  if (depth > parser->tree->limits.max_syntax_depth) {
    bb_parser_diagnostic(parser, BB_SYNTAX_DIAGNOSTIC_DEPTH_LIMIT, "Syntax nesting limit exceeded.", token);
    if (parser->tree->tokens[token].kind != BB_SYNTAX_TOKEN_EOF) (void)bb_parser_advance(parser);
    return bb_parser_error_node(parser, token);
  }
  kind = parser->tree->tokens[token].kind;
  if (kind == BB_SYNTAX_TOKEN_ERROR) {
    (void)bb_parser_advance(parser);
    return bb_parser_error_node(parser, token);
  }
  if (kind == BB_SYNTAX_TOKEN_NUMBER || kind == BB_SYNTAX_TOKEN_STRING || kind == BB_SYNTAX_TOKEN_COLOR) {
    bb_syntax_node_kind node_kind = kind == BB_SYNTAX_TOKEN_NUMBER   ? BB_SYNTAX_NUMBER_LITERAL
                                    : kind == BB_SYNTAX_TOKEN_STRING ? BB_SYNTAX_STRING_LITERAL
                                                                     : BB_SYNTAX_COLOR_LITERAL;
    (void)bb_parser_advance(parser);
    parser->status = bb_syntax_add_node(
      parser->tree,
      node_kind,
      span.byte_start,
      span.byte_end,
      (uint32_t)token,
      &node
    );
    return node;
  }
  if (bb_parser_is_symbol(parser, token, "[")) {
    (void)bb_parser_advance(parser);
    parser->status = bb_syntax_add_node(
      parser->tree,
      BB_SYNTAX_ARRAY_EXPRESSION,
      span.byte_start,
      span.byte_end,
      BB_SYNTAX_TOKEN_NONE,
      &node
    );
    while (parser->status == BB_STATUS_OK) {
      const size_t next = bb_parser_peek(parser, 0);
      if (bb_parser_match_symbol(parser, "]")) {
        bb_syntax_finish_node(parser->tree, node, parser->tree->tokens[next].span.byte_end);
        break;
      }
      if (bb_parser_is_nested_boundary(parser)) {
        bb_parser_diagnostic(
          parser,
          BB_SYNTAX_DIAGNOSTIC_EXPECTED_CLOSE_BRACKET,
          "Expected a closing bracket.",
          next
        );
        bb_syntax_finish_node(parser->tree, node, parser->tree->tokens[next].span.byte_start);
        break;
      }
      if (bb_parser_match_symbol(parser, ",")) {
        bb_parser_diagnostic(
          parser,
          BB_SYNTAX_DIAGNOSTIC_EXPECTED_EXPRESSION,
          "Expected an expression before the comma.",
          next
        );
        continue;
      }
      {
        const bb_syntax_node item = bb_parser_parse_expression(parser, depth + 1);
        if (parser->status == BB_STATUS_OK) parser->status = bb_syntax_add_child(parser->tree, node, item);
      }
      if (parser->status != BB_STATUS_OK) break;
      if (bb_parser_match_symbol(parser, ",")) continue;
      if (bb_parser_is_symbol(parser, bb_parser_peek(parser, 0), "]")) continue;
      if (bb_parser_is_nested_boundary(parser)) continue;
      bb_parser_diagnostic(
        parser,
        BB_SYNTAX_DIAGNOSTIC_EXPECTED_SEPARATOR,
        "Expected a comma or closing bracket.",
        bb_parser_peek(parser, 0)
      );
      while (!bb_parser_is_symbol(parser, bb_parser_peek(parser, 0), ",") &&
             !bb_parser_is_symbol(parser, bb_parser_peek(parser, 0), "]") &&
             !bb_parser_is_nested_boundary(parser)) (void)bb_parser_advance(parser);
      (void)bb_parser_match_symbol(parser, ",");
    }
    return node;
  }
  if (bb_parser_is_symbol(parser, token, "(")) {
    bb_syntax_node expression;
    (void)bb_parser_advance(parser);
    parser->status = bb_syntax_add_node(
      parser->tree,
      BB_SYNTAX_GROUP_EXPRESSION,
      span.byte_start,
      span.byte_end,
      BB_SYNTAX_TOKEN_NONE,
      &node
    );
    expression = bb_parser_parse_expression(parser, depth + 1);
    if (parser->status == BB_STATUS_OK) parser->status = bb_syntax_add_child(parser->tree, node, expression);
    token = bb_parser_peek(parser, 0);
    if (bb_parser_match_symbol(parser, ")")) bb_syntax_finish_node(parser->tree, node, parser->tree->tokens[token].span.byte_end);
    else {
      bb_parser_diagnostic(
        parser,
        BB_SYNTAX_DIAGNOSTIC_EXPECTED_CLOSE_PAREN,
        "Expected a closing parenthesis.",
        token
      );
      bb_syntax_finish_node(parser->tree, node, parser->tree->tokens[token].span.byte_start);
    }
    return node;
  }
  if (kind == BB_SYNTAX_TOKEN_IDENTIFIER) {
    bb_syntax_node identifier = BB_SYNTAX_NODE_NONE;
    (void)bb_parser_advance(parser);
    parser->status = bb_syntax_add_node(
      parser->tree,
      BB_SYNTAX_IDENTIFIER_EXPRESSION,
      span.byte_start,
      span.byte_end,
      (uint32_t)token,
      &identifier
    );
    if (parser->status != BB_STATUS_OK || !bb_parser_match_symbol(parser, "(")) return identifier;
    parser->status = bb_syntax_add_node(
      parser->tree,
      BB_SYNTAX_CALL_EXPRESSION,
      span.byte_start,
      span.byte_end,
      (uint32_t)token,
      &node
    );
    if (parser->status == BB_STATUS_OK) parser->status = bb_syntax_add_child(parser->tree, node, identifier);
    bb_parser_parse_arguments(parser, node, depth, bb_parser_is_gradient_name(parser, token));
    return node;
  }
  bb_parser_diagnostic(
    parser,
    BB_SYNTAX_DIAGNOSTIC_EXPECTED_EXPRESSION,
    "Expected an expression.",
    token
  );
  if (kind != BB_SYNTAX_TOKEN_EOF) (void)bb_parser_advance(parser);
  return bb_parser_error_node(parser, token);
}

static bb_syntax_node bb_parser_parse_expression(bb_parser *parser, uint32_t depth) {
  bb_syntax_node expression = bb_parser_parse_primary(parser, depth);
  while (parser->status == BB_STATUS_OK && bb_parser_match_symbol(parser, ".")) {
    const size_t name_token = bb_parser_peek(parser, 0);
    bb_syntax_node name = BB_SYNTAX_NODE_NONE;
    bb_syntax_node member = BB_SYNTAX_NODE_NONE;
    uint32_t end = parser->tree->nodes[expression - 1].info.span.byte_end;
    if (parser->tree->tokens[name_token].kind == BB_SYNTAX_TOKEN_IDENTIFIER) {
      const bb_span name_span = parser->tree->tokens[name_token].span;
      (void)bb_parser_advance(parser);
      parser->status = bb_syntax_add_node(
        parser->tree,
        BB_SYNTAX_IDENTIFIER_EXPRESSION,
        name_span.byte_start,
        name_span.byte_end,
        (uint32_t)name_token,
        &name
      );
      end = name_span.byte_end;
    } else {
      bb_parser_diagnostic(
        parser,
        BB_SYNTAX_DIAGNOSTIC_EXPECTED_METHOD,
        "Expected a method name after the dot.",
        name_token
      );
      if (parser->tree->tokens[name_token].kind != BB_SYNTAX_TOKEN_EOF) (void)bb_parser_advance(parser);
      name = bb_parser_error_node(parser, name_token);
    }
    if (parser->status == BB_STATUS_OK)
      parser->status = bb_syntax_add_node(
        parser->tree,
        BB_SYNTAX_MEMBER_CALL_EXPRESSION,
        parser->tree->nodes[expression - 1].info.span.byte_start,
        end,
        parser->tree->tokens[name_token].kind == BB_SYNTAX_TOKEN_IDENTIFIER ? (uint32_t)name_token
                                                                            : BB_SYNTAX_TOKEN_NONE,
        &member
      );
    if (parser->status == BB_STATUS_OK) parser->status = bb_syntax_add_child(parser->tree, member, expression);
    if (parser->status == BB_STATUS_OK) parser->status = bb_syntax_add_child(parser->tree, member, name);
    if (!bb_parser_match_symbol(parser, "(")) {
      bb_parser_diagnostic(
        parser,
        BB_SYNTAX_DIAGNOSTIC_EXPECTED_OPEN_PAREN,
        "Expected an opening parenthesis after the method name.",
        bb_parser_peek(parser, 0)
      );
      expression = member;
      continue;
    }
    bb_parser_parse_arguments(parser, member, depth, 0);
    expression = member;
  }
  return expression;
}

static bb_syntax_node bb_parser_parse_statement(bb_parser *parser) {
  size_t token = bb_parser_peek(parser, 0);
  const bb_span start_span = parser->tree->tokens[token].span;
  bb_syntax_node statement = BB_SYNTAX_NODE_NONE;
  if (bb_parser_is_identifier(parser, token, "import")) {
    bb_syntax_node path;
    size_t path_token;
    (void)bb_parser_advance(parser);
    path_token = bb_parser_peek(parser, 0);
    if (parser->tree->tokens[path_token].kind == BB_SYNTAX_TOKEN_STRING) {
      const bb_span path_span = parser->tree->tokens[path_token].span;
      (void)bb_parser_advance(parser);
      parser->status = bb_syntax_add_node(
        parser->tree,
        BB_SYNTAX_STRING_LITERAL,
        path_span.byte_start,
        path_span.byte_end,
        (uint32_t)path_token,
        &path
      );
    } else {
      bb_parser_diagnostic(
        parser,
        BB_SYNTAX_DIAGNOSTIC_EXPECTED_IMPORT_PATH,
        "Expected an import path string.",
        path_token
      );
      if (!bb_parser_is_nested_boundary(parser)) (void)bb_parser_advance(parser);
      path = bb_parser_error_node(parser, path_token);
    }
    if (parser->status == BB_STATUS_OK)
      parser->status = bb_syntax_add_node(
        parser->tree,
        BB_SYNTAX_IMPORT_STATEMENT,
        start_span.byte_start,
        parser->tree->nodes[path - 1].info.span.byte_end,
        (uint32_t)token,
        &statement
      );
    if (parser->status == BB_STATUS_OK) parser->status = bb_syntax_add_child(parser->tree, statement, path);
    return statement;
  }
  if (parser->tree->tokens[token].kind == BB_SYNTAX_TOKEN_IDENTIFIER &&
      bb_parser_is_symbol(parser, bb_parser_peek(parser, 1), ":=")) {
    bb_syntax_node name = BB_SYNTAX_NODE_NONE;
    bb_syntax_node value;
    const size_t name_token = bb_parser_advance(parser);
    (void)bb_parser_advance(parser);
    parser->status = bb_syntax_add_node(
      parser->tree,
      BB_SYNTAX_IDENTIFIER_EXPRESSION,
      start_span.byte_start,
      start_span.byte_end,
      (uint32_t)name_token,
      &name
    );
    value = bb_parser_parse_expression(parser, 1);
    if (parser->status == BB_STATUS_OK)
      parser->status = bb_syntax_add_node(
        parser->tree,
        BB_SYNTAX_BINDING_STATEMENT,
        start_span.byte_start,
        parser->tree->nodes[value - 1].info.span.byte_end,
        (uint32_t)name_token,
        &statement
      );
    if (parser->status == BB_STATUS_OK) parser->status = bb_syntax_add_child(parser->tree, statement, name);
    if (parser->status == BB_STATUS_OK) parser->status = bb_syntax_add_child(parser->tree, statement, value);
    return statement;
  }
  {
    const bb_syntax_node expression = bb_parser_parse_expression(parser, 1);
    if (parser->status == BB_STATUS_OK)
      parser->status = bb_syntax_add_node(
        parser->tree,
        BB_SYNTAX_EXPRESSION_STATEMENT,
        start_span.byte_start,
        parser->tree->nodes[expression - 1].info.span.byte_end,
        BB_SYNTAX_TOKEN_NONE,
        &statement
      );
    if (parser->status == BB_STATUS_OK) parser->status = bb_syntax_add_child(parser->tree, statement, expression);
  }
  return statement;
}

static bb_status bb_syntax_parse_tokens(bb_syntax_tree *tree) {
  bb_parser parser;
  bb_syntax_node program = BB_SYNTAX_NODE_NONE;
  parser.tree = tree;
  parser.cursor = 0;
  parser.status = bb_syntax_add_node(
    tree,
    BB_SYNTAX_PROGRAM,
    0,
    (uint32_t)tree->source.contents.length,
    BB_SYNTAX_TOKEN_NONE,
    &program
  );
  tree->root = program;
  while (parser.status == BB_STATUS_OK) {
    const size_t before = bb_parser_peek(&parser, 0);
    bb_syntax_node statement;
    if (tree->tokens[before].kind == BB_SYNTAX_TOKEN_EOF) break;
    if (bb_parser_match_symbol(&parser, ";")) continue;
    statement = bb_parser_parse_statement(&parser);
    if (parser.status == BB_STATUS_OK) parser.status = bb_syntax_add_child(tree, program, statement);
    (void)bb_parser_match_symbol(&parser, ";");
    if (parser.status == BB_STATUS_OK && bb_parser_peek(&parser, 0) == before) {
      bb_parser_diagnostic(
        &parser,
        BB_SYNTAX_DIAGNOSTIC_EXPECTED_EXPRESSION,
        "Parser could not make progress.",
        before
      );
      if (tree->tokens[before].kind != BB_SYNTAX_TOKEN_EOF) (void)bb_parser_advance(&parser);
    }
  }
  return parser.status;
}

static bb_status bb_syntax_build_diagnostic_order(bb_syntax_tree *tree) {
  const size_t count = bb_diagnostic_store_count(&tree->diagnostics);
  size_t bytes;
  size_t index;
  bb_status status;
  if (!bb_size_multiply(count, sizeof(*tree->diagnostic_order), &bytes)) return BB_STATUS_OVERFLOW;
  tree->diagnostic_order_bytes = bytes;
  status = bb_context_allocate(
    tree->context,
    bytes,
    _Alignof(size_t),
    (void **)&tree->diagnostic_order
  );
  if (status != BB_STATUS_OK) return status;
  for (index = 0; index < count; index += 1) {
    size_t position = index;
    bb_diagnostic current;
    tree->diagnostic_order[index] = index;
    status = bb_diagnostic_store_get(&tree->diagnostics, index, &current);
    if (status != BB_STATUS_OK) return status;
    while (position > 0) {
      bb_diagnostic previous;
      status = bb_diagnostic_store_get(&tree->diagnostics, tree->diagnostic_order[position - 1], &previous);
      if (status != BB_STATUS_OK) return status;
      if (previous.primary_span.byte_start < current.primary_span.byte_start ||
          (previous.primary_span.byte_start == current.primary_span.byte_start &&
           previous.primary_span.byte_end <= current.primary_span.byte_end)) break;
      tree->diagnostic_order[position] = tree->diagnostic_order[position - 1];
      position -= 1;
    }
    tree->diagnostic_order[position] = index;
  }
  return BB_STATUS_OK;
}

bb_status bb_syntax_parse(bb_context *context, bb_source_id source_id, bb_syntax_tree **out_tree) {
  bb_syntax_tree *tree = NULL;
  bb_source_info source;
  bb_status status;
  if (out_tree == NULL) return BB_STATUS_INVALID_ARGUMENT;
  *out_tree = NULL;
  if (context == NULL) return BB_STATUS_INVALID_ARGUMENT;
  status = bb_context_source_info(context, source_id, &source);
  if (status != BB_STATUS_OK) return status;
  status = bb_context_allocate(context, sizeof(*tree), _Alignof(bb_syntax_tree), (void **)&tree);
  if (status != BB_STATUS_OK) return status;
  memset(tree, 0, sizeof(*tree));
  tree->context = context;
  tree->source = source;
  status = bb_context_get_limits(context, &tree->limits);
  if (status != BB_STATUS_OK) goto fail;
  status = bb_diagnostic_store_init(&tree->diagnostics, context);
  if (status != BB_STATUS_OK) goto fail;
  status = bb_syntax_lex(tree);
  if (status != BB_STATUS_OK) goto fail;
  status = bb_syntax_parse_tokens(tree);
  if (status != BB_STATUS_OK) goto fail;
  status = bb_syntax_build_diagnostic_order(tree);
  if (status != BB_STATUS_OK) goto fail;
  *out_tree = tree;
  return BB_STATUS_OK;

fail:
  bb_syntax_tree_destroy(tree);
  return status;
}

void bb_syntax_tree_destroy(bb_syntax_tree *tree) {
  size_t bytes;
  bb_context *context;
  if (tree == NULL) return;
  context = tree->context;
  bb_context_deallocate(context, tree->diagnostic_order, tree->diagnostic_order_bytes, _Alignof(size_t));
  bb_diagnostic_store_destroy(&tree->diagnostics);
  bytes = tree->child_capacity * sizeof(*tree->children);
  bb_context_deallocate(context, tree->children, bytes, _Alignof(bb_syntax_child_record));
  bytes = tree->node_capacity * sizeof(*tree->nodes);
  bb_context_deallocate(context, tree->nodes, bytes, _Alignof(bb_syntax_node_record));
  bytes = tree->token_capacity * sizeof(*tree->tokens);
  bb_context_deallocate(context, tree->tokens, bytes, _Alignof(bb_syntax_token_record));
  bb_context_deallocate(context, tree, sizeof(*tree), _Alignof(bb_syntax_tree));
}

bb_source_id bb_syntax_tree_source_id(const bb_syntax_tree *tree) {
  return tree == NULL ? BB_SOURCE_ID_NONE : tree->source.id;
}

bb_syntax_node bb_syntax_tree_root(const bb_syntax_tree *tree) {
  return tree == NULL ? BB_SYNTAX_NODE_NONE : tree->root;
}

size_t bb_syntax_tree_token_count(const bb_syntax_tree *tree) {
  return tree == NULL ? 0 : tree->token_count;
}

bb_status bb_syntax_tree_token(const bb_syntax_tree *tree, size_t index, bb_syntax_token_info *out_token) {
  const bb_syntax_token_record *record;
  if (tree == NULL || out_token == NULL) return BB_STATUS_INVALID_ARGUMENT;
  if (index >= tree->token_count) return BB_STATUS_NOT_FOUND;
  record = &tree->tokens[index];
  out_token->kind = record->kind;
  out_token->unit = record->unit;
  out_token->flags = record->flags;
  out_token->span = record->span;
  out_token->text.data = record->span.byte_end == record->span.byte_start
                           ? NULL
                           : (const char *)tree->source.contents.data + record->span.byte_start;
  out_token->text.length = record->span.byte_end - record->span.byte_start;
  return BB_STATUS_OK;
}

size_t bb_syntax_tree_node_count(const bb_syntax_tree *tree) {
  return tree == NULL ? 0 : tree->node_count;
}

bb_status bb_syntax_tree_node(const bb_syntax_tree *tree, bb_syntax_node node, bb_syntax_node_info *out_node) {
  if (tree == NULL || out_node == NULL || node == BB_SYNTAX_NODE_NONE) return BB_STATUS_INVALID_ARGUMENT;
  if (node > tree->node_count) return BB_STATUS_NOT_FOUND;
  *out_node = tree->nodes[node - 1].info;
  return BB_STATUS_OK;
}

bb_status bb_syntax_tree_child(
  const bb_syntax_tree *tree,
  bb_syntax_node node,
  size_t child_index,
  bb_syntax_node *out_child
) {
  uint32_t child;
  size_t index;
  if (tree == NULL || out_child == NULL || node == BB_SYNTAX_NODE_NONE) return BB_STATUS_INVALID_ARGUMENT;
  *out_child = BB_SYNTAX_NODE_NONE;
  if (node > tree->node_count) return BB_STATUS_NOT_FOUND;
  if (child_index >= tree->nodes[node - 1].info.child_count) return BB_STATUS_NOT_FOUND;
  child = tree->nodes[node - 1].first_child;
  for (index = 0; index < child_index; index += 1) child = tree->children[child - 1].next;
  *out_child = tree->children[child - 1].node;
  return BB_STATUS_OK;
}

size_t bb_syntax_tree_diagnostic_count(const bb_syntax_tree *tree) {
  return tree == NULL ? 0 : bb_diagnostic_store_count(&tree->diagnostics);
}

bb_status bb_syntax_tree_diagnostic(const bb_syntax_tree *tree, size_t index, bb_diagnostic *out_diagnostic) {
  const size_t count = bb_syntax_tree_diagnostic_count(tree);
  if (tree == NULL || out_diagnostic == NULL) return BB_STATUS_INVALID_ARGUMENT;
  if (index >= count) return BB_STATUS_NOT_FOUND;
  return bb_diagnostic_store_get(&tree->diagnostics, tree->diagnostic_order[index], out_diagnostic);
}
