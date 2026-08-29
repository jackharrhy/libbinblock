#include <binblock/wasm.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define BB_WASM_MAX_SESSIONS 32
#define BB_WASM_MAX_MODULES 1024
#define BB_WASM_MAX_ASSETS 8192
#define BB_WASM_SESSION_SLOT_BITS 6
#define BB_WASM_SESSION_SLOT_MASK ((UINT32_C(1) << BB_WASM_SESSION_SLOT_BITS) - 1)
#define BB_WASM_SESSION_EPOCH_MASK (UINT32_MAX >> BB_WASM_SESSION_SLOT_BITS)

typedef struct bb_wasm_module_resource {
  uint8_t *specifier;
  uint32_t specifier_length;
  uint8_t *identity;
  uint32_t identity_length;
  uint8_t *source;
  uint32_t source_length;
} bb_wasm_module_resource;

typedef struct bb_wasm_asset_resource {
  uint8_t *logical_id;
  uint32_t logical_id_length;
  uint8_t *content_id;
  uint32_t content_id_length;
  uint32_t width;
  uint32_t height;
  uint8_t *rgba;
  uint32_t rgba_length;
  uint8_t *encoded;
  uint32_t encoded_length;
  uint32_t has_encoded_bytes;
} bb_wasm_asset_resource;

typedef struct bb_wasm_session_state {
  uint32_t handle_epoch;
  uint32_t generation;
  uint8_t *source;
  uint32_t source_length;
  bb_context *context;
  bb_program *program;
  bb_parameter_override *overrides;
  size_t override_count;
  bb_wasm_module_resource *modules;
  size_t module_count;
  bb_wasm_asset_resource *assets;
  size_t asset_count;
} bb_wasm_session_state;

static bb_wasm_session_state *bb_wasm_sessions[BB_WASM_MAX_SESSIONS];
static uint32_t bb_wasm_session_epochs[BB_WASM_MAX_SESSIONS];

static uint32_t bb_wasm_session_slot(bb_wasm_session session) {
  const uint32_t encoded_slot = session & BB_WASM_SESSION_SLOT_MASK;
  return encoded_slot == 0 || encoded_slot > BB_WASM_MAX_SESSIONS ? UINT32_MAX : encoded_slot - 1;
}

static bb_wasm_session_state *bb_wasm_state(bb_wasm_session session) {
  const uint32_t slot = bb_wasm_session_slot(session);
  bb_wasm_session_state *state;
  if (slot == UINT32_MAX) return NULL;
  state = bb_wasm_sessions[slot];
  if (state == NULL || state->handle_epoch != (session >> BB_WASM_SESSION_SLOT_BITS)) return NULL;
  return state;
}

static uint64_t bb_wasm_u64(uint32_t low, uint32_t high) {
  return (uint64_t)low | ((uint64_t)high << 32);
}

static uint32_t bb_wasm_u64_low(uint64_t value) {
  return (uint32_t)value;
}

static uint32_t bb_wasm_u64_high(uint64_t value) {
  return (uint32_t)(value >> 32);
}

static uint32_t bb_wasm_rgba(bb_rgba8 color) {
  return ((uint32_t)color.red << 24) | ((uint32_t)color.green << 16) |
         ((uint32_t)color.blue << 8) | color.alpha;
}

static void bb_wasm_store_double(uint32_t *destination, double value) {
  uint64_t bits;
  memcpy(&bits, &value, sizeof(bits));
  destination[0] = bb_wasm_u64_low(bits);
  destination[1] = bb_wasm_u64_high(bits);
}

static bb_status bb_wasm_copy(bb_string_view value, uint8_t *destination, uint32_t capacity) {
  if (value.length > UINT32_MAX || capacity < value.length || (value.length != 0 && destination == NULL))
    return BB_STATUS_INVALID_ARGUMENT;
  if (value.length != 0) memcpy(destination, value.data, value.length);
  return BB_STATUS_OK;
}

static int bb_wasm_view_equal(bb_string_view left, const uint8_t *right, uint32_t right_length) {
  return left.length == right_length &&
         (right_length == 0 || memcmp(left.data, right, right_length) == 0);
}

static uint8_t *bb_wasm_copy_bytes(const uint8_t *source, uint32_t length) {
  uint8_t *copy;
  if (length != 0 && source == NULL) return NULL;
  copy = malloc(length == 0 ? 1 : length);
  if (copy != NULL && length != 0) memcpy(copy, source, length);
  return copy;
}

static void bb_wasm_module_destroy(bb_wasm_module_resource *module) {
  free(module->source);
  free(module->identity);
  free(module->specifier);
  memset(module, 0, sizeof(*module));
}

static void bb_wasm_asset_destroy(bb_wasm_asset_resource *asset) {
  free(asset->encoded);
  free(asset->rgba);
  free(asset->content_id);
  free(asset->logical_id);
  memset(asset, 0, sizeof(*asset));
}

static void bb_wasm_clear_resources(bb_wasm_session_state *state) {
  size_t index;
  for (index = 0; index < state->module_count; index += 1) bb_wasm_module_destroy(&state->modules[index]);
  for (index = 0; index < state->asset_count; index += 1) bb_wasm_asset_destroy(&state->assets[index]);
  free(state->modules);
  free(state->assets);
  state->modules = NULL;
  state->module_count = 0;
  state->assets = NULL;
  state->asset_count = 0;
}

