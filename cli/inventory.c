#include "inventory.h"

#include "png.h"
#include "sha256.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#endif

typedef struct bb_cli_inventory_record {
  char *path;
  char *full_path;
  size_t byte_length;
  char encoded_sha256[65];
  uint32_t width;
  uint32_t height;
  const char *pixel_format;
  const char *alpha_presence;
  char decoded_sha256[65];
  const char *family;
  const char *equivalence;
  const char *alias_identity;
  const char *alias_target;
} bb_cli_inventory_record;

typedef struct bb_cli_inventory_set {
  bb_cli_inventory_record *records;
  size_t count;
  size_t capacity;
} bb_cli_inventory_set;

typedef struct bb_cli_family {
  const char *path;
  const char *id;
} bb_cli_family;

static const bb_cli_family bb_cli_families[] = {
  {"col", "flat-color"},
  {"Gradient Layers Alpha Maps", "gradient-masks"},
  {"col bin 2", "gradient-variants"},
  {"blue 64-8 24 bit", "downscaled"},
  {"red FG-Alpha", "foreground-alpha"},
  {"Red-col fg-alpha Print", "foreground-composites"},
  {"brown bear", "elliptical-gradients"},
  {"out4 - Select Library", "layer-compositions-curated"},
  {"out4 - modding", "layer-compositions-modding"},
  {"out4-special", "layer-compositions-special"},
  {"No AA 64px Black+White", "sans-glyphs"},
  {"New folder", "serif-glyphs"},
  {"result", "ordered-results"},
};

static const char *bb_cli_result_groups[] = {
  "col_blue_hi", "col_blue_lo", "col_cyan_hi", "col_cyan_lo", "col_green_hi", "col_green_lo",
  "col_pink_hi", "col_pink_lo", "col_red_hi", "col_red_lo", "col_yellow_hi", "col_yellow_lo",
};

static char *bb_cli_inventory_copy(const char *text) {
  const size_t length = strlen(text);
  char *result = malloc(length + 1);
  if (result != NULL) memcpy(result, text, length + 1);
  return result;
}

static char *bb_cli_inventory_join(const char *left, const char *right) {
  const size_t left_length = strlen(left);
  const size_t right_length = strlen(right);
  const int separator = left_length != 0 && left[left_length - 1] != '/' && left[left_length - 1] != '\\';
  char *result;
  if (left_length > SIZE_MAX - right_length - (size_t)separator - 1) return NULL;
  result = malloc(left_length + (size_t)separator + right_length + 1);
  if (result == NULL) return NULL;
  memcpy(result, left, left_length);
  if (separator) result[left_length] = '/';
  memcpy(result + left_length + (size_t)separator, right, right_length + 1);
  return result;
}

static int bb_cli_inventory_png_name(const char *name) {
  const size_t length = strlen(name);
  return length >= 4 && name[length - 4] == '.' && tolower((unsigned char)name[length - 3]) == 'p' &&
         tolower((unsigned char)name[length - 2]) == 'n' && tolower((unsigned char)name[length - 1]) == 'g';
}

static int bb_cli_inventory_push(bb_cli_inventory_set *set, const char *path, const char *full_path) {
  bb_cli_inventory_record *replacement;
  bb_cli_inventory_record *record;
  if (set->count == set->capacity) {
    const size_t capacity = set->capacity == 0 ? 128 : set->capacity * 2;
    if (capacity < set->capacity || capacity > SIZE_MAX / sizeof(*set->records)) return 0;
    replacement = realloc(set->records, capacity * sizeof(*set->records));
    if (replacement == NULL) return 0;
    set->records = replacement;
    set->capacity = capacity;
  }
  record = &set->records[set->count];
  memset(record, 0, sizeof(*record));
  record->path = bb_cli_inventory_copy(path);
  record->full_path = bb_cli_inventory_copy(full_path);
  if (record->path == NULL || record->full_path == NULL) {
    free(record->full_path);
    free(record->path);
    memset(record, 0, sizeof(*record));
    return 0;
  }
  set->count += 1;
  return 1;
}

