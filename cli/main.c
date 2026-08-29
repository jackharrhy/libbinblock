#include "png.h"
#include "inventory.h"
#include "sha256.h"

#include <binblock/program.h>
#include <binblock/module.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct bb_cli_file_record {
  char *logical;
  char *path;
  char content_id[32];
  uint8_t *bytes;
  size_t length;
  uint32_t width;
  uint32_t height;
  bb_cli_png decoded;
} bb_cli_file_record;

typedef struct bb_cli_host {
  char *base_directory;
  bb_cli_file_record *assets;
  size_t asset_count;
  size_t asset_capacity;
  bb_cli_file_record *modules;
  size_t module_count;
  size_t module_capacity;
} bb_cli_host;

typedef struct bb_cli_loaded {
  bb_context *context;
  bb_program *program;
  bb_cli_host host;
  size_t error_count;
} bb_cli_loaded;

typedef enum bb_cli_compare_mode {
  BB_CLI_COMPARE_RGBA,
  BB_CLI_COMPARE_ALPHA,
  BB_CLI_COMPARE_BYTES
} bb_cli_compare_mode;

typedef struct bb_cli_contract {
  char *path;
  char *equivalence;
  bb_cli_compare_mode mode;
  uint32_t max_channel_error;
} bb_cli_contract;

typedef struct bb_cli_contract_set {
  bb_cli_contract *items;
  size_t count;
  size_t capacity;
} bb_cli_contract_set;

static volatile sig_atomic_t bb_cli_cancelled = 0;

static void bb_cli_signal(int signal_number) {
  (void)signal_number;
  bb_cli_cancelled = 1;
}

static uint32_t bb_cli_should_cancel(void *user) {
  (void)user;
  return bb_cli_cancelled != 0;
}

static bb_status bb_cli_render_output(
  bb_program *program,
  size_t output_index,
  uint64_t item_index,
  bb_surface **out_surface
) {
  bb_render_options options;
  bb_render_options_init(&options);
  options.should_cancel = bb_cli_should_cancel;
  return bb_program_render_output_with_options(program, output_index, item_index, &options, out_surface);
}

static int bb_cli_view_is(bb_string_view value, const char *expected) {
  const size_t length = strlen(expected);
  return value.length == length && (length == 0 || memcmp(value.data, expected, length) == 0);
}

static char *bb_cli_copy_view(bb_string_view value) {
  char *copy = malloc(value.length + 1);
  if (copy == NULL) return NULL;
  if (value.length != 0) memcpy(copy, value.data, value.length);
  copy[value.length] = '\0';
  return copy;
}

static char *bb_cli_copy_string(const char *value) {
  return bb_cli_copy_view((bb_string_view){value, strlen(value)});
}

static void bb_cli_contracts_destroy(bb_cli_contract_set *contracts) {
  size_t index;
  if (contracts == NULL) return;
  for (index = 0; index < contracts->count; index += 1) {
    free(contracts->items[index].equivalence);
    free(contracts->items[index].path);
  }
  free(contracts->items);
  memset(contracts, 0, sizeof(*contracts));
}

static int bb_cli_contracts_push(
  bb_cli_contract_set *contracts,
  const char *path,
  const char *equivalence,
  uint32_t max_channel_error
) {
  bb_cli_contract *replacement;
  bb_cli_contract *item;
  if (contracts->count == contracts->capacity) {
    const size_t capacity = contracts->capacity == 0 ? 64 : contracts->capacity * 2;
    if (capacity < contracts->capacity || capacity > SIZE_MAX / sizeof(*contracts->items)) return 0;
    replacement = realloc(contracts->items, capacity * sizeof(*contracts->items));
    if (replacement == NULL) return 0;
    contracts->items = replacement;
    contracts->capacity = capacity;
  }
  item = &contracts->items[contracts->count];
  memset(item, 0, sizeof(*item));
  item->path = bb_cli_copy_string(path);
  item->equivalence = bb_cli_copy_string(equivalence);
  if (item->path == NULL || item->equivalence == NULL) {
    free(item->equivalence);
    free(item->path);
    memset(item, 0, sizeof(*item));
    return 0;
  }
  item->mode = strcmp(equivalence, "alpha-only-exact") == 0 ? BB_CLI_COMPARE_ALPHA
               : strcmp(equivalence, "byte-alias") == 0     ? BB_CLI_COMPARE_BYTES
                                                               : BB_CLI_COMPARE_RGBA;
  item->max_channel_error = max_channel_error;
  contracts->count += 1;
  return 1;
}

static int bb_cli_contracts_load(const char *path, bb_cli_contract_set *out_contracts) {
  char line[2048];
  FILE *file;
  memset(out_contracts, 0, sizeof(*out_contracts));
  file = fopen(path, "rb");
  if (file == NULL) return 0;
  while (fgets(line, sizeof(line), file) != NULL) {
    char *first;
    char *second;
    char *third;
    char *fourth;
    char *end;
    unsigned long long declared_index;
    unsigned long declared_max_error = 0;
    const size_t length = strlen(line);
    if (length == 0 || line[0] == '#') continue;
    if (line[length - 1] != '\n' && !feof(file)) goto fail;
    line[strcspn(line, "\r\n")] = '\0';
    first = strchr(line, '\t');
    if (first == NULL) goto fail;
    *first++ = '\0';
    second = strchr(first, '\t');
    if (second == NULL) goto fail;
    *second++ = '\0';
    third = strchr(second, '\t');
    if (third != NULL) {
      *third++ = '\0';
      fourth = strchr(third, '\t');
      if (fourth != NULL) *fourth = '\0';
      errno = 0;
      declared_max_error = strtoul(third, &end, 10);
      if (errno != 0 || end == third || *end != '\0' || declared_max_error > UINT8_MAX) goto fail;
    }
    errno = 0;
    declared_index = strtoull(line, &end, 10);
    if (errno != 0 || end == line || *end != '\0' || declared_index != out_contracts->count ||
        first[0] == '\0' || second[0] == '\0' ||
        !bb_cli_contracts_push(out_contracts, first, second, (uint32_t)declared_max_error)) goto fail;
  }
  if (ferror(file) || out_contracts->count == 0) goto fail;
  fclose(file);
  return 1;

fail:
  fclose(file);
  bb_cli_contracts_destroy(out_contracts);
  return 0;
}

