#include <binblock_wii.h>

#include <malloc.h>
#include <ogc/color.h>
#include <ogc/gx.h>
#include <ogc/system.h>
#include <ogc/video.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wiiuse/wpad.h>

#include "suite_module.h"

enum {
  BB_WII_FIFO_SIZE = 256 * 1024,
  BB_WII_DEMO_TEXTURE_COUNT = 12
};

typedef struct bb_wii_demo_texture {
  GXTexObj texture;
  bb_wii_texture_desc desc;
  uint8_t *pixels;
} bb_wii_demo_texture;

typedef struct bb_wii_demo_layout {
  int16_t x;
  int16_t y;
} bb_wii_demo_layout;

static const bb_wii_demo_layout bb_wii_demo_layouts[BB_WII_DEMO_TEXTURE_COUNT] = {
  {-225, 125},
  {-75, 125},
  {75, 125},
  {225, 125},
  {-225, 0},
  {-75, 0},
  {75, 0},
  {225, 0},
  {-225, -125},
  {-75, -125},
  {75, -125},
  {225, -125},
};

static GXRModeObj bb_wii_mode;
static void *bb_wii_framebuffer;

static void bb_wii_fail(const char *message, bb_status status) {
  fprintf(stderr, "BINBLOCK_WII_DEMO_ERROR %s: %s\n", message, bb_status_name(status));
  exit(EXIT_FAILURE);
}

static bb_status bb_wii_upload_texture(
  void *user,
  const bb_wii_texture_desc *desc,
  const uint8_t *gx_rgba8,
  size_t byte_length
) {
  bb_wii_demo_texture *tile = user;
  if (tile == NULL || desc == NULL || gx_rgba8 == NULL || byte_length != desc->byte_length)
    return BB_STATUS_INVALID_ARGUMENT;
  DCFlushRange((void *)gx_rgba8, byte_length);
  GX_InitTexObj(
    &tile->texture,
    (void *)gx_rgba8,
    (uint16_t)desc->padded_width,
    (uint16_t)desc->padded_height,
    GX_TF_RGBA8,
    GX_CLAMP,
    GX_CLAMP,
    GX_FALSE
  );
  GX_InitTexObjFilterMode(&tile->texture, GX_NEAR, GX_NEAR);
  tile->desc = *desc;
  return BB_STATUS_OK;
}

static void bb_wii_init_video(void) {
  VIDEO_Init();
  WPAD_Init();
  VIDEO_GetPreferredMode(&bb_wii_mode);
  VIDEO_Configure(&bb_wii_mode);
  bb_wii_framebuffer = SYS_AllocateFramebuffer(&bb_wii_mode);
  VIDEO_ClearFrameBuffer(&bb_wii_mode, bb_wii_framebuffer, COLOR_BLACK);
  VIDEO_SetNextFramebuffer(bb_wii_framebuffer);
  VIDEO_SetBlack(false);
  VIDEO_Flush();
  VIDEO_WaitForFlush();
}

static void bb_wii_init_gx(void) {
  Mtx identity;
  Mtx44 projection;
  void *fifo = memalign(32, BB_WII_FIFO_SIZE);
  if (fifo == NULL) {
    fputs("BINBLOCK_WII_DEMO_ERROR fifo allocation\n", stderr);
    exit(EXIT_FAILURE);
  }
  memset(fifo, 0, BB_WII_FIFO_SIZE);
  GX_Init(MEM_K0_TO_K1(fifo), BB_WII_FIFO_SIZE);
  GX_SetCopyClear((GXColor){0x09, 0x0e, 0x19, 0xff}, GX_MAX_Z24);
  GX_SetViewport(0, 0, bb_wii_mode.fbWidth, bb_wii_mode.efbHeight, 0, 1);
  GX_SetScissor(0, 0, bb_wii_mode.fbWidth, bb_wii_mode.efbHeight);
  GX_SetDispCopySrc(0, 0, bb_wii_mode.fbWidth, bb_wii_mode.efbHeight);
  GX_SetDispCopyYScale(GX_GetYScaleFactor(bb_wii_mode.efbHeight, bb_wii_mode.xfbHeight));
  GX_SetDispCopyDst(bb_wii_mode.fbWidth, bb_wii_mode.xfbHeight);
  GX_SetCopyFilter(bb_wii_mode.aa, bb_wii_mode.sample_pattern, GX_TRUE, bb_wii_mode.vfilter);
  GX_SetFieldMode(
    bb_wii_mode.field_rendering,
    bb_wii_mode.viHeight == 2 * bb_wii_mode.xfbHeight ? GX_ENABLE : GX_DISABLE
  );
  GX_SetCullMode(GX_CULL_NONE);
  GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
  GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
  GX_SetNumChans(0);
  GX_SetNumTexGens(1);
  GX_SetNumTevStages(1);
  GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
  GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
  GX_SetTevOp(GX_TEVSTAGE0, GX_REPLACE);
  GX_ClearVtxDesc();
  GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
  GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
  GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_S16, 0);
  GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
  guOrtho(
    projection,
    -(float)bb_wii_mode.efbHeight / 2,
    (float)bb_wii_mode.efbHeight / 2,
    -(float)bb_wii_mode.fbWidth / 2,
    (float)bb_wii_mode.fbWidth / 2,
    0,
    1
  );
  GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
  guMtxIdentity(identity);
  GX_LoadPosMtxImm(identity, GX_PNMTX0);
  GX_SetCurrentMtx(GX_PNMTX0);
  GX_CopyDisp(bb_wii_framebuffer, GX_TRUE);
  GX_DrawDone();
}