static bb_status bb_wasm_resolve_module(
  void *user,
  const bb_module_request *request,
  bb_resolved_module *out_module
) {
  bb_wasm_session_state *state = user;
  size_t index;
  for (index = 0; index < state->module_count; index += 1) {
    const bb_wasm_module_resource *module = &state->modules[index];
    if (!bb_wasm_view_equal(request->specifier, module->specifier, module->specifier_length)) continue;
    memset(out_module, 0, sizeof(*out_module));
    out_module->kind = BB_RESOLVED_MODULE_SOURCE;
    out_module->identity = (bb_string_view){(const char *)module->identity, module->identity_length};
    out_module->source_name = request->specifier;
    out_module->source = (bb_bytes){module->source, module->source_length};
    return BB_STATUS_OK;
  }
  return BB_STATUS_NOT_FOUND;
}

static bb_status bb_wasm_resolve_asset(
  void *user,
  const bb_asset_request *request,
  bb_resolved_asset *out_asset
) {
  bb_wasm_session_state *state = user;
  size_t index;
  for (index = 0; index < state->asset_count; index += 1) {
    const bb_wasm_asset_resource *asset = &state->assets[index];
    if (!bb_wasm_view_equal(request->logical_id, asset->logical_id, asset->logical_id_length)) continue;
    memset(out_asset, 0, sizeof(*out_asset));
    out_asset->content_id = (bb_string_view){(const char *)asset->content_id, asset->content_id_length};
    out_asset->width = asset->width;
    out_asset->height = asset->height;
    out_asset->has_encoded_bytes = asset->has_encoded_bytes;
    return BB_STATUS_OK;
  }
  return BB_STATUS_NOT_FOUND;
}

static bb_status bb_wasm_decode_asset(void *user, bb_string_view content_id, bb_const_image_view *out_image) {
  bb_wasm_session_state *state = user;
  size_t index;
  for (index = 0; index < state->asset_count; index += 1) {
    const bb_wasm_asset_resource *asset = &state->assets[index];
    if (!bb_wasm_view_equal(content_id, asset->content_id, asset->content_id_length)) continue;
    if (asset->rgba == NULL || asset->rgba_length == 0) return BB_STATUS_NOT_FOUND;
    out_image->desc = (bb_image_desc){
      asset->width,
      asset->height,
      (size_t)asset->width * 4,
      BB_PIXEL_FORMAT_RGBA8_UNORM,
      BB_COLOR_SPACE_NUMERIC_SRGB,
      BB_ALPHA_MODE_STRAIGHT,
    };
    out_image->data = asset->rgba;
    out_image->data_length = asset->rgba_length;
    return BB_STATUS_OK;
  }
  return BB_STATUS_NOT_FOUND;
}

static bb_status bb_wasm_encoded_asset(void *user, bb_string_view content_id, bb_bytes *out_bytes) {
  bb_wasm_session_state *state = user;
  size_t index;
  for (index = 0; index < state->asset_count; index += 1) {
    const bb_wasm_asset_resource *asset = &state->assets[index];
    if (!bb_wasm_view_equal(content_id, asset->content_id, asset->content_id_length)) continue;
    if (asset->encoded_length == 0) return BB_STATUS_NOT_FOUND;
    *out_bytes = (bb_bytes){asset->encoded, asset->encoded_length};
    return BB_STATUS_OK;
  }
  return BB_STATUS_NOT_FOUND;
}

static void bb_wasm_clear_compilation(bb_wasm_session_state *state) {
  bb_program_destroy(state->program);
  bb_context_destroy(state->context);
  state->program = NULL;
  state->context = NULL;
}

static bb_status bb_wasm_compile_state(bb_wasm_session_state *state) {
  bb_source_id source_id = BB_SOURCE_ID_NONE;
  bb_syntax_tree *syntax = NULL;
  bb_compile_options options;
  bb_status status;
  bb_wasm_clear_compilation(state);
  status = bb_context_create(NULL, &state->context);
  if (status == BB_STATUS_OK)
    status = bb_context_add_source(
      state->context,
      (bb_string_view){"notebook.binscript", sizeof("notebook.binscript") - 1},
      (bb_bytes){state->source, state->source_length},
      &source_id
    );
  if (status == BB_STATUS_OK) status = bb_syntax_parse(state->context, source_id, &syntax);
  bb_compile_options_init(&options);
  options.user = state;
  options.resolve_module = bb_wasm_resolve_module;
  options.resolve_asset = bb_wasm_resolve_asset;
  options.decode_asset = bb_wasm_decode_asset;
  options.encoded_asset = bb_wasm_encoded_asset;
  options.parameter_overrides = state->overrides;
  options.parameter_override_count = state->override_count;
  if (status == BB_STATUS_OK) status = bb_program_compile_with_options(state->context, syntax, &options, &state->program);
  bb_syntax_tree_destroy(syntax);
  if (status != BB_STATUS_OK) bb_wasm_clear_compilation(state);
  state->generation += 1;
  if (state->generation == 0) state->generation = 1;
  return status;
}

bb_wasm_session bb_wasm_session_create(void) {
  uint32_t index;
  for (index = 0; index < BB_WASM_MAX_SESSIONS; index += 1) {
    bb_wasm_session_state *state;
    uint32_t epoch;
    if (bb_wasm_sessions[index] != NULL) continue;
    state = calloc(1, sizeof(*state));
    if (state == NULL) return BB_WASM_SESSION_NONE;
    epoch = (bb_wasm_session_epochs[index] + 1) & BB_WASM_SESSION_EPOCH_MASK;
    if (epoch == 0) epoch = 1;
    bb_wasm_session_epochs[index] = epoch;
    state->handle_epoch = epoch;
    state->generation = 1;
    bb_wasm_sessions[index] = state;
    return (epoch << BB_WASM_SESSION_SLOT_BITS) | (index + 1);
  }
  return BB_WASM_SESSION_NONE;
}

