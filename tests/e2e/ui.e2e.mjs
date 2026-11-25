// End-to-end browser test: loads dist/index.html from file:// (no server) and drives the real UI.
import { createRequire } from "node:module";
import { fileURLToPath, pathToFileURL } from "node:url";
import path from "node:path";
import assert from "node:assert/strict";
import { writeFileSync, mkdirSync } from "node:fs";

const require = createRequire(import.meta.url);
const { chromium } = require("playwright");
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const url = pathToFileURL(path.join(root, "dist", "index.html")).href;
const shotDir = process.env.SHOT_DIR;

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 1360, height: 820 }, deviceScaleFactor: 1 });
const problems = [];
page.on("console", (m) => { if (m.type() === "error") problems.push("console.error: " + m.text()); });
page.on("pageerror", (e) => problems.push("pageerror: " + e.message));

let step = "";
const t = async (name, fn) => { step = name; await fn(); console.log("  ok  " + name); };
const text = (sel) => page.locator(sel).innerText();
const waitState = (s, timeout = 20000) => page.waitForFunction((s) => document.getElementById("st-state").textContent.startsWith(s), s, { timeout });

try {
  await page.goto(url);
  await t("boots with one instance and hello loaded (paused)", async () => {
    await page.waitForFunction(() => document.getElementById("console").textContent.includes("loaded hello"));
    assert.match(await text("#instances"), /Silica AArch64/);
    await waitState("paused");
  });
  await t("Run executes the real ELF and prints its output", async () => {
    await page.click("#btn-run");
    await waitState("exited");
    assert.match(await text("#console"), /Hello from a real AArch64 binary on SilicaHost!/);
    assert.match(await text("#console"), /guest exited with code 0/);
    assert.match(await text("#st-detail"), /exited with code 0/);
  });
  await t("registers panel shows live values (PC advanced, SP at top of RAM)", async () => {
    const regs = await text("#regs");
    assert.match(regs, /SP\s+0x00000000003ffff0/);
    assert.ok(Number((await text("#st-n")).replace(/,/g, "")) > 5);
  });
  await t("conformance sample self-verifies against the x86-64 oracle", async () => {
    await page.selectOption("#sample", "conf");
    await page.click("#btn-load-sample");
    await waitState("paused");
    await page.click("#btn-run");
    await waitState("exited");
    assert.match(await text("#console"), /conformance: 64\/64 lines match the x86-64 oracle/);
    assert.match(await text("#console"), /ccmp=/);
  });
  await t("breakpoints stop execution; Step advances; Run resumes", async () => {
    await page.selectOption("#sample", "fib");
    await page.click("#btn-load-sample");
    await waitState("paused");
    await page.fill("#bp-addr", "1000");
    await page.click("#bp-add");
    await page.click("#btn-run");
    await waitState("paused (breakpoint)");
    assert.match(await text("#st-pc"), /0x1000/);
    assert.match(await text("#bps"), /0x1000/);
    await page.click("#btn-step");
    await page.waitForFunction(() => document.getElementById("st-pc").textContent !== "0x1000");
    await page.click("#bps button");
    await page.click("#btn-run");
    await waitState("exited");
    assert.match(await text("#console"), /fib\(20\) = 6765/);
  });
  await t("memory viewer shows the loaded code and follows PC", async () => {
    await page.click("#tab-memory");
    await page.click("#mem-pc");
    await page.waitForFunction(() => document.getElementById("memdump").textContent.includes("|"));
    assert.match(await text("#memdump"), /0x00001000/);
    await page.click("#tab-console");
  });
  await t("multiple instances are independent", async () => {
    await page.click("#btn-new");
    await page.waitForFunction(() => document.querySelectorAll("#instances .inst").length === 2);
    assert.equal(await text("#console").then((s) => s.includes("fib(20)")), false, "new instance has its own console");
    await page.locator("#instances .inst").first().click();
    await page.waitForFunction(() => document.getElementById("console").textContent.includes("fib(20)"));
  });
  await t("loading garbage / a non-AArch64 file reports an error and leaves the host usable", async () => {
    const dir = path.join(root, "build", "e2e"); mkdirSync(dir, { recursive: true });
    const bad = path.join(dir, "bad.elf");
    const e = Buffer.alloc(64); e.set([0x7f, 0x45, 0x4c, 0x46, 2, 1, 1]); e.writeUInt16LE(2, 16); e.writeUInt16LE(62, 18);  // x86-64 ELF
    writeFileSync(bad, e);
    await page.setInputFiles("#file", bad);
    await page.waitForFunction(() => document.getElementById("console").textContent.includes("not an AArch64 binary"));
    await page.selectOption("#sample", "hello");
    await page.click("#btn-load-sample");
    await page.click("#btn-run");
    await waitState("exited");
  });
  await t("a raw binary loads at 0x1000 and a guest fault is reported, not thrown", async () => {
    const dir = path.join(root, "build", "e2e");
    const raw = path.join(dir, "bad.bin");
    writeFileSync(raw, Buffer.from([0xff, 0xff, 0xff, 0xff]));
    await page.setInputFiles("#file", raw);
    await page.waitForFunction(() => document.getElementById("console").textContent.includes("RAW"));
    await page.click("#btn-run");
    await waitState("halted");
    assert.match(await text("#console"), /core halted: invalid or unmodelled instruction at PC 0x1000/);
  });
  await t("close instance", async () => {
    const before = await page.locator("#instances .inst").count();
    await page.locator("#instances .inst .x").first().click();
    await page.waitForFunction((n) => document.querySelectorAll("#instances .inst").length === n - 1, before);
  });
  if (shotDir) {
    mkdirSync(shotDir, { recursive: true });
    await page.selectOption("#sample", "conf"); await page.click("#btn-load-sample"); await page.click("#btn-run"); await waitState("exited");
    await page.screenshot({ path: path.join(shotDir, "ui-console.png") });
    await page.click("#tab-memory"); await page.click("#mem-pc");
    await page.screenshot({ path: path.join(shotDir, "ui-memory.png") });
  }
  assert.deepEqual(problems, [], "no console errors or uncaught exceptions");
  console.log("e2e: all passed");
} catch (e) {
  console.error(`e2e FAILED at step "${step}":`, e.message);
  console.error("page problems:", problems);
  if (shotDir) { mkdirSync(shotDir, { recursive: true }); await page.screenshot({ path: path.join(shotDir, "fail.png") }); }
  process.exitCode = 1;
} finally {
  await browser.close();
}
