#include <binblock/backend.h>

static uint64_t bb_backend_bits_through(bb_image_node_kind last) {
  uint64_t bits = 0;
  uint32_t kind;
  for (kind = BB_IMAGE_NODE_FILL; kind <= (uint32_t)last; kind += 1)
    bits |= bb_backend_node_kind_bit((bb_image_node_kind)kind);
  return bits;
}

void bb_backend_capabilities_webgl2(bb_backend_capabilities *capabilities) {
  const uint64_t geometric = bb_backend_node_kind_bit(BB_IMAGE_NODE_FILL) |
                             bb_backend_node_kind_bit(BB_IMAGE_NODE_CROP) |
                             bb_backend_node_kind_bit(BB_IMAGE_NODE_CANVAS) |
                             bb_backend_node_kind_bit(BB_IMAGE_NODE_ROTATE) |
                             bb_backend_node_kind_bit(BB_IMAGE_NODE_ASSET);
  if (capabilities == NULL) return;
  bb_backend_capabilities_init(capabilities);
  capabilities->flags = BB_BACKEND_CAN_UPLOAD | BB_BACKEND_CAN_READBACK | BB_BACKEND_DYNAMIC_FIELDS;
  capabilities->supported_node_kinds = bb_backend_bits_through(BB_IMAGE_NODE_ASSET) &
                                       ~bb_backend_node_kind_bit(BB_IMAGE_NODE_RESIZE);
  capabilities->exact_node_kinds = geometric;
  capabilities->bounded_node_kinds = capabilities->supported_node_kinds & ~geometric;
  capabilities->dynamic_node_kinds = bb_backend_node_kind_bit(BB_IMAGE_NODE_FILL) |
                                     bb_backend_node_kind_bit(BB_IMAGE_NODE_ALPHA_FIELD) |
                                     bb_backend_node_kind_bit(BB_IMAGE_NODE_PRESET_GRADIENT) |
                                     bb_backend_node_kind_bit(BB_IMAGE_NODE_LINEAR_GRADIENT) |
                                     bb_backend_node_kind_bit(BB_IMAGE_NODE_ELLIPTICAL_GRADIENT) |
                                     bb_backend_node_kind_bit(BB_IMAGE_NODE_OPACITY) |
                                     bb_backend_node_kind_bit(BB_IMAGE_NODE_COMPOSITE) |
                                     bb_backend_node_kind_bit(BB_IMAGE_NODE_MASK);
  capabilities->bounded_max_channel_error = 1;
  capabilities->max_texture_width = 16384;
  capabilities->max_texture_height = 16384;
  capabilities->max_resources = 16;
  capabilities->preferred_tile_width = 1;
  capabilities->preferred_tile_height = 1;
  capabilities->row_alignment = 4;
}

void bb_backend_capabilities_webgpu(bb_backend_capabilities *capabilities) {
  bb_backend_capabilities_webgl2(capabilities);
  if (capabilities == NULL) return;
  capabilities->supported_node_kinds |= bb_backend_node_kind_bit(BB_IMAGE_NODE_RESIZE);
  capabilities->bounded_node_kinds |= bb_backend_node_kind_bit(BB_IMAGE_NODE_RESIZE);
  capabilities->max_resources = 128;
  capabilities->row_alignment = 256;
}

void bb_backend_capabilities_godot_rendering_device(bb_backend_capabilities *capabilities) {
  bb_backend_capabilities_webgl2(capabilities);
  if (capabilities == NULL) return;
  capabilities->supported_node_kinds |= bb_backend_node_kind_bit(BB_IMAGE_NODE_RESIZE);
  capabilities->bounded_node_kinds |= bb_backend_node_kind_bit(BB_IMAGE_NODE_RESIZE);
  capabilities->max_resources = 32;
  capabilities->row_alignment = 16;
}

void bb_backend_capabilities_wii_gx(bb_backend_capabilities *capabilities) {
  const uint64_t direct = bb_backend_node_kind_bit(BB_IMAGE_NODE_FILL) |
                          bb_backend_node_kind_bit(BB_IMAGE_NODE_CROP) |
                          bb_backend_node_kind_bit(BB_IMAGE_NODE_CANVAS) |
                          bb_backend_node_kind_bit(BB_IMAGE_NODE_ROTATE) |
                          bb_backend_node_kind_bit(BB_IMAGE_NODE_OPACITY) |
                          bb_backend_node_kind_bit(BB_IMAGE_NODE_COMPOSITE) |
                          bb_backend_node_kind_bit(BB_IMAGE_NODE_MASK) |
                          bb_backend_node_kind_bit(BB_IMAGE_NODE_ASSET);
  if (capabilities == NULL) return;
  bb_backend_capabilities_init(capabilities);
  capabilities->flags = BB_BACKEND_CAN_UPLOAD;
  capabilities->supported_node_kinds = direct;
  capabilities->exact_node_kinds = bb_backend_node_kind_bit(BB_IMAGE_NODE_FILL) |
                                   bb_backend_node_kind_bit(BB_IMAGE_NODE_CROP) |
                                   bb_backend_node_kind_bit(BB_IMAGE_NODE_CANVAS) |
                                   bb_backend_node_kind_bit(BB_IMAGE_NODE_ROTATE) |
                                   bb_backend_node_kind_bit(BB_IMAGE_NODE_ASSET);
  capabilities->bounded_node_kinds = direct & ~capabilities->exact_node_kinds;
  capabilities->dynamic_node_kinds = bb_backend_node_kind_bit(BB_IMAGE_NODE_OPACITY) |
                                     bb_backend_node_kind_bit(BB_IMAGE_NODE_COMPOSITE) |
                                     bb_backend_node_kind_bit(BB_IMAGE_NODE_MASK);
  capabilities->bounded_max_channel_error = 8;
  capabilities->max_texture_width = 1024;
  capabilities->max_texture_height = 1024;
  capabilities->max_resources = 8;
  capabilities->preferred_tile_width = 4;
  capabilities->preferred_tile_height = 4;
  capabilities->row_alignment = 32;
}
