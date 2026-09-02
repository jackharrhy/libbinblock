export const overviewExampleSource = `import "binblock/basic"

size := 64
colors := palette(
  red: #ff0000,
  green: #00ff00,
  blue: #0000ff,
  yellow: #ffff00,
)

blocks := colors.map(fill).size(size)
blocks

fade := lg(180deg, white,
  transparent-white).size(size)
blocks.mask(fade)`;

export const gradientExampleSource = `import "binblock/basic"

sky := lg(135deg,
  #ff3366,
  #ffcc33 55%,
  #3366ffff
).size(96)
sky`;

export const maskExampleSource = `import "binblock/basic"

ink := fill(#2355ffff).size(96)
soft-circle := rg(
  white 18%,
  transparent-white,
  center: [47.5, 47.5],
  radius: 44
).size(96)
ink.mask(soft-circle)`;

export const layeredExampleSource = `import "binblock/basic"

base := lg(180deg,
  #0a1833,
  #2457a6
).size(96)

light := rg(
  #ffffffff,
  #ffffff00,
  center: [30, 24],
  radius: 54
).size(96)
base.over(light, 0.72)`;

export const binScriptExamples = [
  { id: 'collections', source: overviewExampleSource },
  { id: 'gradient', source: gradientExampleSource },
  { id: 'mask', source: maskExampleSource },
  { id: 'layers', source: layeredExampleSource },
] as const;

export type BinScriptExampleId = (typeof binScriptExamples)[number]['id'];
