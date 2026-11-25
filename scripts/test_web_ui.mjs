import fs from "node:fs";
import path from "node:path";
import { createRequire } from "node:module";

const puppeteer = createRequire(path.join(process.cwd(), "resolve.js"))("puppeteer-core");
const dataDir = process.env.NYREXVJ_DATA_DIR;
const port = process.env.API_PORT || "9020";
const chrome = process.env.CHROME || "/usr/bin/google-chrome";
const outDir = process.env.OUT_DIR || process.cwd();
if (!dataDir) {
  console.error("Set NYREXVJ_DATA_DIR to the data folder of the running test instance");
  process.exit(2);
}
const token = fs.readFileSync(path.join(dataDir, "api_token"), "utf8").trim();
const base = "http://127.0.0.1:" + port;
const results = [];
const check = (ok, name, detail = "") => {
  results.push({ ok, name, detail });
  console.log((ok ? "OK   " : "FAIL ") + name + (detail ? "  [" + detail + "]" : ""));
};

const api = async (method, path, body) => {
  const response = await fetch(base + "/api/v1" + path, {
    method,
    headers: { Authorization: "Bearer " + token, ...(body ? { "Content-Type": "application/json" } : {}) },
    body: body ? JSON.stringify(body) : undefined,
  });
  return { status: response.status, data: await response.json().catch(() => null) };
};

const browser = await puppeteer.launch({
  executablePath: chrome,
  headless: true,
  args: ["--no-sandbox", "--disable-gpu", "--lang=en-US"],
});

const open = async (lang, width = 1100, height = 900) => {
  const page = await browser.newPage();
  await page.setViewport({ width, height });
  await page.evaluateOnNewDocument((language) => {
    Object.defineProperty(navigator, "language", { get: () => language });
    window.__csp = [];
    document.addEventListener("securitypolicyviolation", (event) => window.__csp.push(event.violatedDirective + " " + event.blockedURI));
  }, lang);
  const problems = [];
  page.on("pageerror", (error) => problems.push("pageerror " + error.message));
  page.on("console", (message) => {
    if (message.type() === "error") {
      problems.push("console " + message.text());
    }
  });
  return { page, problems };
};

const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

{
  const { page, problems } = await open("en-US");
  await page.goto(base + "/#token=" + token, { waitUntil: "load" });
  await wait(1500);
  check(!page.url().includes("token"), "the token is removed from the address bar", page.url());
  const text = await page.evaluate(() => document.body.innerText);
  check(/Connected/.test(text), "the page shows it is connected");
  check(/NyrexVJ Remote/.test(text), "title in English");
  const comp = await page.$eval(".comp", (node) => node.textContent);
  const status = (await api("GET", "/status")).data;
  check(comp === status.composition, "the composition name matches the program", comp);
  const clipCount = await page.$$eval(".clip", (nodes) => nodes.length);
  check(clipCount === 32, "four layers with eight cells", String(clipCount));
  const stored = await page.evaluate(() => sessionStorage.getItem("nyrexvj.token"));
  check(stored === token, "the token lives in session storage only");
  const local = await page.evaluate(() => JSON.stringify(Object.keys(localStorage)));
  check(local === "[]", "nothing is written to local storage", local);
  const cookies = await page.cookies();
  check(cookies.length === 0, "no cookies are set");
  await page.screenshot({ path: path.join(outDir, "desktop_en.png"), fullPage: true });

  const layers = (await api("GET", "/layers")).data.layers;
  let target = null;
  for (const layer of layers) {
    for (const cell of layer.clips) {
      if (cell.clip !== null && layer.activeColumn !== cell.column) {
        target = { layer: layer.index, column: cell.column, name: cell.clip };
        break;
      }
    }
    if (target) break;
  }
  check(target !== null, "a clip that can be triggered exists");
  if (target) {
    const index = (target.layer - 1) * 8 + (target.column - 1);
    const buttons = await page.$$(".clip");
    await buttons[index].click();
    await wait(900);
    const after = (await api("GET", "/layers")).data.layers[target.layer - 1];
    check(after.activeColumn === target.column, "clicking a clip starts it in the program", String(after.activeColumn));
    const marked = await page.$$eval(".clip", (nodes, i) => nodes[i].classList.contains("on"), index);
    check(marked, "the page marks the running clip");
  }

  const blackoutBefore = (await api("GET", "/status")).data.blackout;
  await page.click("button.hot");
  await wait(900);
  const blackoutAfter = (await api("GET", "/status")).data.blackout;
  check(blackoutAfter === !blackoutBefore, "the blackout button toggles blackout");
  await page.click("button.hot");
  await wait(900);
  check((await api("GET", "/status")).data.blackout === blackoutBefore, "and toggles it back");

  const slider = await page.$("input[type=range]");
  const box = await slider.boundingBox();
  await page.mouse.click(box.x + box.width * 0.25, box.y + box.height / 2);
  await wait(900);
  const level = (await api("GET", "/status")).data.masterLevel;
  check(Math.abs(level - 0.25) < 0.05, "the master slider sets the level", String(level));

  const opacitySliders = await page.$$("input[type=range]");
  const second = await opacitySliders[1].boundingBox();
  await page.mouse.click(second.x + second.width * 0.6, second.y + second.height / 2);
  await wait(900);
  const opacity = (await api("GET", "/layers")).data.layers[0].opacity;
  check(Math.abs(opacity - 0.6) < 0.06, "a layer opacity slider works", String(opacity));

  const solo = (await page.$$("button.warn"))[1];
  await solo.click();
  await wait(900);
  check((await api("GET", "/layers")).data.layers[0].solo === true, "the solo button works");
  await solo.click();
  await wait(900);
  check((await api("GET", "/layers")).data.layers[0].solo === false, "and releases it");

  const presets = (await api("GET", "/presets")).data;
  const listed = await page.$$eval("ul.items li span", (nodes) => nodes.map((n) => n.textContent));
  check(presets.factory.every((name) => listed.includes(name)), "all factory looks are listed", String(presets.factory.length));
  const loadButtons = await page.$$("ul.items li button");
  if (presets.factory.length > 1) {
    const wanted = presets.factory.find((name) => name !== status.composition);
    const items = await page.$$("ul.items li");
    for (const item of items) {
      const name = await item.$eval("span", (n) => n.textContent);
      if (name === wanted) {
        await (await item.$("button")).click();
        break;
      }
    }
    await wait(1500);
    const now = (await api("GET", "/status")).data.composition;
    check(now === wanted, "loading a factory look from the page works", now);
  }

  const csp = await page.evaluate(() => window.__csp);
  check(csp.length === 0, "no content security policy violation", JSON.stringify(csp));
  check(problems.length === 0, "no page errors or console errors", problems.join(" | "));
  await page.close();
}

