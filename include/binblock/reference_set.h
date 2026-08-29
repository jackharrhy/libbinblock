#ifndef BINBLOCK_REFERENCE_SET_H
#define BINBLOCK_REFERENCE_SET_H

#include <binblock/raster.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bb_reference_alpha_map {
  bb_surface *surface;
  int32_t offset_x;
  int32_t offset_y;
  uint32_t raster_fallback;
} bb_reference_alpha_map;

/* Map 18 requires a caller-resolved 64x64 raster; maps 00-17 are analytic. */
BB_API bb_status bb_reference_set_alpha_map(
  bb_context *context,
  uint32_t index,
  const bb_surface *map_18_raster,
  bb_reference_alpha_map *out_map
);

#ifdef __cplusplus
}
#endif

#endif