void bb_wasm_session_destroy(bb_wasm_session session) {
  bb_wasm_session_state *state = bb_wasm_state(session);
  const uint32_t slot = bb_wasm_session_slot(session);
  size_t index;
  if (state == NULL) return;
  bb_wasm_clear_compilation(state);
  bb_wasm_clear_resources(state);
  for (index = 0; index < state->override_count; index += 1) free((void *)state->overrides[index].name.data);
  free(state->overrides);
  free(state->source);
  free(state);
  bb_wasm_sessions[slot] = NULL;
}

uint32_t bb_wasm_session_generation(bb_wasm_session session) {
  const bb_wasm_session_state *state = bb_wasm_state(session);
  return state == NULL ? 0 : state->generation;
}

void bb_wasm_session_clear_resources(bb_wasm_session session) {
  bb_wasm_session_state *state = bb_wasm_state(session);
  if (state == NULL) return;
  bb_wasm_clear_compilation(state);
  bb_wasm_clear_resources(state);
  state->generation += 1;
  if (state->generation == 0) state->generation = 1;
}

bb_status bb_wasm_session_add_module(
  bb_wasm_session session,
  const uint8_t *specifier,
  uint32_t specifier_length,
  const uint8_t *identity,
  uint32_t identity_length,
  const uint8_t *source,
  uint32_t source_length
) {
  bb_wasm_session_state *state = bb_wasm_state(session);
  bb_wasm_module_resource *replacement;
  bb_wasm_module_resource module;
  size_t index;
  if (state == NULL || specifier_length == 0 || identity_length == 0 || specifier == NULL || identity == NULL ||
      (source_length != 0 && source == NULL) ||
      bb_utf8_validate((bb_bytes){specifier, specifier_length}) != BB_STATUS_OK ||
      bb_utf8_validate((bb_bytes){identity, identity_length}) != BB_STATUS_OK ||
      bb_utf8_validate((bb_bytes){source, source_length}) != BB_STATUS_OK) return BB_STATUS_INVALID_ARGUMENT;
  for (index = 0; index < state->module_count; index += 1)
    if (bb_wasm_view_equal(
          (bb_string_view){(const char *)specifier, specifier_length},
          state->modules[index].specifier,
          state->modules[index].specifier_length
        )) return BB_STATUS_INVALID_ARGUMENT;
  if (state->module_count >= BB_WASM_MAX_MODULES) return BB_STATUS_LIMIT_EXCEEDED;
  memset(&module, 0, sizeof(module));
  module.specifier = bb_wasm_copy_bytes(specifier, specifier_length);
  module.identity = bb_wasm_copy_bytes(identity, identity_length);
  module.source = bb_wasm_copy_bytes(source, source_length);
  if (module.specifier == NULL || module.identity == NULL || module.source == NULL) {
    bb_wasm_module_destroy(&module);
    return BB_STATUS_OUT_OF_MEMORY;
  }
  module.specifier_length = specifier_length;
  module.identity_length = identity_length;
  module.source_length = source_length;
  replacement = realloc(state->modules, (state->module_count + 1) * sizeof(*state->modules));
  if (replacement == NULL) {
    bb_wasm_module_destroy(&module);
    return BB_STATUS_OUT_OF_MEMORY;
  }
  state->modules = replacement;
  state->modules[state->module_count++] = module;
  return BB_STATUS_OK;
}

bb_status bb_wasm_session_add_asset_rgba(
  bb_wasm_session session,
  const uint8_t *logical_id,
  uint32_t logical_id_length,
  const uint8_t *content_id,
  uint32_t content_id_length,
  uint32_t width,
  uint32_t height,
  const uint8_t *rgba,
  uint32_t rgba_length,
  const uint8_t *encoded,
  uint32_t encoded_length
) {
  bb_wasm_session_state *state = bb_wasm_state(session);
  bb_wasm_asset_resource *replacement;
  bb_wasm_asset_resource asset;
  const uint64_t required = (uint64_t)width * height * 4;
  size_t index;
  if (state == NULL || logical_id_length == 0 || content_id_length == 0 || logical_id == NULL ||
      content_id == NULL || width == 0 || height == 0 || required != rgba_length || rgba == NULL ||
      (encoded_length != 0 && encoded == NULL) ||
      bb_utf8_validate((bb_bytes){logical_id, logical_id_length}) != BB_STATUS_OK ||
      bb_utf8_validate((bb_bytes){content_id, content_id_length}) != BB_STATUS_OK) return BB_STATUS_INVALID_ARGUMENT;
  for (index = 0; index < state->asset_count; index += 1)
    if (bb_wasm_view_equal(
          (bb_string_view){(const char *)logical_id, logical_id_length},
          state->assets[index].logical_id,
          state->assets[index].logical_id_length
        )) return BB_STATUS_INVALID_ARGUMENT;
  if (state->asset_count >= BB_WASM_MAX_ASSETS) return BB_STATUS_LIMIT_EXCEEDED;
  memset(&asset, 0, sizeof(asset));
  asset.logical_id = bb_wasm_copy_bytes(logical_id, logical_id_length);
  asset.content_id = bb_wasm_copy_bytes(content_id, content_id_length);
  asset.rgba = bb_wasm_copy_bytes(rgba, rgba_length);
  asset.encoded = bb_wasm_copy_bytes(encoded, encoded_length);
  if (asset.logical_id == NULL || asset.content_id == NULL || asset.rgba == NULL || asset.encoded == NULL) {
    bb_wasm_asset_destroy(&asset);
    return BB_STATUS_OUT_OF_MEMORY;
  }
  asset.logical_id_length = logical_id_length;
  asset.content_id_length = content_id_length;
  asset.width = width;
  asset.height = height;
  asset.rgba_length = rgba_length;
  asset.encoded_length = encoded_length;
  asset.has_encoded_bytes = encoded_length != 0;
  replacement = realloc(state->assets, (state->asset_count + 1) * sizeof(*state->assets));
  if (replacement == NULL) {
    bb_wasm_asset_destroy(&asset);
    return BB_STATUS_OUT_OF_MEMORY;
  }
  state->assets = replacement;
  state->assets[state->asset_count++] = asset;
  return BB_STATUS_OK;
}