#if defined(_WIN32)
static int bb_cli_inventory_walk(bb_cli_inventory_set *set, const char *root, const char *relative) {
  char *directory = relative[0] == '\0' ? bb_cli_inventory_copy(root) : bb_cli_inventory_join(root, relative);
  char *pattern;
  WIN32_FIND_DATAA entry;
  HANDLE search;
  int ok = 1;
  if (directory == NULL) return 0;
  pattern = bb_cli_inventory_join(directory, "*");
  free(directory);
  if (pattern == NULL) return 0;
  search = FindFirstFileA(pattern, &entry);
  free(pattern);
  if (search == INVALID_HANDLE_VALUE) return 0;
  do {
    char *child_relative;
    char *full_path;
    if (strcmp(entry.cFileName, ".") == 0 || strcmp(entry.cFileName, "..") == 0) continue;
    child_relative = relative[0] == '\0' ? bb_cli_inventory_copy(entry.cFileName)
                                          : bb_cli_inventory_join(relative, entry.cFileName);
    if (child_relative == NULL) {
      ok = 0;
      break;
    }
    if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
      ok = bb_cli_inventory_walk(set, root, child_relative);
    else if (bb_cli_inventory_png_name(entry.cFileName)) {
      full_path = bb_cli_inventory_join(root, child_relative);
      ok = full_path != NULL && bb_cli_inventory_push(set, child_relative, full_path);
      free(full_path);
    }
    free(child_relative);
  } while (ok && FindNextFileA(search, &entry));
  FindClose(search);
  return ok;
}
#else
static int bb_cli_inventory_walk(bb_cli_inventory_set *set, const char *root, const char *relative) {
  char *directory_path = relative[0] == '\0' ? bb_cli_inventory_copy(root) : bb_cli_inventory_join(root, relative);
  DIR *directory;
  struct dirent *entry;
  int ok = 1;
  if (directory_path == NULL) return 0;
  directory = opendir(directory_path);
  free(directory_path);
  if (directory == NULL) return 0;
  while (ok && (entry = readdir(directory)) != NULL) {
    char *child_relative;
    char *full_path;
    struct stat info;
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
    child_relative = relative[0] == '\0' ? bb_cli_inventory_copy(entry->d_name)
                                          : bb_cli_inventory_join(relative, entry->d_name);
    full_path = child_relative == NULL ? NULL : bb_cli_inventory_join(root, child_relative);
    if (full_path == NULL || stat(full_path, &info) != 0) ok = 0;
    else if (S_ISDIR(info.st_mode)) ok = bb_cli_inventory_walk(set, root, child_relative);
    else if (S_ISREG(info.st_mode) && bb_cli_inventory_png_name(entry->d_name))
      ok = bb_cli_inventory_push(set, child_relative, full_path);
    free(full_path);
    free(child_relative);
  }
  closedir(directory);
  return ok;
}
#endif

static int bb_cli_inventory_compare(const void *left, const void *right) {
  const bb_cli_inventory_record *left_record = left;
  const bb_cli_inventory_record *right_record = right;
  const unsigned char *left_path = (const unsigned char *)left_record->path;
  const unsigned char *right_path = (const unsigned char *)right_record->path;
  for (;;) {
    const int left_boundary = *left_path == 0 || *left_path == '/';
    const int right_boundary = *right_path == 0 || *right_path == '/';
    if (left_boundary || right_boundary) {
      if (!left_boundary) return 1;
      if (!right_boundary) return -1;
      if (*left_path == 0 || *right_path == 0) return *left_path == *right_path ? 0 : *left_path == 0 ? -1 : 1;
    } else if (*left_path != *right_path) return *left_path < *right_path ? -1 : 1;
    left_path += 1;
    right_path += 1;
  }
}

static int bb_cli_inventory_read(const char *path, uint8_t **out_bytes, size_t *out_length) {
  FILE *file = fopen(path, "rb");
  long length;
  uint8_t *bytes;
  *out_bytes = NULL;
  *out_length = 0;
  if (file == NULL || fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
      fseek(file, 0, SEEK_SET) != 0) {
    if (file != NULL) fclose(file);
    return 0;
  }
  bytes = malloc(length == 0 ? 1 : (size_t)length);
  if (bytes == NULL || (length != 0 && fread(bytes, 1, (size_t)length, file) != (size_t)length)) {
    free(bytes);
    fclose(file);
    return 0;
  }
  if (fclose(file) != 0) {
    free(bytes);
    return 0;
  }
  *out_bytes = bytes;
  *out_length = (size_t)length;
  return 1;
}

static const char *bb_cli_inventory_family(const char *path) {
  const char *slash = strchr(path, '/');
  const size_t top_length = slash == NULL ? strlen(path) : (size_t)(slash - path);
  size_t index;
  for (index = 0; index < sizeof(bb_cli_families) / sizeof(bb_cli_families[0]); index += 1)
    if (strlen(bb_cli_families[index].path) == top_length &&
        memcmp(path, bb_cli_families[index].path, top_length) == 0)
      return bb_cli_families[index].id;
  return "unclassified";
}

