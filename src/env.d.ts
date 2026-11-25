// Binary/text assets are embedded by esbuild (see scripts/build.mjs).
declare module "*.wasm" { const bytes: Uint8Array; export default bytes; }
declare module "*.elf" { const bytes: Uint8Array; export default bytes; }
declare module "*.txt" { const text: string; export default text; }