bb_status bb_wasm_session_add_asset_metadata(
  bb_wasm_session session,
  const uint8_t *logical_id,
  uint32_t logical_id_length,
  const uint8_t *content_id,
  uint32_t content_id_length,
  uint32_t width,
  uint32_t height,
  uint32_t has_encoded_bytes
) {
  bb_wasm_session_state *state = bb_wasm_state(session);
  bb_wasm_asset_resource *replacement;
  bb_wasm_asset_resource asset;
  size_t index;
  if (state == NULL || logical_id_length == 0 || content_id_length == 0 || logical_id == NULL ||
      content_id == NULL || width == 0 || height == 0 || has_encoded_bytes > 1 ||
      bb_utf8_validate((bb_bytes){logical_id, logical_id_length}) != BB_STATUS_OK ||
      bb_utf8_validate((bb_bytes){content_id, content_id_length}) != BB_STATUS_OK) return BB_STATUS_INVALID_ARGUMENT;
  for (index = 0; index < state->asset_count; index += 1)
    if (bb_wasm_view_equal(
          (bb_string_view){(const char *)logical_id, logical_id_length},
          state->assets[index].logical_id,
          state->assets[index].logical_id_length
        )) return BB_STATUS_INVALID_ARGUMENT;
  if (state->asset_count >= BB_WASM_MAX_ASSETS) return BB_STATUS_LIMIT_EXCEEDED;
  memset(&asset, 0, sizeof(asset));
  asset.logical_id = bb_wasm_copy_bytes(logical_id, logical_id_length);
  asset.content_id = bb_wasm_copy_bytes(content_id, content_id_length);
  if (asset.logical_id == NULL || asset.content_id == NULL) {
    bb_wasm_asset_destroy(&asset);
    return BB_STATUS_OUT_OF_MEMORY;
  }
  asset.logical_id_length = logical_id_length;
  asset.content_id_length = content_id_length;
  asset.width = width;
  asset.height = height;
  asset.has_encoded_bytes = has_encoded_bytes;
  replacement = realloc(state->assets, (state->asset_count + 1) * sizeof(*state->assets));
  if (replacement == NULL) {
    bb_wasm_asset_destroy(&asset);
    return BB_STATUS_OUT_OF_MEMORY;
  }
  state->assets = replacement;
  state->assets[state->asset_count++] = asset;
  return BB_STATUS_OK;
}

bb_status bb_wasm_session_hydrate_asset(
  bb_wasm_session session,
  const uint8_t *logical_id,
  uint32_t logical_id_length,
  const uint8_t *rgba,
  uint32_t rgba_length,
  const uint8_t *encoded,
  uint32_t encoded_length
) {
  bb_wasm_session_state *state = bb_wasm_state(session);
  size_t index;
  if (state == NULL || logical_id == NULL || logical_id_length == 0 || rgba == NULL ||
      (encoded_length != 0 && encoded == NULL) ||
      bb_utf8_validate((bb_bytes){logical_id, logical_id_length}) != BB_STATUS_OK) return BB_STATUS_INVALID_ARGUMENT;
  for (index = 0; index < state->asset_count; index += 1) {
    bb_wasm_asset_resource *asset = &state->assets[index];
    uint8_t *rgba_copy;
    uint8_t *encoded_copy;
    const uint64_t required = (uint64_t)asset->width * asset->height * 4;
    if (!bb_wasm_view_equal(
          (bb_string_view){(const char *)logical_id, logical_id_length},
          asset->logical_id,
          asset->logical_id_length
        )) continue;
    if (required != rgba_length) return BB_STATUS_INVALID_ARGUMENT;
    rgba_copy = bb_wasm_copy_bytes(rgba, rgba_length);
    encoded_copy = bb_wasm_copy_bytes(encoded, encoded_length);
    if (rgba_copy == NULL || encoded_copy == NULL) {
      free(encoded_copy);
      free(rgba_copy);
      return BB_STATUS_OUT_OF_MEMORY;
    }
    free(asset->rgba);
    free(asset->encoded);
    asset->rgba = rgba_copy;
    asset->rgba_length = rgba_length;
    asset->encoded = encoded_copy;
    asset->encoded_length = encoded_length;
    asset->has_encoded_bytes = encoded_length != 0;
    return BB_STATUS_OK;
  }
  return BB_STATUS_NOT_FOUND;
}

bb_status bb_wasm_session_compile(
  bb_wasm_session session,
  const uint8_t *source,
  uint32_t source_length
) {
  bb_wasm_session_state *state = bb_wasm_state(session);
  uint8_t *copy;
  size_t index;
  if (state == NULL || (source_length != 0 && source == NULL)) return BB_STATUS_INVALID_ARGUMENT;
  copy = malloc(source_length == 0 ? 1 : source_length);
  if (copy == NULL) return BB_STATUS_OUT_OF_MEMORY;
  if (source_length != 0) memcpy(copy, source, source_length);
  bb_wasm_clear_compilation(state);
  free(state->source);
  state->source = copy;
  state->source_length = source_length;
  for (index = 0; index < state->override_count; index += 1) free((void *)state->overrides[index].name.data);
  free(state->overrides);
  state->overrides = NULL;
  state->override_count = 0;
  return bb_wasm_compile_state(state);
}

uint32_t bb_wasm_diagnostic_count(bb_wasm_session session) {
  const bb_wasm_session_state *state = bb_wasm_state(session);
  const size_t count = state == NULL || state->program == NULL ? 0 : bb_program_diagnostic_count(state->program);
  return count > UINT32_MAX ? UINT32_MAX : (uint32_t)count;
}