static int16_t bb_wii_round_position(float value) {
  return (int16_t)(value < 0.0f ? value - 0.5f : value + 0.5f);
}

static void bb_wii_draw_tile(const bb_wii_demo_texture *tile, size_t index, uint32_t frame) {
  const bb_wii_demo_layout layout = bb_wii_demo_layouts[index];
  const float phase = (float)frame * 0.035f + (float)index * 0.72f;
  const int16_t half_width = bb_wii_round_position((float)tile->desc.width * 0.375f);
  const int16_t half_height = bb_wii_round_position((float)tile->desc.height * 0.5f);
  const int16_t center_y = bb_wii_round_position((float)layout.y + sinf(phase * 0.67f) * 3.0f);
  GX_LoadTexObj(&tile->texture, GX_TEXMAP0);
  GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
  GX_Position2s16(layout.x - half_width, center_y - half_height);
  GX_TexCoord2f32(0.0f, 0.0f);
  GX_Position2s16(layout.x + half_width, center_y - half_height);
  GX_TexCoord2f32(1.0f, 0.0f);
  GX_Position2s16(layout.x + half_width, center_y + half_height);
  GX_TexCoord2f32(1.0f, 1.0f);
  GX_Position2s16(layout.x - half_width, center_y + half_height);
  GX_TexCoord2f32(0.0f, 1.0f);
  GX_End();
}

int main(void) {
  bb_context_desc context_desc;
  bb_program_output_info output;
  bb_wii_demo_texture tiles[BB_WII_DEMO_TEXTURE_COUNT];
  bb_wii_program *program = NULL;
  bb_status status;
  uint32_t frame = 0;
  size_t index;
  size_t total_texture_bytes = 0;

  SYS_STDIO_Report(true);
  memset(tiles, 0, sizeof(tiles));
  bb_context_desc_init(&context_desc);
  status = bb_wii_program_load(
    &context_desc,
    (bb_bytes){bb_wii_demo_module, BB_WII_DEMO_MODULE_BYTES},
    NULL,
    &program
  );
  if (status != BB_STATUS_OK) bb_wii_fail("load", status);
  if (bb_wii_program_diagnostic_count(program) != 0 || bb_wii_program_output_count(program) != 1) {
    fputs("BINBLOCK_WII_DEMO_ERROR unexpected program shape\n", stderr);
    return EXIT_FAILURE;
  }
  status = bb_wii_program_output(program, 0, &output);
  if (status != BB_STATUS_OK) bb_wii_fail("enumerate", status);
  if (output.item_type != BB_SEMANTIC_ARTIFACT || output.cardinality != BB_WII_DEMO_TEXTURE_COUNT) {
    fputs("BINBLOCK_WII_DEMO_ERROR output is not the bounded 12-texture suite\n", stderr);
    return EXIT_FAILURE;
  }
  for (index = 0; index < BB_WII_DEMO_TEXTURE_COUNT; index += 1) {
    status = bb_wii_program_measure_output_texture(program, 0, index, &tiles[index].desc);
    if (status != BB_STATUS_OK) bb_wii_fail("measure suite texture", status);
    tiles[index].pixels = memalign(32, tiles[index].desc.byte_length);
    if (tiles[index].pixels == NULL) bb_wii_fail("suite texture allocation", BB_STATUS_OUT_OF_MEMORY);
    status = bb_wii_program_render_and_upload(
      program,
      0,
      index,
      tiles[index].pixels,
      tiles[index].desc.byte_length,
      bb_wii_upload_texture,
      &tiles[index],
      &tiles[index].desc
    );
    if (status != BB_STATUS_OK) bb_wii_fail("render/upload suite texture", status);
    total_texture_bytes += tiles[index].desc.byte_length;
  }

  bb_wii_init_video();
  bb_wii_init_gx();
  printf(
    "BINBLOCK_WII_DEMO_OK outputs=1 cardinality=12 textures=12 dimensions=48..80x32..72 gx-bytes=%lu\n",
    (unsigned long)total_texture_bytes
  );
  fflush(stdout);

  while (SYS_MainLoop()) {
    WPAD_ScanPads();
    if (WPAD_ButtonsDown(0) & WPAD_BUTTON_HOME) break;
    GX_InvalidateTexAll();
    for (index = 0; index < BB_WII_DEMO_TEXTURE_COUNT; index += 1)
      bb_wii_draw_tile(&tiles[index], index, frame);
    GX_DrawDone();
    GX_CopyDisp(bb_wii_framebuffer, GX_TRUE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    frame += 1;
  }

  bb_wii_program_destroy(program);
  for (index = 0; index < BB_WII_DEMO_TEXTURE_COUNT; index += 1) free(tiles[index].pixels);
  return EXIT_SUCCESS;
}