static int bb_cli_read_file(const char *path, uint8_t **out_bytes, size_t *out_length) {
  FILE *file;
  long length;
  uint8_t *bytes;
  *out_bytes = NULL;
  *out_length = 0;
  file = fopen(path, "rb");
  if (file == NULL) return 0;
  if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return 0;
  }
  bytes = malloc((size_t)length == 0 ? 1 : (size_t)length);
  if (bytes == NULL || ((size_t)length != 0 && fread(bytes, 1, (size_t)length, file) != (size_t)length)) {
    free(bytes);
    fclose(file);
    return 0;
  }
  fclose(file);
  *out_bytes = bytes;
  *out_length = (size_t)length;
  return 1;
}

static uint64_t bb_cli_hash_bytes(const uint8_t *bytes, size_t length) {
  uint64_t hash = UINT64_C(14695981039346656037);
  size_t index;
  for (index = 0; index < length; index += 1) hash = (hash ^ bytes[index]) * UINT64_C(1099511628211);
  return hash;
}

static char *bb_cli_directory(const char *path) {
  const char *slash = strrchr(path, '/');
  size_t length = slash == NULL ? 1 : (size_t)(slash - path);
  char *directory;
  if (slash == NULL) return bb_cli_copy_string(".");
  if (length == 0) length = 1;
  directory = malloc(length + 1);
  if (directory == NULL) return NULL;
  memcpy(directory, path, length);
  directory[length] = '\0';
  return directory;
}

static char *bb_cli_join(const char *directory, const char *path) {
  const size_t directory_length = strlen(directory);
  const size_t path_length = strlen(path);
  char *joined;
  if (path[0] == '/') return bb_cli_copy_string(path);
  joined = malloc(directory_length + 1 + path_length + 1);
  if (joined == NULL) return NULL;
  memcpy(joined, directory, directory_length);
  joined[directory_length] = '/';
  memcpy(joined + directory_length + 1, path, path_length + 1);
  return joined;
}

static void bb_cli_file_record_destroy(bb_cli_file_record *record) {
  if (record == NULL) return;
  bb_cli_png_destroy(&record->decoded);
  free(record->bytes);
  free(record->path);
  free(record->logical);
  memset(record, 0, sizeof(*record));
}

static void bb_cli_host_destroy(bb_cli_host *host) {
  size_t index;
  for (index = 0; index < host->asset_count; index += 1) bb_cli_file_record_destroy(&host->assets[index]);
  for (index = 0; index < host->module_count; index += 1) bb_cli_file_record_destroy(&host->modules[index]);
  free(host->assets);
  free(host->modules);
  free(host->base_directory);
  memset(host, 0, sizeof(*host));
}

static bb_cli_file_record *bb_cli_host_add_record(
  bb_cli_file_record **records,
  size_t *count,
  size_t *capacity
) {
  bb_cli_file_record *replacement;
  if (*count == *capacity) {
    const size_t new_capacity = *capacity == 0 ? 4 : *capacity * 2;
    if (new_capacity < *capacity || new_capacity > SIZE_MAX / sizeof(**records)) return NULL;
    replacement = realloc(*records, new_capacity * sizeof(**records));
    if (replacement == NULL) return NULL;
    *records = replacement;
    *capacity = new_capacity;
  }
  memset(&(*records)[*count], 0, sizeof(**records));
  *count += 1;
  return &(*records)[*count - 1];
}

static bb_status bb_cli_module_resolver(
  void *user,
  const bb_module_request *request,
  bb_resolved_module *out_module
) {
  bb_cli_host *host = user;
  bb_cli_file_record *record;
  char *specifier = bb_cli_copy_view(request->specifier);
  size_t index;
  (void)request->importer_identity;
  if (specifier == NULL) return BB_STATUS_OUT_OF_MEMORY;
  for (index = 0; index < host->module_count; index += 1) {
    if (strcmp(host->modules[index].logical, specifier) == 0) {
      record = &host->modules[index];
      free(specifier);
      goto found;
    }
  }
  record = bb_cli_host_add_record(&host->modules, &host->module_count, &host->module_capacity);
  if (record == NULL) {
    free(specifier);
    return BB_STATUS_OUT_OF_MEMORY;
  }
  record->logical = specifier;
  record->path = bb_cli_join(host->base_directory, specifier);
  if (record->path == NULL || !bb_cli_read_file(record->path, &record->bytes, &record->length)) return BB_STATUS_NOT_FOUND;
  (void)snprintf(
    record->content_id,
    sizeof(record->content_id),
    "fnv64:%016llx",
    (unsigned long long)bb_cli_hash_bytes(record->bytes, record->length)
  );

found:
  memset(out_module, 0, sizeof(*out_module));
  out_module->kind = record->length >= 4 && record->bytes[0] == 'B' && record->bytes[1] == 'B' &&
                         record->bytes[2] == 'M' && record->bytes[3] == 0
                       ? BB_RESOLVED_MODULE_PRECOMPILED
                       : BB_RESOLVED_MODULE_SOURCE;
  out_module->identity = (bb_string_view){record->content_id, strlen(record->content_id)};
  out_module->source_name = (bb_string_view){record->path, strlen(record->path)};
  if (out_module->kind == BB_RESOLVED_MODULE_PRECOMPILED)
    out_module->precompiled = (bb_bytes){record->bytes, record->length};
  else out_module->source = (bb_bytes){record->bytes, record->length};
  return BB_STATUS_OK;
}

