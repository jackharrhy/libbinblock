#ifndef BINBLOCK_GRAPH_INTERNAL_H
#define BINBLOCK_GRAPH_INTERNAL_H

#include <binblock/graph.h>

typedef struct bb_graph_node {
  bb_image_node_kind kind;
  bb_hash128 hash;
  uint32_t depth;
  union {
    struct {
      uint32_t width;
      uint32_t height;
      bb_rgba8 color;
    } fill;
    struct {
      bb_graph_asset_desc desc;
      char *content_id;
      size_t content_id_bytes;
    } asset;
    struct {
      bb_alpha_field_desc desc;
      uint8_t *levels;
      size_t level_bytes;
    } alpha_field;
    struct {
      uint32_t width;
      uint32_t height;
      bb_gradient_preset preset;
      int32_t turns;
      bb_rgba8 color;
    } preset;
    struct {
      bb_linear_gradient_desc desc;
      bb_gradient_stop *stops;
      size_t stop_bytes;
    } linear;
    struct {
      bb_elliptical_gradient_desc desc;
      bb_gradient_stop *stops;
      size_t stop_bytes;
    } ellipse;
    struct {
      bb_image_node source;
      int32_t x;
      int32_t y;
      uint32_t width;
      uint32_t height;
    } crop;
    struct {
      bb_image_node source;
      uint32_t width;
      uint32_t height;
      int32_t x;
      int32_t y;
    } canvas;
    struct {
      bb_image_node source;
      int32_t turns;
    } rotate;
    struct {
      bb_image_node source;
      double opacity;
    } opacity;
    struct {
      bb_image_node destination;
      bb_image_node source;
      int32_t offset_x;
      int32_t offset_y;
      double opacity;
    } composite;
    struct {
      bb_image_node source;
      bb_image_node mask;
      bb_mask_mode mode;
    } mask;
    struct {
      bb_image_node source;
      uint32_t width;
      uint32_t height;
    } resize;
    struct { bb_image_node source; } invert_alpha;
    struct { bb_image_node source; bb_rgba8 color; } color_transform;
    struct {
      bb_image_node source;
      bb_rgba8 source_foreground;
      bb_rgba8 source_background;
      bb_rgba8 foreground;
      bb_rgba8 background;
    } remap;
    struct { bb_image_node source; bb_rgba8 source_base; bb_rgba8 target_base; } shift_rgb;
  } data;
} bb_graph_node;

typedef struct bb_graph_provenance {
  bb_image_node node;
  bb_span span;
} bb_graph_provenance;

struct bb_image_graph {
  bb_context *context;
  bb_graph_node *nodes;
  size_t node_count;
  size_t node_capacity;
  bb_graph_provenance *provenance;
  size_t provenance_count;
  size_t provenance_capacity;
  uint32_t sealed;
};

const bb_graph_node *bb_image_graph_get_node(const bb_image_graph *graph, bb_image_node node);

#endif