static const char *bb_cli_inventory_equivalence(const bb_cli_inventory_record *record) {
  static const char *bounded_variants[] = {
    "grad00blk100", "grad00blk25", "grad00wht", "grad01blk100", "grad01blk25", "grad01wht",
    "grad02blk100", "grad02blk50", "grad02wht", "grad03blk100", "grad03blk25", "grad03blk50",
    "grad03wht100", "grad04blk100", "grad04blk50", "grad04wht",
  };
  static const char *bounded_downscaled_variants[] = {
    "grad00blk100", "grad00blk25", "grad00wht", "grad01blk100", "grad01wht",
    "grad02blk100", "grad02blk50", "grad02wht", "grad03blk100", "grad03blk25",
    "grad03wht100", "grad04blk100", "grad04wht", "grad05blk100", "grad05blk50",
    "grad05wht50", "grad06blk40", "grad10wht100-rotCCW", "grad10wht100-rotCW",
    "grad10wht100", "grad14blk25", "grad14blk50", "grad14wht", "grad15blk100",
    "grad15blk25", "grad15blk50", "grad15wht", "grad16blk20", "grad16wht",
    "grad17blk", "grad17blk50", "grad17wht",
  };
  static const char prefix[] = "col bin 2/rgb/col_blue_hi-";
  static const char downscaled_prefix[] = "blue 64-8 24 bit/col_blue_hi-";
  size_t index;
  if (strcmp(record->family, "flat-color") == 0) return "pixel-exact";
  if (strcmp(record->family, "gradient-masks") == 0)
    return strcmp(record->path, "Gradient Layers Alpha Maps/18.png") == 0 ? "raster-fallback"
                                                                           : "alpha-only-exact";
  if (strncmp(record->path, downscaled_prefix, sizeof(downscaled_prefix) - 1) == 0) {
    const char *variant = record->path + sizeof(downscaled_prefix) - 1;
    const size_t variant_length = strlen(variant);
    if (strcmp(variant, "grad06blk50.png") == 0) return "raster-fallback";
    if (variant_length > 4 && strcmp(variant + variant_length - 4, ".png") == 0)
      for (index = 0; index < sizeof(bounded_downscaled_variants) / sizeof(bounded_downscaled_variants[0]); index += 1)
        if (strlen(bounded_downscaled_variants[index]) == variant_length - 4 &&
            memcmp(variant, bounded_downscaled_variants[index], variant_length - 4) == 0)
          return "bounded-difference";
    return "pixel-exact";
  }
  if (strcmp(record->family, "downscaled") == 0) return "pixel-exact";
  if (strncmp(record->path, prefix, sizeof(prefix) - 1) == 0) {
    const char *variant = record->path + sizeof(prefix) - 1;
    const size_t variant_length = strlen(variant);
    if (variant_length > 4 && strcmp(variant + variant_length - 4, ".png") == 0)
      for (index = 0; index < sizeof(bounded_variants) / sizeof(bounded_variants[0]); index += 1)
        if (strlen(bounded_variants[index]) == variant_length - 4 &&
            memcmp(variant, bounded_variants[index], variant_length - 4) == 0)
          return "bounded-difference";
  }
  return "raster-fallback";
}

static int bb_cli_inventory_analyze(bb_cli_inventory_set *set) {
  size_t index;
  for (index = 0; index < set->count; index += 1) {
    bb_cli_inventory_record *record = &set->records[index];
    uint8_t *bytes = NULL;
    size_t byte_length = 0;
    bb_cli_png png;
    size_t pixel;
    int translucent = 0;
    size_t earlier;
    memset(&png, 0, sizeof(png));
    if (!bb_cli_inventory_read(record->full_path, &bytes, &byte_length) ||
        !bb_cli_png_decode(bytes, byte_length, &png)) {
      free(bytes);
      return 0;
    }
    record->byte_length = byte_length;
    bb_cli_sha256_hex(bytes, byte_length, record->encoded_sha256);
    bb_cli_sha256_hex(png.rgba, png.rgba_length, record->decoded_sha256);
    record->width = png.width;
    record->height = png.height;
    record->pixel_format = bytes[25] == 2 ? "RGB8_UNORM" : "RGBA8_UNORM";
    for (pixel = 3; pixel < png.rgba_length; pixel += 4)
      if (png.rgba[pixel] != 255) {
        translucent = 1;
        break;
      }
    record->alpha_presence = bytes[25] == 2 ? "none" : translucent ? "translucent" : "opaque";
    record->family = bb_cli_inventory_family(record->path);
    record->equivalence = bb_cli_inventory_equivalence(record);
    for (earlier = 0; earlier < index; earlier += 1) {
      if (strcmp(record->encoded_sha256, set->records[earlier].encoded_sha256) == 0) {
        record->alias_identity = "bytes";
        record->alias_target = set->records[earlier].path;
        record->equivalence = "byte-alias";
        break;
      }
    }
    if (record->alias_target == NULL)
      for (earlier = 0; earlier < index; earlier += 1) {
        if (strcmp(record->decoded_sha256, set->records[earlier].decoded_sha256) == 0) {
          record->alias_identity = "pixels";
          record->alias_target = set->records[earlier].path;
          record->equivalence = "pixel-alias";
          break;
        }
      }
    if (strncmp(record->path, "result/ColBinSet_", sizeof("result/ColBinSet_") - 1) == 0) {
      const char *digits = record->path + sizeof("result/ColBinSet_") - 1;
      char *end = NULL;
      const unsigned long result_index = strtoul(digits, &end, 10);
      if (end != digits && strcmp(end, ".png") == 0 && result_index < 972 && result_index % 81 == 80) {
        char expected[128];
        const size_t group_index = result_index / 81;
        const int length = snprintf(
          expected,
          sizeof(expected),
          "col bin 2/rgb/%s.png",
          bb_cli_result_groups[group_index]
        );
        if (length < 0 || (size_t)length >= sizeof(expected)) {
          bb_cli_png_destroy(&png);
          free(bytes);
          return 0;
        }
        for (earlier = 0; earlier < index; earlier += 1)
          if (strcmp(set->records[earlier].path, expected) == 0 &&
              strcmp(set->records[earlier].decoded_sha256, record->decoded_sha256) == 0) {
            record->alias_identity = "pixels";
            record->alias_target = set->records[earlier].path;
            record->equivalence = "pixel-alias";
            break;
          }
      }
    }
    bb_cli_png_destroy(&png);
    free(bytes);
  }
  return 1;
}