static bb_status bb_cli_asset_resolver(
  void *user,
  const bb_asset_request *request,
  bb_resolved_asset *out_asset
) {
  bb_cli_host *host = user;
  bb_cli_file_record *record;
  char *logical = bb_cli_copy_view(request->logical_id);
  size_t index;
  if (logical == NULL) return BB_STATUS_OUT_OF_MEMORY;
  for (index = 0; index < host->asset_count; index += 1) {
    if (strcmp(host->assets[index].logical, logical) == 0) {
      record = &host->assets[index];
      free(logical);
      goto found;
    }
  }
  record = bb_cli_host_add_record(&host->assets, &host->asset_count, &host->asset_capacity);
  if (record == NULL) {
    free(logical);
    return BB_STATUS_OUT_OF_MEMORY;
  }
  record->logical = logical;
  record->path = bb_cli_join(host->base_directory, logical);
  if (record->path == NULL || !bb_cli_read_file(record->path, &record->bytes, &record->length) ||
      !bb_cli_png_info(record->bytes, record->length, &record->width, &record->height)) return BB_STATUS_NOT_FOUND;
  (void)snprintf(
    record->content_id,
    sizeof(record->content_id),
    "fnv64:%016llx",
    (unsigned long long)bb_cli_hash_bytes(record->bytes, record->length)
  );

found:
  memset(out_asset, 0, sizeof(*out_asset));
  out_asset->content_id = (bb_string_view){record->content_id, strlen(record->content_id)};
  out_asset->width = record->width;
  out_asset->height = record->height;
  out_asset->has_encoded_bytes = 1;
  return BB_STATUS_OK;
}

static bb_cli_file_record *bb_cli_find_asset_identity(bb_cli_host *host, bb_string_view identity) {
  size_t index;
  for (index = 0; index < host->asset_count; index += 1)
    if (bb_cli_view_is(identity, host->assets[index].content_id)) return &host->assets[index];
  return NULL;
}

static bb_status bb_cli_asset_decoder(void *user, bb_string_view content_id, bb_const_image_view *out_image) {
  bb_cli_host *host = user;
  bb_cli_file_record *record = bb_cli_find_asset_identity(host, content_id);
  if (record == NULL) return BB_STATUS_NOT_FOUND;
  if (record->decoded.rgba == NULL && !bb_cli_png_decode(record->bytes, record->length, &record->decoded))
    return BB_STATUS_INVALID_ARGUMENT;
  out_image->desc = (bb_image_desc){
    record->decoded.width,
    record->decoded.height,
    (size_t)record->decoded.width * 4,
    BB_PIXEL_FORMAT_RGBA8_UNORM,
    BB_COLOR_SPACE_NUMERIC_SRGB,
    BB_ALPHA_MODE_STRAIGHT,
  };
  out_image->data = record->decoded.rgba;
  out_image->data_length = record->decoded.rgba_length;
  return BB_STATUS_OK;
}

static bb_status bb_cli_asset_encoded(void *user, bb_string_view content_id, bb_bytes *out_bytes) {
  bb_cli_host *host = user;
  bb_cli_file_record *record = bb_cli_find_asset_identity(host, content_id);
  if (record == NULL) return BB_STATUS_NOT_FOUND;
  *out_bytes = (bb_bytes){record->bytes, record->length};
  return BB_STATUS_OK;
}

static void bb_cli_print_diagnostic(bb_context *context, const bb_diagnostic *diagnostic) {
  bb_source_info source;
  uint32_t line = 1;
  uint32_t column = 1;
  uint32_t index;
  const char *severity = diagnostic->severity == BB_DIAGNOSTIC_ERROR ? "error"
                         : diagnostic->severity == BB_DIAGNOSTIC_WARNING ? "warning"
                                                                         : "note";
  if (bb_context_source_info(context, diagnostic->primary_span.source_id, &source) != BB_STATUS_OK) return;
  for (index = 0; index < diagnostic->primary_span.byte_start; index += 1) {
    if (source.contents.data[index] == '\n') {
      line += 1;
      column = 1;
    } else column += 1;
  }
  fprintf(
    stderr,
    "%.*s:%u:%u: %s[%u]: %.*s\n",
    (int)source.name.length,
    source.name.data,
    line,
    column,
    severity,
    diagnostic->code,
    (int)diagnostic->message.length,
    diagnostic->message.data
  );
}

static int bb_cli_load(const char *path, bb_cli_loaded *out_loaded) {
  uint8_t *source = NULL;
  size_t source_length = 0;
  bb_source_id source_id = BB_SOURCE_ID_NONE;
  bb_syntax_tree *syntax = NULL;
  bb_compile_options options;
  size_t index;
  bb_status status;
  memset(out_loaded, 0, sizeof(*out_loaded));
  if (!bb_cli_read_file(path, &source, &source_length)) {
    fprintf(stderr, "could not read %s\n", path);
    return 0;
  }
  out_loaded->host.base_directory = bb_cli_directory(path);
  if (out_loaded->host.base_directory == NULL || bb_context_create(NULL, &out_loaded->context) != BB_STATUS_OK) goto fail;
  status = bb_context_add_source(
    out_loaded->context,
    (bb_string_view){path, strlen(path)},
    (bb_bytes){source, source_length},
    &source_id
  );
  if (status == BB_STATUS_OK) status = bb_syntax_parse(out_loaded->context, source_id, &syntax);
  bb_compile_options_init(&options);
  options.user = &out_loaded->host;
  options.resolve_module = bb_cli_module_resolver;
  options.resolve_asset = bb_cli_asset_resolver;
  options.decode_asset = bb_cli_asset_decoder;
  options.encoded_asset = bb_cli_asset_encoded;
  if (status == BB_STATUS_OK)
    status = bb_program_compile_with_options(out_loaded->context, syntax, &options, &out_loaded->program);
  bb_syntax_tree_destroy(syntax);
  free(source);
  if (status != BB_STATUS_OK) {
    fprintf(stderr, "compile failed: %s\n", bb_status_name(status));
    goto fail_without_source;
  }
  for (index = 0; index < bb_program_diagnostic_count(out_loaded->program); index += 1) {
    bb_diagnostic diagnostic;
    if (bb_program_diagnostic(out_loaded->program, index, &diagnostic) == BB_STATUS_OK) {
      bb_cli_print_diagnostic(out_loaded->context, &diagnostic);
      if (diagnostic.severity == BB_DIAGNOSTIC_ERROR) out_loaded->error_count += 1;
    }
  }
  return 1;

fail:
  free(source);
fail_without_source:
  bb_program_destroy(out_loaded->program);
  bb_context_destroy(out_loaded->context);
  bb_cli_host_destroy(&out_loaded->host);
  memset(out_loaded, 0, sizeof(*out_loaded));
  return 0;
}

