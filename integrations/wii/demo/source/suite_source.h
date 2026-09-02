#ifndef BINBLOCK_WII_DEMO_SUITE_SOURCE_H
#define BINBLOCK_WII_DEMO_SUITE_SOURCE_H

/* Embedded UTF-8 BinScript. The trailing C string terminator is excluded. */
static const uint8_t bb_wii_demo_source[] =
  "import \"binblock/basic\"\n"
  "\n"
  "suite := collect([\n"
  "  artifact(\"red-hi-64.png\", fill(#ff0000).size(64)),\n"
  "  artifact(\"blue-radial-64.png\", fill(#0000ff).size(64).over(rg(transparent-black 42%, black, width: 64, height: 64, center: [31.5, 31.5], radius: 32, easing: \"reference\"))),\n"
  "  artifact(\"cyan-core-64.png\", fill(#00ffff).size(64).over(rg(white 18%, transparent-white, width: 64, height: 64, center: [31.5, 31.5], radius: 32, easing: \"reference\")).over(rg(transparent-black 55%, black, width: 64, height: 64, center: [31.5, 31.5], radius: 32, easing: \"reference\"))),\n"
  "  artifact(\"green-diagonal-64.png\", lg(135deg, black 0%, #00ff00 32%, #00ff00 68%, black 100%).size(64)),\n"
  "  artifact(\"yellow-red-72x48.png\", lg(90deg, #ffff00 0%, #ffff00 49%, #ff0000 50%, #800000 100%).size(72, 48)),\n"
  "  artifact(\"magenta-column-48x72.png\", lg(0deg, #800080, #ff00ff 42%, white 50%, #ff00ff 58%, #800080).size(48, 72)),\n"
  "  artifact(\"skin-radial-64.png\", fill(#ffa169).size(64).over(rg(#ffffffaa 15%, transparent-white, width: 64, height: 64, center: [31.5, 31.5], radius: 32, easing: \"reference\")).over(rg(transparent-black 45%, #800000cc, width: 64, height: 64, center: [31.5, 31.5], radius: 32, easing: \"reference\"))),\n"
  "  artifact(\"skin-bands-72x48.png\", lg(90deg, #b16a61 0%, #b16a61 24%, #c9907c 25%, #c9907c 49%, #c9a996 50%, #c9a996 74%, #ffb5a3 75%, #ffb5a3 100%).size(72, 48)),\n"
  "  artifact(\"mono-bars-80x32.png\", lg(90deg, black 0%, black 24%, white 25%, white 49%, #808080 50%, #808080 74%, #c0c0c0 75%, #c0c0c0 100%).size(80, 32)),\n"
  "  artifact(\"cyan-red-split-80x40.png\", lg(90deg, #00ffff 0%, #00ffff 49%, #ff0000 50%, #ff0000 100%).size(80, 40)),\n"
  "  artifact(\"low-spectrum-56x72.png\", lg(0deg, #000080 0%, #000080 16%, #008080 17%, #008080 33%, #008000 34%, #008000 49%, #808000 50%, #808000 66%, #800000 67%, #800000 83%, #800080 84%, #800080 100%).size(56, 72)),\n"
  "  artifact(\"red-mask-64.png\", fill(black).size(64).over(fill(#ff0000).size(64).mask(rg(white 36%, transparent-white, width: 64, height: 64, center: [31.5, 31.5], radius: 32, easing: \"reference\")))),\n"
  "])\n"
  "\n"
  "suite\n";

#define BB_WII_DEMO_SOURCE_BYTES (sizeof(bb_wii_demo_source) - 1)

#endif
