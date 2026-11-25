// Bundle the TypeScript tests with esbuild (embedding wasm/elf as binary) and run them with node:test.
import { build } from "esbuild";
import { readdirSync, rmSync, mkdirSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import path from "node:path";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const out = path.join(root, "build", "tests");
rmSync(out, { recursive: true, force: true });
mkdirSync(out, { recursive: true });
const entries = readdirSync(path.join(root, "tests")).filter((f) => f.endsWith(".test.ts")).map((f) => path.join(root, "tests", f));
await build({
  entryPoints: entries, outdir: out, bundle: true, platform: "node", format: "esm", target: "node22",
  loader: { ".wasm": "binary", ".elf": "binary" }, outExtension: { ".js": ".mjs" }, logLevel: "warning",
  // import.meta.url must keep pointing at the source tree for fixture lookups
  define: {}, banner: { js: "" },
});
const r = spawnSync(process.execPath, ["--test", ...entries.map((e) => path.join(out, path.basename(e).replace(/\.ts$/, ".mjs")))], { stdio: "inherit", cwd: root });
process.exit(r.status ?? 1);