static void bb_cli_loaded_destroy(bb_cli_loaded *loaded) {
  bb_program_destroy(loaded->program);
  bb_context_destroy(loaded->context);
  bb_cli_host_destroy(&loaded->host);
  memset(loaded, 0, sizeof(*loaded));
}

static int bb_cli_safe_relative(bb_string_view path) {
  size_t index;
  if (path.length == 0 || path.data[0] == '/') return 0;
  for (index = 0; index + 1 < path.length; index += 1) {
    if (path.data[index] == '.' && path.data[index + 1] == '.' &&
        (index == 0 || path.data[index - 1] == '/') &&
        (index + 2 == path.length || path.data[index + 2] == '/')) return 0;
  }
  return 1;
}

static int bb_cli_mkdirs(const char *path) {
  char *copy = bb_cli_copy_string(path);
  char *cursor;
  if (copy == NULL) return 0;
  for (cursor = copy + 1; *cursor != '\0'; cursor += 1) {
    if (*cursor != '/') continue;
    *cursor = '\0';
    if (mkdir(copy, 0777) != 0 && errno != EEXIST) {
      free(copy);
      return 0;
    }
    *cursor = '/';
  }
  free(copy);
  return 1;
}

static char *bb_cli_artifact_path(const char *directory, const bb_artifact_value *artifact) {
  bb_string_view relative = artifact->path.length == 0 ? artifact->key : artifact->path;
  const int has_png = relative.length >= 4 && memcmp(relative.data + relative.length - 4, ".png", 4) == 0;
  const size_t directory_length = strlen(directory);
  char *path;
  if (!bb_cli_safe_relative(relative)) return NULL;
  path = malloc(directory_length + 1 + relative.length + (has_png ? 0 : 4) + 1);
  if (path == NULL) return NULL;
  memcpy(path, directory, directory_length);
  path[directory_length] = '/';
  memcpy(path + directory_length + 1, relative.data, relative.length);
  if (!has_png) memcpy(path + directory_length + 1 + relative.length, ".png", 4);
  path[directory_length + 1 + relative.length + (has_png ? 0 : 4)] = '\0';
  return path;
}

static int bb_cli_list(bb_cli_loaded *loaded, int summary_only) {
  const bb_image_graph *graph = bb_program_image_graph(loaded->program);
  size_t parameter_index;
  size_t output_index;
  printf("parameters\t%lu\n", (unsigned long)bb_program_parameter_count(loaded->program));
  for (parameter_index = 0; parameter_index < bb_program_parameter_count(loaded->program); parameter_index += 1) {
    bb_program_parameter_info parameter;
    if (bb_program_parameter(loaded->program, parameter_index, &parameter) != BB_STATUS_OK) return 0;
    printf("parameter\t%.*s\ttype=%u\n", (int)parameter.name.length, parameter.name.data, parameter.type);
  }
  printf("outputs\t%lu\n", (unsigned long)bb_program_output_count(loaded->program));
  for (output_index = 0; output_index < bb_program_output_count(loaded->program); output_index += 1) {
    bb_program_output_info output;
    uint64_t item_index;
    if (bb_program_output(loaded->program, output_index, &output) != BB_STATUS_OK) return 0;
    printf("output\t%lu\tcardinality=%llu\n", (unsigned long)output_index, (unsigned long long)output.cardinality);
    if (summary_only) continue;
    for (item_index = 0; item_index < output.cardinality; item_index += 1) {
      bb_artifact_value artifact;
      uint32_t width;
      uint32_t height;
      bb_hash128 hash;
      if (bb_program_output_artifact(loaded->program, output_index, item_index, &artifact) != BB_STATUS_OK ||
          bb_image_graph_node_dimensions(graph, artifact.image, &width, &height) != BB_STATUS_OK ||
          bb_image_graph_node_hash(graph, artifact.image, &hash) != BB_STATUS_OK) return 0;
      printf(
        "item\t%lu\t%llu\t%.*s\t%ux%u\t%016llx%016llx\tprovenance=%lu\n",
        (unsigned long)output_index,
        (unsigned long long)item_index,
        (int)artifact.key.length,
        artifact.key.data,
        width,
        height,
        (unsigned long long)hash.high,
        (unsigned long long)hash.low,
        (unsigned long)bb_image_graph_provenance_count(graph, artifact.image)
      );
    }
  }
  return 1;
}

