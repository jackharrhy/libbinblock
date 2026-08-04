import { renderLegacyAlphaMap } from './legacy.js';
import { evaluateExpressionValue } from './recipe-expressions.js';

function evaluate(value, context) {
  return evaluateExpressionValue(value, { variables: context.variables, properties: context.values });
}

export const DEFAULT_SET_IMAGE_OPERATIONS = {
  'default-set/alpha-map': async (node, context) => {
    const index = evaluate(node.index, context);
    if (!Number.isInteger(index) || index < 0 || index > 18) throw new Error('default-set/alpha-map index must be an integer from 0 through 18.');
    if (index !== 18) return renderLegacyAlphaMap(index);
    const assetId = evaluate(node.rasterAsset, context);
    const asset = context.document.assets[assetId];
    if (asset?.type !== 'raster') throw new Error(`Unknown alpha-map raster asset: ${assetId}`);
    if (!context.resolveRaster) throw new Error(`No raster resolver was provided for asset ${assetId}.`);
    return renderLegacyAlphaMap(index, await context.resolveRaster(assetId, asset));
  },
};
