# Bingen

Bingen builds collections of small texture tiles from versioned JSON recipes. Recipes can generate fills and gradients, transform pinned raster assets, compose stages, expand parameter sets, and create aliases. The browser app previews the resulting atlas and exports the collection as a ZIP.

The repository includes 4,312 images under `default-set/`, grouped into the families documented in `RECIPES.md`. Some families use analytic operations; the rest use pinned raster assets. Either kind can feed later recipe stages.

## Run it

Use Node.js 20 or newer.

```sh
npm install
npm run build
```

Open `dist/index.html`, or serve the project locally:

```sh
python3 -m http.server 8888
```

Then open `http://localhost:8888/dist/`.

## Tests

```sh
npm test
```

`npm run verify` runs the tests and builds the standalone page.

## Recipe format

`bin-block-recipe/v1` documents are validated with Zod. Values use a small expression language based on Mapbox style expressions. Stages emit ordered artifact sets, and later stages can filter or expand those sets.

See `RECIPES.md` for the schema model, available operations, and examples. `REPRODUCTION.md` explains analytic, alias, and raster-backed outputs. Remaining work is tracked in `TODO.md`.

Collection ZIPs contain the effective recipes, a generated JSON Schema, the texture atlas, and a provenance manifest.

## Publication note

No license has been selected for the code or default-set assets. Confirm the assets' provenance and choose a license before making the repository public.