static const char *bb_cli_graph_kind_name(bb_image_node_kind kind) {
  switch (kind) {
    case BB_IMAGE_NODE_FILL: return "fill";
    case BB_IMAGE_NODE_ALPHA_FIELD: return "alpha-field";
    case BB_IMAGE_NODE_PRESET_GRADIENT: return "preset-gradient";
    case BB_IMAGE_NODE_LINEAR_GRADIENT: return "linear-gradient";
    case BB_IMAGE_NODE_ELLIPTICAL_GRADIENT: return "elliptical-gradient";
    case BB_IMAGE_NODE_CROP: return "crop";
    case BB_IMAGE_NODE_CANVAS: return "canvas";
    case BB_IMAGE_NODE_ROTATE: return "rotate";
    case BB_IMAGE_NODE_OPACITY: return "opacity";
    case BB_IMAGE_NODE_COMPOSITE: return "composite";
    case BB_IMAGE_NODE_MASK: return "mask";
    case BB_IMAGE_NODE_RESIZE: return "resize";
    case BB_IMAGE_NODE_ASSET: return "asset";
    case BB_IMAGE_NODE_INVERT_ALPHA: return "invert-alpha";
    case BB_IMAGE_NODE_SET_VISIBLE_RGB: return "set-visible-rgb";
    case BB_IMAGE_NODE_TINT_CHROMA: return "tint-chroma";
    case BB_IMAGE_NODE_REMAP_TWO_COLOR: return "remap-two-color";
    case BB_IMAGE_NODE_SHIFT_RGB: return "shift-rgb";
    default: return "unknown";
  }
}

static int bb_cli_graph_dump(bb_cli_loaded *loaded) {
  const bb_image_graph *graph = bb_program_image_graph(loaded->program);
  uint32_t node_count;
  uint32_t node;
  size_t output_index;
  size_t parameter_index;
  /* Collection transforms lower image nodes lazily. Enumerate artifacts first
   * so the dump describes the complete graph reachable from every output. */
  for (output_index = 0; output_index < bb_program_output_count(loaded->program); output_index += 1) {
    bb_program_output_info output;
    uint64_t item_index;
    if (bb_program_output(loaded->program, output_index, &output) != BB_STATUS_OK) return 0;
    for (item_index = 0; item_index < output.cardinality; item_index += 1) {
      bb_artifact_value artifact;
      if (bb_program_output_artifact(loaded->program, output_index, item_index, &artifact) != BB_STATUS_OK) return 0;
    }
  }
  node_count = bb_image_graph_node_count(graph);
  printf("diagnostics\t%lu\n", (unsigned long)bb_program_diagnostic_count(loaded->program));
  printf("parameters\t%lu\n", (unsigned long)bb_program_parameter_count(loaded->program));
  for (parameter_index = 0; parameter_index < bb_program_parameter_count(loaded->program); parameter_index += 1) {
    bb_program_parameter_info parameter;
    if (bb_program_parameter(loaded->program, parameter_index, &parameter) != BB_STATUS_OK) return 0;
    printf(
      "parameter\t%lu\t%.*s\ttype=%u\n",
      (unsigned long)parameter_index,
      (int)parameter.name.length,
      parameter.name.data,
      (unsigned int)parameter.type
    );
  }
  printf("graph\tnodes=%u\n", node_count);
  for (node = 1; node <= node_count; node += 1) {
    bb_image_node_info info;
    bb_hash128 hash;
    size_t input_index;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    if (bb_image_graph_node_info(graph, node, &info) != BB_STATUS_OK ||
        bb_image_graph_node_hash(graph, node, &hash) != BB_STATUS_OK) return 0;
    printf(
      "node\t%u\t%s\t%ux%u\tinputs=",
      node,
      bb_cli_graph_kind_name(info.kind),
      info.width,
      info.height
    );
    if (info.input_count == 0) fputc('-', stdout);
    for (input_index = 0; input_index < info.input_count; input_index += 1) {
      if (input_index != 0) fputc(',', stdout);
      printf("%u", info.inputs[input_index]);
    }
    printf(
      "\thash=%016llx%016llx\tprovenance=%lu",
      (unsigned long long)hash.high,
      (unsigned long long)hash.low,
      (unsigned long)bb_image_graph_provenance_count(graph, node)
    );
    if (info.kind == BB_IMAGE_NODE_FILL)
      printf(
        "\tcolor=#%02x%02x%02x%02x",
        info.options.fill_color.red,
        info.options.fill_color.green,
        info.options.fill_color.blue,
        info.options.fill_color.alpha
      );
    else if (info.kind == BB_IMAGE_NODE_ASSET)
      printf("\tasset=%.*s", (int)info.options.asset.content_id.length, info.options.asset.content_id.data);
    fputc('\n', stdout);
  }
  printf("outputs\t%lu\n", (unsigned long)bb_program_output_count(loaded->program));
  for (output_index = 0; output_index < bb_program_output_count(loaded->program); output_index += 1) {
    bb_program_output_info output;
    uint64_t item_index;
    if (bb_program_output(loaded->program, output_index, &output) != BB_STATUS_OK) return 0;
    printf(
      "output\t%lu\ttype=%u\tcardinality=%llu\n",
      (unsigned long)output_index,
      (unsigned int)output.item_type,
      (unsigned long long)output.cardinality
    );
    for (item_index = 0; item_index < output.cardinality; item_index += 1) {
      bb_artifact_value artifact;
      if (bb_program_output_artifact(loaded->program, output_index, item_index, &artifact) != BB_STATUS_OK) return 0;
      printf(
        "artifact\t%lu\t%llu\tkey=%.*s\tpath=%.*s\troot=%u\talias=%u",
        (unsigned long)output_index,
        (unsigned long long)item_index,
        (int)artifact.key.length,
        artifact.key.data,
        (int)artifact.path.length,
        artifact.path.data,
        artifact.image,
        (unsigned int)artifact.alias_identity
      );
      if (artifact.alias_target.length != 0)
        printf("\ttarget=%.*s", (int)artifact.alias_target.length, artifact.alias_target.data);
      fputc('\n', stdout);
    }
  }
  return 1;
}