static void bb_cli_inventory_json_string(FILE *file, const char *value) {
  const unsigned char *cursor = (const unsigned char *)value;
  fputc('"', file);
  while (*cursor != 0) {
    if (*cursor == '"' || *cursor == '\\') fprintf(file, "\\%c", *cursor);
    else if (*cursor < 0x20) fprintf(file, "\\u%04x", *cursor);
    else fputc(*cursor, file);
    cursor += 1;
  }
  fputc('"', file);
}

static int bb_cli_inventory_write(const bb_cli_inventory_set *set, const char *output_path) {
  FILE *file = output_path == NULL ? stdout : fopen(output_path, "wb");
  size_t index;
  if (file == NULL) return 0;
  fprintf(file, "{\n  \"format\": \"binblock-inventory/v1\",\n  \"fileCount\": %zu,\n  \"files\": [\n", set->count);
  for (index = 0; index < set->count; index += 1) {
    const bb_cli_inventory_record *record = &set->records[index];
    fprintf(file, "    {\"path\":");
    bb_cli_inventory_json_string(file, record->path);
    fprintf(file, ",\"family\":");
    bb_cli_inventory_json_string(file, record->family);
    fprintf(
      file,
      ",\"byteLength\":%zu,\"encodedSha256\":\"%s\",\"width\":%u,\"height\":%u,",
      record->byte_length,
      record->encoded_sha256,
      record->width,
      record->height
    );
    fprintf(file, "\"pixelFormat\":\"%s\",\"alphaPresence\":\"%s\",", record->pixel_format, record->alpha_presence);
    fprintf(file, "\"decodedRgba8Sha256\":\"%s\",\"equivalence\":\"%s\"", record->decoded_sha256, record->equivalence);
    if (record->alias_target != NULL) {
      fprintf(file, ",\"alias\":{\"identity\":\"%s\",\"target\":", record->alias_identity);
      bb_cli_inventory_json_string(file, record->alias_target);
      fputc('}', file);
    }
    fprintf(file, "}%s\n", index + 1 == set->count ? "" : ",");
  }
  fprintf(file, "  ]\n}\n");
  if (ferror(file) || (output_path != NULL && fclose(file) != 0)) return 0;
  return 1;
}

static void bb_cli_inventory_destroy(bb_cli_inventory_set *set) {
  size_t index;
  for (index = 0; index < set->count; index += 1) {
    free(set->records[index].full_path);
    free(set->records[index].path);
  }
  free(set->records);
  memset(set, 0, sizeof(*set));
}

int bb_cli_inventory(const char *root, const char *output_path) {
  bb_cli_inventory_set set;
  int ok;
  memset(&set, 0, sizeof(set));
  ok = root != NULL && bb_cli_inventory_walk(&set, root, "");
  if (ok) qsort(set.records, set.count, sizeof(*set.records), bb_cli_inventory_compare);
  if (ok) ok = bb_cli_inventory_analyze(&set);
  if (ok) ok = bb_cli_inventory_write(&set, output_path);
  if (ok) fprintf(stderr, "inventoried\t%zu\n", set.count);
  else fprintf(stderr, "inventory failed for %s: %s\n", root == NULL ? "(null)" : root, strerror(errno));
  bb_cli_inventory_destroy(&set);
  return ok;
}