bb_status bb_wasm_diagnostic_get(
  bb_wasm_session session,
  uint32_t diagnostic_index,
  bb_wasm_diagnostic_info *out_info
) {
  const bb_wasm_session_state *state = bb_wasm_state(session);
  bb_diagnostic diagnostic;
  bb_status status;
  if (state == NULL || state->program == NULL || out_info == NULL ||
      (out_info->struct_size != 0 && out_info->struct_size != sizeof(*out_info))) return BB_STATUS_INVALID_ARGUMENT;
  status = bb_program_diagnostic(state->program, diagnostic_index, &diagnostic);
  if (status != BB_STATUS_OK) return status;
  if (diagnostic.message.length > UINT32_MAX) return BB_STATUS_OVERFLOW;
  *out_info = (bb_wasm_diagnostic_info){
    sizeof(*out_info),
    diagnostic.severity,
    diagnostic.code,
    diagnostic.primary_span.source_id,
    diagnostic.primary_span.byte_start,
    diagnostic.primary_span.byte_end,
    (uint32_t)diagnostic.message.length,
  };
  return BB_STATUS_OK;
}

bb_status bb_wasm_diagnostic_copy_message(
  bb_wasm_session session,
  uint32_t diagnostic_index,
  uint8_t *destination,
  uint32_t capacity
) {
  const bb_wasm_session_state *state = bb_wasm_state(session);
  bb_diagnostic diagnostic;
  bb_status status;
  if (state == NULL || state->program == NULL) return BB_STATUS_INVALID_ARGUMENT;
  status = bb_program_diagnostic(state->program, diagnostic_index, &diagnostic);
  return status == BB_STATUS_OK ? bb_wasm_copy(diagnostic.message, destination, capacity) : status;
}

bb_status bb_wasm_trace_at(
  bb_wasm_session session,
  uint32_t byte_offset,
  bb_wasm_trace_info *out_info
) {
  const bb_wasm_session_state *state = bb_wasm_state(session);
  bb_semantic_trace trace;
  bb_status status;
  if (state == NULL || state->program == NULL || out_info == NULL ||
      (out_info->struct_size != 0 && out_info->struct_size != sizeof(*out_info))) return BB_STATUS_INVALID_ARGUMENT;
  status = bb_program_trace_at(state->program, 1, byte_offset, &trace);
  if (status != BB_STATUS_OK) return status;
  *out_info = (bb_wasm_trace_info){
    sizeof(*out_info),
    trace.type,
    trace.image,
    bb_wasm_u64_low(trace.output_index),
    bb_wasm_u64_high(trace.output_index),
    trace.span.source_id,
    trace.span.byte_start,
    trace.span.byte_end,
  };
  return BB_STATUS_OK;
}

uint32_t bb_wasm_parameter_count(bb_wasm_session session) {
  const bb_wasm_session_state *state = bb_wasm_state(session);
  const size_t count = state == NULL || state->program == NULL ? 0 : bb_program_parameter_count(state->program);
  return count > UINT32_MAX ? UINT32_MAX : (uint32_t)count;
}

bb_status bb_wasm_parameter_get(
  bb_wasm_session session,
  uint32_t parameter_index,
  bb_wasm_parameter_info *out_info
) {
  const bb_wasm_session_state *state = bb_wasm_state(session);
  bb_program_parameter_info parameter;
  bb_status status;
  if (state == NULL || state->program == NULL || out_info == NULL ||
      (out_info->struct_size != 0 && out_info->struct_size != sizeof(*out_info))) return BB_STATUS_INVALID_ARGUMENT;
  status = bb_program_parameter(state->program, parameter_index, &parameter);
  if (status != BB_STATUS_OK) return status;
  if (parameter.name.length > UINT32_MAX) return BB_STATUS_OVERFLOW;
  *out_info = (bb_wasm_parameter_info){
    sizeof(*out_info),
    parameter.type,
    (uint32_t)parameter.name.length,
    parameter.span.source_id,
    parameter.span.byte_start,
    parameter.span.byte_end,
  };
  return BB_STATUS_OK;
}

bb_status bb_wasm_parameter_copy_name(
  bb_wasm_session session,
  uint32_t parameter_index,
  uint8_t *destination,
  uint32_t capacity
) {
  const bb_wasm_session_state *state = bb_wasm_state(session);
  bb_program_parameter_info parameter;
  bb_status status;
  if (state == NULL || state->program == NULL) return BB_STATUS_INVALID_ARGUMENT;
  status = bb_program_parameter(state->program, parameter_index, &parameter);
  return status == BB_STATUS_OK ? bb_wasm_copy(parameter.name, destination, capacity) : status;
}

static bb_status bb_wasm_set_override(
  bb_wasm_session_state *state,
  uint32_t parameter_index,
  bb_parameter_override value
) {
  bb_program_parameter_info parameter;
  bb_parameter_override *replacement;
  char *name;
  size_t index;
  bb_status status;
  if (state == NULL || state->program == NULL || state->source == NULL) return BB_STATUS_INVALID_ARGUMENT;
  status = bb_program_parameter(state->program, parameter_index, &parameter);
  if (status != BB_STATUS_OK) return status;
  if (parameter.type != value.type) return BB_STATUS_INVALID_ARGUMENT;
  for (index = 0; index < state->override_count; index += 1) {
    if (state->overrides[index].name.length == parameter.name.length &&
        memcmp(state->overrides[index].name.data, parameter.name.data, parameter.name.length) == 0) {
      value.name = state->overrides[index].name;
      state->overrides[index] = value;
      return bb_wasm_compile_state(state);
    }
  }
  name = malloc(parameter.name.length == 0 ? 1 : parameter.name.length);
  if (name == NULL) return BB_STATUS_OUT_OF_MEMORY;
  if (parameter.name.length != 0) memcpy(name, parameter.name.data, parameter.name.length);
  if (state->override_count == SIZE_MAX / sizeof(*state->overrides)) {
    free(name);
    return BB_STATUS_OVERFLOW;
  }
  replacement = realloc(state->overrides, (state->override_count + 1) * sizeof(*state->overrides));
  if (replacement == NULL) {
    free(name);
    return BB_STATUS_OUT_OF_MEMORY;
  }
  state->overrides = replacement;
  value.name = (bb_string_view){name, parameter.name.length};
  state->overrides[state->override_count++] = value;
  return bb_wasm_compile_state(state);
}