static int bb_cli_write_item(
  bb_cli_loaded *loaded,
  size_t output_index,
  uint64_t item_index,
  const char *directory
) {
  bb_artifact_value artifact;
  bb_surface *surface = NULL;
  char *path;
  int ok;
  if (bb_program_output_artifact(loaded->program, output_index, item_index, &artifact) != BB_STATUS_OK) return 0;
  path = bb_cli_artifact_path(directory, &artifact);
  if (path == NULL || !bb_cli_mkdirs(path) ||
      bb_cli_render_output(loaded->program, output_index, item_index, &surface) != BB_STATUS_OK) {
    free(path);
    bb_surface_destroy(surface);
    return 0;
  }
  ok = bb_cli_png_write_surface(path, surface);
  bb_surface_destroy(surface);
  free(path);
  return ok;
}

static int bb_cli_render_range(
  bb_cli_loaded *loaded,
  size_t output_index,
  uint64_t start,
  uint64_t count,
  const char *directory
) {
  bb_program_output_info output;
  uint64_t rendered = 0;
  if (bb_program_output(loaded->program, output_index, &output) != BB_STATUS_OK) return 0;
  if (start > output.cardinality) start = output.cardinality;
  if (count > output.cardinality - start) count = output.cardinality - start;
  while (rendered < count && !bb_cli_cancelled) {
    if (!bb_cli_write_item(loaded, output_index, start + rendered, directory)) return 0;
    rendered += 1;
  }
  printf("rendered\t%llu\n", (unsigned long long)rendered);
  return rendered == count;
}

static int bb_cli_precompile_file(const char *source_path, const char *output_path) {
  uint8_t *source = NULL;
  size_t source_length = 0;
  uint8_t *encoded = NULL;
  size_t encoded_length = 0;
  size_t written = 0;
  FILE *output = NULL;
  int ok = 0;
  if (!bb_cli_read_file(source_path, &source, &source_length) ||
      bb_precompiled_module_measure((bb_bytes){source, source_length}, &encoded_length) != BB_STATUS_OK)
    goto cleanup;
  encoded = malloc(encoded_length);
  if (encoded == NULL ||
      bb_precompiled_module_write(
        (bb_bytes){source, source_length}, encoded, encoded_length, &written
      ) != BB_STATUS_OK) goto cleanup;
  output = fopen(output_path, "wb");
  if (output == NULL || fwrite(encoded, 1, written, output) != written) goto cleanup;
  if (fclose(output) != 0) {
    output = NULL;
    goto cleanup;
  }
  output = NULL;
  printf("precompiled\t%lu\n", (unsigned long)written);
  ok = 1;

cleanup:
  if (output != NULL) fclose(output);
  free(encoded);
  free(source);
  return ok;
}

static int bb_cli_compare_item(
  bb_cli_loaded *loaded,
  size_t output_index,
  uint64_t item_index,
  const char *fixture_directory,
  bb_cli_compare_mode mode,
  uint32_t allowed_max_error,
  uint64_t *out_differing_units,
  uint32_t *out_max_error,
  uint64_t *out_first_mismatch,
  char out_actual_sha256[65],
  char out_fixture_sha256[65]
) {
  bb_artifact_value artifact;
  bb_bytes encoded;
  bb_surface *surface = NULL;
  bb_const_image_view actual;
  char *path = NULL;
  uint8_t *fixture_bytes = NULL;
  size_t fixture_length = 0;
  bb_cli_png fixture;
  size_t index;
  int match = 1;
  *out_differing_units = 0;
  *out_max_error = 0;
  *out_first_mismatch = UINT64_MAX;
  out_actual_sha256[0] = '\0';
  out_fixture_sha256[0] = '\0';
  memset(&fixture, 0, sizeof(fixture));
  if (bb_program_output_artifact(loaded->program, output_index, item_index, &artifact) != BB_STATUS_OK ||
      (path = bb_cli_artifact_path(fixture_directory, &artifact)) == NULL ||
      !bb_cli_read_file(path, &fixture_bytes, &fixture_length)) {
    match = 0;
    goto cleanup;
  }
  if (mode == BB_CLI_COMPARE_BYTES) {
    if (bb_program_output_encoded(loaded->program, output_index, item_index, &encoded) != BB_STATUS_OK) {
      match = 0;
      goto cleanup;
    }
    bb_cli_sha256_hex(encoded.data, encoded.length, out_actual_sha256);
    bb_cli_sha256_hex(fixture_bytes, fixture_length, out_fixture_sha256);
    for (index = 0; index < encoded.length || index < fixture_length; index += 1) {
      const uint32_t actual_byte = index < encoded.length ? encoded.data[index] : 0;
      const uint32_t fixture_byte = index < fixture_length ? fixture_bytes[index] : 0;
      const uint32_t difference = actual_byte > fixture_byte ? actual_byte - fixture_byte
                                                              : fixture_byte - actual_byte;
      if (difference != 0 || index >= encoded.length || index >= fixture_length) {
        if (*out_first_mismatch == UINT64_MAX) *out_first_mismatch = index;
        *out_differing_units += 1;
        if (difference > *out_max_error) *out_max_error = difference;
        match = 0;
      }
    }
    goto cleanup;
  }
  if (!bb_cli_png_decode(fixture_bytes, fixture_length, &fixture) ||
      bb_cli_render_output(loaded->program, output_index, item_index, &surface) != BB_STATUS_OK ||
      bb_surface_get_const_view(surface, &actual) != BB_STATUS_OK) {
    match = 0;
    goto cleanup;
  }
  if (fixture.width != actual.desc.width || fixture.height != actual.desc.height ||
      fixture.rgba_length != actual.data_length) {
    bb_cli_sha256_hex(actual.data, actual.data_length, out_actual_sha256);
    bb_cli_sha256_hex(fixture.rgba, fixture.rgba_length, out_fixture_sha256);
    *out_first_mismatch = 0;
    match = 0;
    goto cleanup;
  }
  bb_cli_sha256_hex(actual.data, actual.data_length, out_actual_sha256);
  bb_cli_sha256_hex(fixture.rgba, fixture.rgba_length, out_fixture_sha256);
  for (index = 0; index < actual.data_length; index += 1) {
    if (mode == BB_CLI_COMPARE_ALPHA && index % 4 != 3) continue;
    const uint32_t difference = actual.data[index] > fixture.rgba[index]
                                  ? actual.data[index] - fixture.rgba[index]
                                  : fixture.rgba[index] - actual.data[index];
    if (difference != 0) {
      if (*out_first_mismatch == UINT64_MAX) *out_first_mismatch = index;
      *out_differing_units += 1;
      if (difference > *out_max_error) *out_max_error = difference;
      if (difference > allowed_max_error) match = 0;
    }
  }

cleanup:
  bb_surface_destroy(surface);
  bb_cli_png_destroy(&fixture);
  free(fixture_bytes);
  free(path);
  return match;
}

