declare module '*dist-wasm/binblock.mjs' {
  import type { BinBlockWasmFactory } from '../bindings/javascript/binblock.js';

  const createBinBlockModule: BinBlockWasmFactory;
  export default createBinBlockModule;
}