bb_status bb_wasm_parameter_set_bool(
  bb_wasm_session session,
  uint32_t parameter_index,
  uint32_t value
) {
  bb_parameter_override override;
  memset(&override, 0, sizeof(override));
  override.type = BB_SEMANTIC_BOOL;
  override.value.boolean = !!value;
  return bb_wasm_set_override(bb_wasm_state(session), parameter_index, override);
}

bb_status bb_wasm_parameter_set_integer(
  bb_wasm_session session,
  uint32_t parameter_index,
  int32_t value_low,
  int32_t value_high
) {
  bb_parameter_override override;
  const uint64_t bits = (uint32_t)value_low | ((uint64_t)(uint32_t)value_high << 32);
  memset(&override, 0, sizeof(override));
  override.type = BB_SEMANTIC_INTEGER;
  memcpy(&override.value.integer, &bits, sizeof(bits));
  return bb_wasm_set_override(bb_wasm_state(session), parameter_index, override);
}

bb_status bb_wasm_parameter_set_number(
  bb_wasm_session session,
  uint32_t parameter_index,
  double value
) {
  bb_wasm_session_state *state = bb_wasm_state(session);
  bb_program_parameter_info parameter;
  bb_parameter_override override;
  bb_status status;
  if (!isfinite(value) || state == NULL || state->program == NULL) return BB_STATUS_INVALID_ARGUMENT;
  status = bb_program_parameter(state->program, parameter_index, &parameter);
  if (status != BB_STATUS_OK) return status;
  if (parameter.type != BB_SEMANTIC_NUMBER && parameter.type != BB_SEMANTIC_DEGREES &&
      parameter.type != BB_SEMANTIC_PERCENTAGE) return BB_STATUS_INVALID_ARGUMENT;
  memset(&override, 0, sizeof(override));
  override.type = parameter.type;
  override.value.number = value;
  return bb_wasm_set_override(state, parameter_index, override);
}

bb_status bb_wasm_parameter_set_color(
  bb_wasm_session session,
  uint32_t parameter_index,
  uint32_t rgba
) {
  bb_parameter_override override;
  memset(&override, 0, sizeof(override));
  override.type = BB_SEMANTIC_COLOR;
  override.value.color = (bb_rgba8){
    (uint8_t)(rgba >> 24),
    (uint8_t)(rgba >> 16),
    (uint8_t)(rgba >> 8),
    (uint8_t)rgba,
  };
  return bb_wasm_set_override(bb_wasm_state(session), parameter_index, override);
}

uint32_t bb_wasm_output_count(bb_wasm_session session) {
  const bb_wasm_session_state *state = bb_wasm_state(session);
  const size_t count = state == NULL || state->program == NULL ? 0 : bb_program_output_count(state->program);
  return count > UINT32_MAX ? UINT32_MAX : (uint32_t)count;
}

bb_status bb_wasm_output_get(
  bb_wasm_session session,
  uint32_t output_index,
  bb_wasm_output_info *out_info
) {
  const bb_wasm_session_state *state = bb_wasm_state(session);
  bb_program_output_info output;
  bb_status status;
  if (state == NULL || state->program == NULL || out_info == NULL ||
      (out_info->struct_size != 0 && out_info->struct_size != sizeof(*out_info))) return BB_STATUS_INVALID_ARGUMENT;
  status = bb_program_output(state->program, output_index, &output);
  if (status != BB_STATUS_OK) return status;
  *out_info = (bb_wasm_output_info){
    sizeof(*out_info),
    output.item_type,
    bb_wasm_u64_low(output.cardinality),
    bb_wasm_u64_high(output.cardinality),
    output.span.source_id,
    output.span.byte_start,
    output.span.byte_end,
  };
  return BB_STATUS_OK;
}

static bb_status bb_wasm_artifact(
  const bb_wasm_session_state *state,
  uint32_t output_index,
  uint32_t item_index_low,
  uint32_t item_index_high,
  bb_artifact_value *out_artifact
) {
  if (state == NULL || state->program == NULL) return BB_STATUS_INVALID_ARGUMENT;
  return bb_program_output_artifact(
    state->program,
    output_index,
    bb_wasm_u64(item_index_low, item_index_high),
    out_artifact
  );
}

bb_status bb_wasm_artifact_get(
  bb_wasm_session session,
  uint32_t output_index,
  uint32_t item_index_low,
  uint32_t item_index_high,
  bb_wasm_artifact_info *out_info
) {
  const bb_wasm_session_state *state = bb_wasm_state(session);
  bb_artifact_value artifact;
  bb_status status;
  if (out_info == NULL || (out_info->struct_size != 0 && out_info->struct_size != sizeof(*out_info)))
    return BB_STATUS_INVALID_ARGUMENT;
  status = bb_wasm_artifact(state, output_index, item_index_low, item_index_high, &artifact);
  if (status != BB_STATUS_OK) return status;
  if (artifact.key.length > UINT32_MAX || artifact.path.length > UINT32_MAX ||
      artifact.alias_target.length > UINT32_MAX) return BB_STATUS_OVERFLOW;
  *out_info = (bb_wasm_artifact_info){
    sizeof(*out_info),
    artifact.image,
    (uint32_t)artifact.key.length,
    (uint32_t)artifact.path.length,
    artifact.alias_identity,
    (uint32_t)artifact.alias_target.length,
    artifact.provenance.source_id,
    artifact.provenance.byte_start,
    artifact.provenance.byte_end,
  };
  return BB_STATUS_OK;
}

