# Reference conformance corpus

This directory is a checked-in compatibility fixture, not a build-output
directory. Its 4,312 PNG files preserve historical paths and bytes so the
portable renderer can be measured against a stable corpus.

The files at this directory's root have distinct roles:

- `reference-manifest.json` is the generated inventory of paths, dimensions,
  formats, encoded hashes, decoded RGBA8 hashes, families, and aliases.
- `reference-set.binscript` is the generated lazy BinScript program exposing
  all 4,312 stable output paths.
- `reference-conformance.tsv` is the generated per-output equivalence and
  tolerance contract used by the native comparator.
- The remaining subdirectories contain the immutable PNG fixtures. Their
  historical names, including spaces and capitalization, are intentional.

Do not hand-edit the three generated contract files. Regenerate and verify them
from the repository root:

```sh
npm run inventory
npm run reference:program
npm run inventory:check
npm run reference:program:check
```

The hand-curated fast subset is
[`tests/fixtures/reference-smoke.json`](../tests/fixtures/reference-smoke.json).
See [`docs/reference-manifest.md`](../docs/reference-manifest.md) for the format
and [`docs/reference-reproduction.md`](../docs/reference-reproduction.md) for
the analytic, alias, and raster reproduction model.
