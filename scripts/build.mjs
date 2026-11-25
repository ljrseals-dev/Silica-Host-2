// Bundle the app (single inlined index.html that works from file://) and the ESM library.
import { build } from "esbuild";
import { readFileSync, writeFileSync, mkdirSync, rmSync, copyFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import path from "node:path";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const dist = path.join(root, "dist");
rmSync(dist, { recursive: true, force: true });
mkdirSync(dist, { recursive: true });
const common = { bundle: true, loader: { ".wasm": "binary", ".elf": "binary", ".txt": "text" }, target: "es2022", logLevel: "warning" };

await build({ ...common, entryPoints: [path.join(root, "src/app/main.ts")], outfile: path.join(dist, "app.js"), format: "iife", minify: true, sourcemap: false });
await build({ ...common, entryPoints: [path.join(root, "src/index.ts")], outfile: path.join(dist, "silica-host.mjs"), format: "esm", minify: false });

const js = readFileSync(path.join(dist, "app.js"), "utf8").replace(/<\/script/gi, "<\\/script");
const html = readFileSync(path.join(root, "public/index.html"), "utf8");
if (!html.includes("<!--APP_SCRIPT-->")) throw new Error("template marker missing");
writeFileSync(path.join(dist, "index.html"), html.replace("<!--APP_SCRIPT-->", () => `<script>${js}</script>`));
copyFileSync(path.join(root, "build/silica-core.wasm"), path.join(dist, "silica-core.wasm"));
console.log(`dist/index.html ${(readFileSync(path.join(dist, "index.html")).length / 1024).toFixed(1)} KiB (single file, wasm embedded)`);
