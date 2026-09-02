#include "program_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static bb_semantic_value bb_program_evaluate_array(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node array,
  bb_syntax_node_info array_info
) {
  bb_semantic_value result = bb_semantic_error();
  bb_value *artifacts = NULL;
  bb_collection *collection = NULL;
  size_t bytes;
  size_t index;
  bb_status status;
  if (array_info.child_count == 2) {
    bb_syntax_node_info left_info;
    bb_syntax_node_info right_info;
    const bb_syntax_node left_node = bb_program_child(syntax, array, 0);
    const bb_syntax_node right_node = bb_program_child(syntax, array, 1);
    if (bb_program_node_info(syntax, left_node, &left_info) == BB_STATUS_OK &&
        bb_program_node_info(syntax, right_node, &right_info) == BB_STATUS_OK &&
        left_info.kind == BB_SYNTAX_NUMBER_LITERAL && right_info.kind == BB_SYNTAX_NUMBER_LITERAL) {
      bb_semantic_value left = bb_program_evaluate(program, syntax, left_node);
      bb_semantic_value right = bb_program_evaluate(program, syntax, right_node);
      double x;
      double y;
      if (bb_semantic_as_number(left, &x) && bb_semantic_as_number(right, &y)) {
        result.type = BB_SEMANTIC_VECTOR2;
        result.data.vector2.x = x;
        result.data.vector2.y = y;
      } else {
        bb_program_diagnostic_span(
          program,
          BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
          "Vector components must be finite unitless numbers.",
          array_info.span
        );
      }
      bb_semantic_release(&right);
      bb_semantic_release(&left);
      return result;
    }
  }
  if (array_info.child_count == 0) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
      "The initial compiler requires a typed non-empty array.",
      array_info.span
    );
    return result;
  }
  if (!bb_size_multiply(array_info.child_count, sizeof(*artifacts), &bytes)) {
    bb_program_fail(program, BB_STATUS_OVERFLOW);
    return result;
  }
  status = bb_context_allocate(program->context, bytes, _Alignof(bb_value), (void **)&artifacts);
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    return result;
  }
  memset(artifacts, 0, bytes);
  for (index = 0; index < array_info.child_count; index += 1) {
    bb_semantic_value item = bb_program_evaluate(program, syntax, bb_program_child(syntax, array, index));
    bb_graph_asset_desc asset_desc;
    if (item.type != BB_SEMANTIC_IMAGE || item.data.image.key.length == 0) {
      bb_program_diagnostic_span(
        program,
        BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
        "Array image items require stable keys; asset() provides one.",
        array_info.span
      );
      bb_semantic_release(&item);
      goto cleanup;
    }
    artifacts[index].kind = BB_VALUE_ARTIFACT;
    artifacts[index].data.artifact.key = item.data.image.key;
    artifacts[index].data.artifact.path = item.data.image.path;
    artifacts[index].data.artifact.image = item.data.image.node;
    artifacts[index].data.artifact.alias_identity = BB_ALIAS_NONE;
    artifacts[index].data.artifact.provenance = array_info.span;
    if (item.data.image.encoded_alias && program->options.encoded_asset != NULL &&
        bb_image_graph_asset(program->graph, item.data.image.node, &asset_desc) == BB_STATUS_OK) {
      artifacts[index].data.artifact.alias_identity = BB_ALIAS_BYTES;
      artifacts[index].data.artifact.alias_target = asset_desc.content_id;
    }
    bb_semantic_release(&item);
  }
  status = bb_collection_from_values(
    program->context,
    artifacts,
    array_info.child_count,
    &collection
  );
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    goto cleanup;
  }
  result.type = BB_SEMANTIC_COLLECTION;
  result.data.collection.plan = collection;
  result.data.collection.shape = BB_SEMANTIC_COLLECTION_ARTIFACTS;

cleanup:
  bb_context_deallocate(program->context, artifacts, bytes, _Alignof(bb_value));
  return result;
}

bb_semantic_value bb_program_evaluate(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node node
) {
  bb_syntax_node_info info;
  bb_semantic_value value = bb_semantic_error();
  bb_syntax_token_info token;
  if (program->status != BB_STATUS_OK || bb_program_node_info(syntax, node, &info) != BB_STATUS_OK) return value;
  switch (info.kind) {
    case BB_SYNTAX_NUMBER_LITERAL: {
      double number;
      int64_t integer;
      int is_integer;
      if (bb_syntax_tree_token(syntax, info.primary_token, &token) != BB_STATUS_OK ||
          !bb_parse_numeric_token(token, &number, &integer, &is_integer)) {
        bb_program_diagnostic_span(
          program,
          BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
          "Invalid finite numeric literal.",
          info.span
        );
        break;
      }
      if (token.unit == BB_SYNTAX_UNIT_DEGREES) {
        value.type = BB_SEMANTIC_DEGREES;
        value.data.number = number;
      } else if (token.unit == BB_SYNTAX_UNIT_PERCENTAGE) {
        value.type = BB_SEMANTIC_PERCENTAGE;
        value.data.number = number;
      } else if (is_integer) {
        value.type = BB_SEMANTIC_INTEGER;
        value.data.integer = integer;
      } else {
        value.type = BB_SEMANTIC_NUMBER;
        value.data.number = number;
      }
      break;
    }
    case BB_SYNTAX_COLOR_LITERAL:
      value.type = BB_SEMANTIC_COLOR;
      if (!bb_parse_color(bb_program_token_text(syntax, info.primary_token), &value.data.color))
        value = bb_semantic_error();
      break;
    case BB_SYNTAX_STRING_LITERAL:
      value.type = BB_SEMANTIC_STRING;
      value.data.string = bb_program_token_text(syntax, info.primary_token);
      break;
    case BB_SYNTAX_IDENTIFIER_EXPRESSION: {
      const bb_string_view name = bb_program_token_text(syntax, info.primary_token);
      bb_program_binding *binding = bb_program_find_binding(program, name);
      value = binding != NULL ? bb_program_evaluate_binding(program, syntax, binding, info.span)
                              : bb_program_builtin_identifier(program, name, info.span);
      break;
    }
    case BB_SYNTAX_CALL_EXPRESSION:
      value = bb_program_evaluate_call(program, syntax, node, info);
      break;
    case BB_SYNTAX_MEMBER_CALL_EXPRESSION:
      value = bb_program_evaluate_member(program, syntax, node, info);
      break;
    case BB_SYNTAX_GROUP_EXPRESSION:
      value = bb_program_evaluate(program, syntax, bb_program_child(syntax, node, 0));
      break;
    case BB_SYNTAX_ARRAY_EXPRESSION:
      value = bb_program_evaluate_array(program, syntax, node, info);
      break;
    case BB_SYNTAX_ERROR_NODE:
      break;
    default:
      bb_program_diagnostic_span(
        program,
        BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
        "This syntax form is not a value in the initial semantic slice.",
        info.span
      );
      break;
  }
  bb_program_trace_value(program, info.span, &value);
  return value;
}