static int bb_cli_compare(
  bb_cli_loaded *loaded,
  size_t output_index,
  uint64_t start,
  uint64_t count,
  const char *fixture_directory,
  const char *report_path,
  const char *contracts_path,
  const char *item_report_path,
  int alpha_only
) {
  bb_cli_contract_set contracts;
  uint64_t compared = 0;
  uint64_t matched = 0;
  uint64_t differing_units = 0;
  uint32_t max_error = 0;
  FILE *report = stdout;
  FILE *item_report = NULL;
  bb_program_output_info output;
  uint64_t item_index;
  int success;
  memset(&contracts, 0, sizeof(contracts));
  if (bb_program_output(loaded->program, output_index, &output) != BB_STATUS_OK) return 0;
  if (contracts_path != NULL &&
      (!bb_cli_contracts_load(contracts_path, &contracts) || contracts.count != output.cardinality)) {
    fprintf(stderr, "contract file does not match output cardinality\n");
    bb_cli_contracts_destroy(&contracts);
    return 0;
  }
  if (item_report_path != NULL) {
    item_report = fopen(item_report_path, "wb");
    if (item_report == NULL) {
      bb_cli_contracts_destroy(&contracts);
      return 0;
    }
    fputs(
      "index\tpath\tequivalence\tallowed-max-error\tmatched\tdiffering-units\tmax-error\tfirst-mismatch\tactual-sha256\tfixture-sha256\n",
      item_report
    );
  }
  if (start > output.cardinality) start = output.cardinality;
  if (count > output.cardinality - start) count = output.cardinality - start;
  for (item_index = 0; item_index < count && !bb_cli_cancelled; item_index += 1) {
    const uint64_t absolute_index = start + item_index;
    const bb_cli_contract *contract = contracts.count == 0 ? NULL : &contracts.items[absolute_index];
    const bb_cli_compare_mode mode = contract != NULL ? contract->mode
                                     : alpha_only    ? BB_CLI_COMPARE_ALPHA
                                                     : BB_CLI_COMPARE_RGBA;
    const char *equivalence = contract != NULL ? contract->equivalence
                              : alpha_only     ? "alpha-only-exact"
                                               : "pixel-exact";
    const uint32_t allowed_max_error = contract != NULL ? contract->max_channel_error : 0;
    bb_artifact_value artifact;
    bb_string_view logical_path;
    uint64_t item_differences = 0;
    uint32_t item_max_error = 0;
    uint64_t item_first_mismatch = UINT64_MAX;
    char actual_sha256[65] = {0};
    char fixture_sha256[65] = {0};
    char first_mismatch_text[32] = "-";
    int item_matches = bb_program_output_artifact(loaded->program, output_index, absolute_index, &artifact) == BB_STATUS_OK;
    if (item_matches) {
      logical_path = artifact.path.length == 0 ? artifact.key : artifact.path;
      if (contract != NULL && !bb_cli_view_is(logical_path, contract->path)) item_matches = 0;
    } else logical_path = (bb_string_view){NULL, 0};
    if (item_matches)
      item_matches = bb_cli_compare_item(
        loaded,
        output_index,
        absolute_index,
        fixture_directory,
        mode,
        allowed_max_error,
        &item_differences,
        &item_max_error,
        &item_first_mismatch,
        actual_sha256,
        fixture_sha256
      );
    if (item_first_mismatch != UINT64_MAX)
      snprintf(first_mismatch_text, sizeof(first_mismatch_text), "%llu", (unsigned long long)item_first_mismatch);
    if (item_matches) matched += 1;
    differing_units += item_differences;
    if (item_max_error > max_error) max_error = item_max_error;
    if (item_report != NULL)
      fprintf(
        item_report,
        "%llu\t%.*s\t%s\t%u\t%u\t%llu\t%u\t%s\t%s\t%s\n",
        (unsigned long long)absolute_index,
        (int)logical_path.length,
        logical_path.data == NULL ? "" : logical_path.data,
        equivalence,
        allowed_max_error,
        (unsigned)item_matches,
        (unsigned long long)item_differences,
        item_max_error,
        first_mismatch_text,
        actual_sha256,
        fixture_sha256
      );
    compared += 1;
  }
  if (report_path != NULL) {
    report = fopen(report_path, "wb");
    if (report == NULL) {
      if (item_report != NULL) fclose(item_report);
      bb_cli_contracts_destroy(&contracts);
      return 0;
    }
  }
  fprintf(
    report,
    "{\"equivalence\":\"%s\",\"compared\":%llu,\"matched\":%llu,\"differingUnits\":%llu,\"maxError\":%u}\n",
    contracts.count != 0 ? "manifest" : alpha_only ? "alpha-only-exact" : "pixel-exact",
    (unsigned long long)compared,
    (unsigned long long)matched,
    (unsigned long long)differing_units,
    max_error
  );
  if (report_path != NULL) fclose(report);
  if (item_report != NULL) fclose(item_report);
  success = compared == matched && !bb_cli_cancelled;
  bb_cli_contracts_destroy(&contracts);
  return success;
}