bb_status bb_wasm_artifact_copy_key(
  bb_wasm_session session,
  uint32_t output_index,
  uint32_t item_index_low,
  uint32_t item_index_high,
  uint8_t *destination,
  uint32_t capacity
) {
  bb_artifact_value artifact;
  bb_status status = bb_wasm_artifact(
    bb_wasm_state(session), output_index, item_index_low, item_index_high, &artifact
  );
  return status == BB_STATUS_OK ? bb_wasm_copy(artifact.key, destination, capacity) : status;
}

bb_status bb_wasm_artifact_copy_path(
  bb_wasm_session session,
  uint32_t output_index,
  uint32_t item_index_low,
  uint32_t item_index_high,
  uint8_t *destination,
  uint32_t capacity
) {
  bb_artifact_value artifact;
  bb_status status = bb_wasm_artifact(
    bb_wasm_state(session), output_index, item_index_low, item_index_high, &artifact
  );
  return status == BB_STATUS_OK ? bb_wasm_copy(artifact.path, destination, capacity) : status;
}

bb_status bb_wasm_graph_node_get(
  bb_wasm_session session,
  uint32_t node,
  bb_wasm_graph_node_info *out_info
) {
  const bb_wasm_session_state *state = bb_wasm_state(session);
  bb_image_node_info info = {0};
  bb_status status;
  if (state == NULL || state->program == NULL || out_info == NULL ||
      (out_info->struct_size != 0 && out_info->struct_size != sizeof(*out_info))) return BB_STATUS_INVALID_ARGUMENT;
  info.struct_size = sizeof(info);
  status = bb_image_graph_node_info(bb_program_image_graph(state->program), node, &info);
  if (status != BB_STATUS_OK) return status;
  memset(out_info, 0, sizeof(*out_info));
  out_info->struct_size = sizeof(*out_info);
  out_info->kind = info.kind;
  out_info->width = info.width;
  out_info->height = info.height;
  out_info->input_count = (uint32_t)info.input_count;
  if (info.input_count > 0) out_info->inputs[0] = info.inputs[0];
  if (info.input_count > 1) out_info->inputs[1] = info.inputs[1];
  switch (info.kind) {
    case BB_IMAGE_NODE_FILL: out_info->options[0] = bb_wasm_rgba(info.options.fill_color); break;
    case BB_IMAGE_NODE_ALPHA_FIELD:
      out_info->options[0] = info.options.alpha_field.metric;
      out_info->options[1] = info.options.alpha_field.direction;
      out_info->options[2] = info.options.alpha_field.easing;
      out_info->options[3] = bb_wasm_rgba(info.options.alpha_field.color);
      out_info->options[4] = info.options.alpha_field.legacy_radial_rounding;
      out_info->stop_count = (uint32_t)info.options.alpha_field.level_count;
      bb_wasm_store_double(&out_info->scalar_bits[0], info.options.alpha_field.center_x);
      bb_wasm_store_double(&out_info->scalar_bits[2], info.options.alpha_field.center_y);
      bb_wasm_store_double(&out_info->scalar_bits[4], info.options.alpha_field.radius);
      break;
    case BB_IMAGE_NODE_PRESET_GRADIENT:
      out_info->options[0] = info.options.preset_gradient.preset;
      out_info->options[1] = (uint32_t)info.options.preset_gradient.quarter_turns;
      out_info->options[2] = bb_wasm_rgba(info.options.preset_gradient.color);
      break;
    case BB_IMAGE_NODE_LINEAR_GRADIENT:
      out_info->options[0] = info.options.linear_gradient.has_explicit_extent;
      out_info->options[1] = info.options.linear_gradient.easing;
      out_info->stop_count = (uint32_t)info.options.linear_gradient.stop_count;
      bb_wasm_store_double(&out_info->scalar_bits[0], info.options.linear_gradient.angle_degrees);
      bb_wasm_store_double(&out_info->scalar_bits[2], info.options.linear_gradient.extent);
      break;
    case BB_IMAGE_NODE_ELLIPTICAL_GRADIENT:
      out_info->options[0] = info.options.elliptical_gradient.easing;
      out_info->options[1] = info.options.elliptical_gradient.legacy_radial_rounding;
      out_info->stop_count = (uint32_t)info.options.elliptical_gradient.stop_count;
      bb_wasm_store_double(&out_info->scalar_bits[0], info.options.elliptical_gradient.center_x);
      bb_wasm_store_double(&out_info->scalar_bits[2], info.options.elliptical_gradient.center_y);
      bb_wasm_store_double(&out_info->scalar_bits[4], info.options.elliptical_gradient.radius_x);
      bb_wasm_store_double(&out_info->scalar_bits[6], info.options.elliptical_gradient.radius_y);
      bb_wasm_store_double(&out_info->scalar_bits[8], info.options.elliptical_gradient.rotation_radians);
      break;
    case BB_IMAGE_NODE_CROP:
    case BB_IMAGE_NODE_CANVAS:
      out_info->options[0] = (uint32_t)info.options.placement.x;
      out_info->options[1] = (uint32_t)info.options.placement.y;
      break;
    case BB_IMAGE_NODE_ROTATE:
      out_info->options[0] = (uint32_t)info.options.quarter_turns;
      break;
    case BB_IMAGE_NODE_OPACITY:
      bb_wasm_store_double(&out_info->scalar_bits[0], info.options.opacity);
      break;
    case BB_IMAGE_NODE_COMPOSITE:
      out_info->options[0] = (uint32_t)info.options.composite.offset_x;
      out_info->options[1] = (uint32_t)info.options.composite.offset_y;
      bb_wasm_store_double(&out_info->scalar_bits[0], info.options.composite.opacity);
      break;
    case BB_IMAGE_NODE_MASK: out_info->options[0] = info.options.mask_mode; break;
    case BB_IMAGE_NODE_ASSET:
      if (info.options.asset.content_id.length > UINT32_MAX) return BB_STATUS_OVERFLOW;
      out_info->asset_content_id_length = (uint32_t)info.options.asset.content_id.length;
      break;
    case BB_IMAGE_NODE_SET_VISIBLE_RGB:
    case BB_IMAGE_NODE_TINT_CHROMA: out_info->options[0] = bb_wasm_rgba(info.options.color); break;
    case BB_IMAGE_NODE_REMAP_TWO_COLOR:
      out_info->options[0] = bb_wasm_rgba(info.options.remap.source_foreground);
      out_info->options[1] = bb_wasm_rgba(info.options.remap.source_background);
      out_info->options[2] = bb_wasm_rgba(info.options.remap.foreground);
      out_info->options[3] = bb_wasm_rgba(info.options.remap.background);
      break;
    case BB_IMAGE_NODE_SHIFT_RGB:
      out_info->options[0] = bb_wasm_rgba(info.options.shift_rgb.source_base);
      out_info->options[1] = bb_wasm_rgba(info.options.shift_rgb.target_base);
      break;
    case BB_IMAGE_NODE_RESIZE:
    case BB_IMAGE_NODE_INVERT_ALPHA: break;
    default: return BB_STATUS_UNSUPPORTED;
  }
  return BB_STATUS_OK;
}