{
  const { page, problems } = await open("de-DE");
  await page.goto(base + "/#token=" + token, { waitUntil: "load" });
  await wait(1200);
  const text = await page.evaluate(() => document.body.innerText);
  check(/Verbunden/.test(text) && /Ebene/.test(text) && /Deckkraft/.test(text), "German texts are used");
  check((await page.evaluate(() => document.documentElement.lang)) === "de", "html lang follows the language");
  await page.screenshot({ path: path.join(outDir, "desktop_de.png"), fullPage: true });
  check(problems.length === 0, "German page has no errors", problems.join(" | "));
  await page.close();
}

for (const lang of ["es", "fr", "it", "pt", "nl", "ja"]) {
  const { page, problems } = await open(lang);
  await page.goto(base + "/#token=" + token, { waitUntil: "load" });
  await wait(900);
  const title = await page.evaluate(() => document.title);
  const text = await page.evaluate(() => document.body.innerText);
  check(title !== "NyrexVJ Remote" && !/Connected|Layer\b/.test(text), "language " + lang + " is applied", title);
  check(problems.length === 0, "language " + lang + " has no errors", problems.join(" | "));
  await page.close();
}

{
  const { page } = await open("xx-XX");
  await page.goto(base + "/#token=" + token, { waitUntil: "load" });
  await wait(900);
  const text = await page.evaluate(() => document.body.innerText);
  check(/Connected/.test(text), "an unknown language falls back to English");
  await page.close();
}

{
  const { page, problems } = await open("en-US", 390, 800);
  await page.goto(base + "/#token=" + token, { waitUntil: "load" });
  await wait(1200);
  const overflow = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
  check(overflow <= 1, "no sideways scrolling on a phone width", String(overflow));
  const small = await page.$$eval("button", (nodes) => nodes.filter((n) => n.getBoundingClientRect().height < 40).length);
  check(small === 0, "all buttons are at least 40 px tall", String(small));
  await page.screenshot({ path: path.join(outDir, "phone_en.png"), fullPage: true });
  check(problems.length === 0, "phone page has no errors", problems.join(" | "));
  await page.close();
}

{
  const { page } = await open("en-US");
  await page.goto(base + "/", { waitUntil: "load" });
  await wait(500);
  const gate = await page.evaluate(() => document.querySelector(".gate") !== null);
  check(gate, "without a token the page asks for one");
  await page.type("input", "abc");
  await page.click("button.on");
  await wait(300);
  const message = await page.$eval(".err", (n) => n.textContent);
  check(message.length > 0, "a malformed token is refused in the page", message);
  await page.$eval("input", (n) => (n.value = ""));
  await page.type("input", "0".repeat(64));
  await page.click("button.on");
  await wait(1500);
  const again = await page.evaluate(() => document.querySelector(".gate") !== null);
  const err = again ? await page.$eval(".err", (n) => n.textContent) : "";
  check(again && err.length > 0, "a wrong token returns to the token form", err);
  await page.screenshot({ path: path.join(outDir, "gate.png") });
  await page.$eval("input", (n) => (n.value = ""));
  await page.type("input", token);
  await page.click("button.on");
  await wait(1500);
  const connected = await page.evaluate(() => /Connected/.test(document.body.innerText));
  check(connected, "typing the right token connects");
  await page.close();
}

{
  const page = await browser.newPage();
  const response = await page.goto(base + "/api/v1/status", { waitUntil: "load" });
  check(response.status() === 401, "opening an API address in the browser without a token gives 401", String(response.status()));
  await page.close();
}

{
  const page = await browser.newPage();
  await page.goto(base + "/", { waitUntil: "load" });
  const outcome = await page.evaluate(async () => {
    try {
      const response = await fetch("http://localhost:" + location.port + "/api/v1/status", { headers: { Authorization: "Bearer x" } });
      return "status " + response.status;
    } catch (error) {
      return "blocked";
    }
  });
  check(outcome === "blocked", "a cross-origin fetch from the page is blocked by the browser", outcome);
  await page.close();
}

await browser.close();
const failed = results.filter((r) => !r.ok).length;
console.log("browser check: " + (results.length - failed) + " passed, " + failed + " failed");
fs.writeFileSync(path.join(outDir, "browser_check.json"), JSON.stringify(results, null, 2));
process.exit(failed ? 1 : 0);