static uint64_t bb_cli_u64(const char *text, int *ok) {
  char *end = NULL;
  unsigned long long value = strtoull(text, &end, 10);
  *ok = text[0] != '\0' && end != NULL && *end == '\0';
  return (uint64_t)value;
}

static void bb_cli_usage(void) {
  fprintf(
    stderr,
    "usage:\n"
    "  binblock check FILE\n"
    "  binblock list FILE\n"
    "  binblock graph FILE\n"
    "  binblock render FILE [--output N] [--start N] [--count N] [--dir DIR]\n"
    "  binblock package FILE [--limit N] [--dir DIR]\n"
    "  binblock precompile FILE --out FILE.bbm\n"
    "  binblock inventory DIRECTORY [--out inventory.json]\n"
    "  binblock compare FILE --fixtures DIR [--contracts FILE] [--alpha-only]\n"
    "                   [--report FILE] [--item-report FILE]\n"
  );
}

int main(int argc, char **argv) {
  const char *command;
  const char *file;
  const char *directory = "out";
  const char *fixtures = NULL;
  const char *report = NULL;
  const char *contracts = NULL;
  const char *item_report = NULL;
  const char *precompile_output = NULL;
  uint64_t start = 0;
  uint64_t count = UINT64_MAX;
  uint64_t limit = UINT64_MAX;
  size_t output_index = 0;
  int index;
  int ok = 1;
  int summary_only = 0;
  int alpha_only = 0;
  bb_cli_loaded loaded;
  if (argc < 3) {
    bb_cli_usage();
    return 2;
  }
  command = argv[1];
  file = argv[2];
  for (index = 3; index < argc; index += 1) {
    int parsed;
    if (strcmp(argv[index], "--output") == 0 && index + 1 < argc) {
      output_index = (size_t)bb_cli_u64(argv[++index], &parsed);
      if (!parsed) return 2;
    } else if (strcmp(argv[index], "--start") == 0 && index + 1 < argc) {
      start = bb_cli_u64(argv[++index], &parsed);
      if (!parsed) return 2;
    } else if (strcmp(argv[index], "--count") == 0 && index + 1 < argc) {
      count = bb_cli_u64(argv[++index], &parsed);
      if (!parsed) return 2;
    } else if (strcmp(argv[index], "--limit") == 0 && index + 1 < argc) {
      limit = bb_cli_u64(argv[++index], &parsed);
      if (!parsed) return 2;
    } else if (strcmp(argv[index], "--dir") == 0 && index + 1 < argc) directory = argv[++index];
    else if (strcmp(argv[index], "--fixtures") == 0 && index + 1 < argc) fixtures = argv[++index];
    else if (strcmp(argv[index], "--report") == 0 && index + 1 < argc) report = argv[++index];
    else if (strcmp(argv[index], "--contracts") == 0 && index + 1 < argc) contracts = argv[++index];
    else if (strcmp(argv[index], "--item-report") == 0 && index + 1 < argc) item_report = argv[++index];
    else if (strcmp(argv[index], "--out") == 0 && index + 1 < argc) precompile_output = argv[++index];
    else if (strcmp(argv[index], "--summary") == 0) summary_only = 1;
    else if (strcmp(argv[index], "--alpha-only") == 0) alpha_only = 1;
    else {
      bb_cli_usage();
      return 2;
    }
  }
  signal(SIGINT, bb_cli_signal);
  signal(SIGTERM, bb_cli_signal);
  if (strcmp(command, "inventory") == 0) return bb_cli_inventory(file, precompile_output) ? 0 : 1;
  if (!bb_cli_load(file, &loaded)) return 1;
  if (loaded.error_count != 0) ok = 0;
  else if (strcmp(command, "check") == 0) printf("ok\n");
  else if (strcmp(command, "list") == 0) ok = bb_cli_list(&loaded, summary_only);
  else if (strcmp(command, "graph") == 0) ok = bb_cli_graph_dump(&loaded);
  else if (strcmp(command, "render") == 0) ok = bb_cli_render_range(&loaded, output_index, start, count, directory);
  else if (strcmp(command, "precompile") == 0 && precompile_output != NULL)
    ok = bb_cli_precompile_file(file, precompile_output);
  else if (strcmp(command, "package") == 0) {
    uint64_t rendered = 0;
    size_t output;
    for (output = 0; output < bb_program_output_count(loaded.program) && rendered < limit && !bb_cli_cancelled; output += 1) {
      bb_program_output_info info;
      uint64_t item;
      if (bb_program_output(loaded.program, output, &info) != BB_STATUS_OK) {
        ok = 0;
        break;
      }
      for (item = 0; item < info.cardinality && rendered < limit && !bb_cli_cancelled; item += 1) {
        if (!bb_cli_write_item(&loaded, output, item, directory)) {
          ok = 0;
          break;
        }
        rendered += 1;
      }
    }
    printf("packaged\t%llu\n", (unsigned long long)rendered);
  } else if (strcmp(command, "compare") == 0 && fixtures != NULL)
    ok = bb_cli_compare(
      &loaded,
      output_index,
      start,
      count,
      fixtures,
      report,
      contracts,
      item_report,
      alpha_only
    );
  else {
    bb_cli_usage();
    ok = 0;
  }
  bb_cli_loaded_destroy(&loaded);
  return ok ? 0 : 1;
}