bb_status bb_wasm_graph_asset_copy_content_id(
  bb_wasm_session session,
  uint32_t node,
  uint8_t *destination,
  uint32_t capacity
) {
  const bb_wasm_session_state *state = bb_wasm_state(session);
  bb_graph_asset_desc asset;
  bb_status status;
  if (state == NULL || state->program == NULL) return BB_STATUS_INVALID_ARGUMENT;
  status = bb_image_graph_asset(bb_program_image_graph(state->program), node, &asset);
  return status == BB_STATUS_OK ? bb_wasm_copy(asset.content_id, destination, capacity) : status;
}

bb_status bb_wasm_graph_gradient_stop_get(
  bb_wasm_session session,
  uint32_t node,
  uint32_t stop_index,
  bb_wasm_gradient_stop_info *out_info
) {
  const bb_wasm_session_state *state = bb_wasm_state(session);
  bb_image_node_info info = {0};
  const bb_gradient_stop *stops;
  size_t stop_count;
  uint64_t offset_bits;
  bb_status status;
  if (state == NULL || state->program == NULL || out_info == NULL ||
      (out_info->struct_size != 0 && out_info->struct_size != sizeof(*out_info))) return BB_STATUS_INVALID_ARGUMENT;
  info.struct_size = sizeof(info);
  status = bb_image_graph_node_info(bb_program_image_graph(state->program), node, &info);
  if (status != BB_STATUS_OK) return status;
  if (info.kind == BB_IMAGE_NODE_LINEAR_GRADIENT) {
    stops = info.options.linear_gradient.stops;
    stop_count = info.options.linear_gradient.stop_count;
  } else if (info.kind == BB_IMAGE_NODE_ELLIPTICAL_GRADIENT) {
    stops = info.options.elliptical_gradient.stops;
    stop_count = info.options.elliptical_gradient.stop_count;
  } else return BB_STATUS_INVALID_ARGUMENT;
  if (stop_index >= stop_count) return BB_STATUS_NOT_FOUND;
  memcpy(&offset_bits, &stops[stop_index].offset, sizeof(offset_bits));
  *out_info = (bb_wasm_gradient_stop_info){
    sizeof(*out_info),
    bb_wasm_u64_low(offset_bits),
    bb_wasm_u64_high(offset_bits),
    bb_wasm_rgba(stops[stop_index].color),
    stops[stop_index].easing,
    stops[stop_index].has_easing,
  };
  return BB_STATUS_OK;
}

bb_status bb_wasm_render_output_rgba(
  bb_wasm_session session,
  uint32_t output_index,
  uint32_t item_index_low,
  uint32_t item_index_high,
  uint8_t *destination,
  uint32_t capacity,
  bb_wasm_render_info *out_info
) {
  bb_wasm_session_state *state = bb_wasm_state(session);
  bb_surface *surface = NULL;
  bb_const_image_view view;
  bb_status status;
  if (state == NULL || state->program == NULL || out_info == NULL ||
      (out_info->struct_size != 0 && out_info->struct_size != sizeof(*out_info))) return BB_STATUS_INVALID_ARGUMENT;
  status = bb_program_render_output(
    state->program,
    output_index,
    bb_wasm_u64(item_index_low, item_index_high),
    &surface
  );
  if (status == BB_STATUS_OK) status = bb_surface_get_const_view(surface, &view);
  if (status == BB_STATUS_OK && view.data_length > UINT32_MAX) status = BB_STATUS_OVERFLOW;
  if (status == BB_STATUS_OK) {
    *out_info = (bb_wasm_render_info){
      sizeof(*out_info),
      view.desc.width,
      view.desc.height,
      (uint32_t)view.data_length,
    };
    if ((destination == NULL && capacity != 0) ||
        (destination != NULL && capacity < view.data_length)) status = BB_STATUS_INVALID_ARGUMENT;
    else if (destination != NULL && view.data_length != 0) memcpy(destination, view.data, view.data_length);
  }
  bb_surface_destroy(surface);
  return status;
}
