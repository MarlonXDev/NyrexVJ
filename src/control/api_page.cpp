#include "control/api_page.hpp"

#include <initializer_list>
#include <string>

namespace nvj::api {

namespace {

std::string join(std::initializer_list<const char*> parts) {
    std::string out;
    for (const char* part : parts) {
        out += part;
    }
    return out;
}

const char* kHtml = R"NVJ(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="referrer" content="no-referrer">
<meta name="robots" content="noindex">
<title>NyrexVJ</title>
<link rel="icon" href="data:,">
<link rel="stylesheet" href="/app.css">
</head>
<body>
<div id="app"></div>
<noscript>NyrexVJ needs JavaScript for this page.</noscript>
<script src="/app.js" defer></script>
</body>
</html>
)NVJ";

const char* kCss1 = R"NVJ(:root{color-scheme:dark;--bg:#0c0e13;--panel:#161922;--panel2:#1d212c;--line:#2a3040;--text:#e9ecf3;--dim:#8b93a7;--accent:#2ad4c3;--accent2:#1b8f85;--hot:#ff4d6d;--warn:#f5b83d;--radius:10px}
*{box-sizing:border-box}
html,body{margin:0;min-height:100%;background:var(--bg);color:var(--text);font:15px/1.4 system-ui,-apple-system,"Segoe UI",Roboto,"Noto Sans",sans-serif}
body{padding:12px 12px 48px;max-width:1180px;margin:0 auto}
h1,h2{margin:0;font-weight:650;letter-spacing:.01em}
h1{font-size:19px}
h2{font-size:12px;text-transform:uppercase;color:var(--dim);letter-spacing:.09em;margin-bottom:8px}
button,input{font:inherit;color:inherit}
button{background:var(--panel2);border:1px solid var(--line);border-radius:8px;min-height:44px;padding:6px 12px;cursor:pointer;transition:background .12s,border-color .12s}
button:hover:not(:disabled){border-color:var(--accent2)}
button:active:not(:disabled){background:var(--accent2)}
button:disabled{opacity:.38;cursor:default}
button:focus-visible,input:focus-visible{outline:2px solid var(--accent);outline-offset:2px}
button.on{background:var(--accent);color:#04201d;border-color:var(--accent);font-weight:650}
button.hot.on{background:var(--hot);border-color:var(--hot);color:#fff}
button.warn.on{background:var(--warn);border-color:var(--warn);color:#241a02}
header.bar{display:flex;flex-wrap:wrap;gap:10px 16px;align-items:center;justify-content:space-between;padding:10px 12px;background:var(--panel);border:1px solid var(--line);border-radius:var(--radius);margin-bottom:12px}
.title{display:flex;align-items:baseline;gap:10px;flex-wrap:wrap}
.comp{color:var(--dim);font-size:14px;max-width:46vw;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.meta{display:flex;gap:14px;align-items:center;flex-wrap:wrap;color:var(--dim);font-size:13px;font-variant-numeric:tabular-nums}
.dot{display:inline-block;width:9px;height:9px;border-radius:50%;background:var(--hot);margin-right:6px}
.dot.live{background:var(--accent)}
section.panel{background:var(--panel);border:1px solid var(--line);border-radius:var(--radius);padding:12px;margin-bottom:12px}
.row{display:flex;flex-wrap:wrap;gap:8px;align-items:center}
.grid8{display:grid;grid-template-columns:repeat(8,minmax(0,1fr));gap:6px}
.grid16{display:grid;grid-template-columns:repeat(8,minmax(0,1fr));gap:6px;margin-bottom:8px}
.master{display:grid;grid-template-columns:auto 1fr auto;gap:12px;align-items:center}
.master label{color:var(--dim);font-size:13px}
.val{min-width:3.4em;text-align:right;color:var(--dim);font-variant-numeric:tabular-nums}
input[type=range]{width:100%;height:34px;accent-color:var(--accent);background:transparent}
.layer{border:1px solid var(--line);border-radius:var(--radius);padding:10px;margin-bottom:10px;background:var(--panel2)}
.layer header{display:flex;flex-wrap:wrap;gap:8px 14px;align-items:center;margin-bottom:8px}
.layer header strong{min-width:5.2em}
.layer .opac{display:flex;gap:8px;align-items:center;flex:1 1 220px;min-width:180px}
.clip{display:flex;flex-direction:column;align-items:flex-start;justify-content:center;gap:1px;min-height:54px;padding:6px 8px;text-align:left;overflow:hidden}
.clip small{color:var(--dim);font-size:11px}
.clip span{max-width:100%;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-size:13px}
.clip.on small{color:#0a3a35}
.lists{display:flex;flex-direction:column;gap:14px}
ul.items{list-style:none;margin:0;padding:0;display:grid;grid-template-columns:repeat(auto-fill,minmax(230px,1fr));gap:6px}
ul.items li{display:flex;gap:8px;align-items:center;justify-content:space-between;padding:4px 4px 4px 10px;border:1px solid var(--line);border-radius:8px;background:var(--panel2)}
ul.items li span{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
ul.items li.cur{border-color:var(--accent)}
.empty{color:var(--dim);font-size:13px}
)NVJ";

const char* kCss2 = R"NVJ(.gate{max-width:460px;margin:12vh auto 0;background:var(--panel);border:1px solid var(--line);border-radius:var(--radius);padding:20px}
.gate p{color:var(--dim);margin:8px 0 14px}
.gate input{width:100%;min-height:44px;padding:8px 10px;background:var(--bg);border:1px solid var(--line);border-radius:8px;margin-bottom:10px;font-family:ui-monospace,Menlo,Consolas,monospace;font-size:13px}
.gate .err{color:var(--hot);min-height:1.4em;margin:0 0 8px}
#toast{position:fixed;left:50%;bottom:16px;transform:translateX(-50%);background:#000c;border:1px solid var(--line);padding:9px 16px;border-radius:999px;max-width:92vw;opacity:0;pointer-events:none;transition:opacity .18s}
#toast.show{opacity:1}
#toast.bad{border-color:var(--hot)}
@media (max-width:720px){body{padding:8px 8px 56px}.grid8,.grid16{grid-template-columns:repeat(4,minmax(0,1fr))}.comp{max-width:100%}}
@media (prefers-reduced-motion:reduce){*{transition:none!important}}
)NVJ";

const char* kJs1 = R"NVJ("use strict";
const TOKEN_KEY = "nyrexvj.token";
const PRESS_HOLD_MS = 700;
const app = document.getElementById("app");
const ui = { texts: {}, lang: "en", token: "", timer: 0, failures: 0, slow: 0, busy: false, snap: null, held: new Map(), toastTimer: 0, nodes: {} };

function t(key, vars) {
  let text = ui.texts[key];
  if (typeof text !== "string") {
    text = key;
  }
  if (vars) {
    for (const name of Object.keys(vars)) {
      text = text.split("{" + name + "}").join(String(vars[name]));
    }
  }
  return text;
}

function h(tag, props, children) {
  const node = document.createElement(tag);
  if (props) {
    for (const name of Object.keys(props)) {
      const value = props[name];
      if (name === "class") {
        node.className = value;
      } else if (name === "text") {
        node.textContent = value;
      } else if (name.startsWith("on")) {
        node.addEventListener(name.slice(2), value);
      } else {
        node.setAttribute(name, value);
      }
    }
  }
  if (children) {
    for (const child of children) {
      if (child) {
        node.appendChild(child);
      }
    }
  }
  return node;
}

function readToken() {
  const found = /(?:^|[#&])token=([0-9a-fA-F]{64})(?:&|$)/.exec(location.hash);
  if (found) {
    const token = found[1].toLowerCase();
    try {
      sessionStorage.setItem(TOKEN_KEY, token);
    } catch (error) {
      ui.token = token;
    }
    history.replaceState(null, "", location.pathname);
    return token;
  }
  try {
    return sessionStorage.getItem(TOKEN_KEY) || "";
  } catch (error) {
    return "";
  }
}

function forgetToken() {
  ui.token = "";
  try {
    sessionStorage.removeItem(TOKEN_KEY);
  } catch (error) {
    ui.token = "";
  }
}

function toast(message, bad) {
  const node = ui.nodes.toast;
  if (!node) {
    return;
  }
  node.textContent = message;
  node.className = bad ? "show bad" : "show";
  clearTimeout(ui.toastTimer);
  ui.toastTimer = setTimeout(() => {
    node.className = "";
  }, 2600);
}

class ApiError extends Error {
  constructor(status, code, message, retry) {
    super(message);
    this.status = status;
    this.code = code;
    this.retry = retry;
  }
}

async function api(method, path, body) {
  const options = { method, cache: "no-store", credentials: "omit", referrerPolicy: "no-referrer", headers: { Authorization: "Bearer " + ui.token } };
  if (body !== undefined) {
    options.headers["Content-Type"] = "application/json";
    options.body = JSON.stringify(body);
  }
  let response;
  try {
    response = await fetch("/api/v1" + path, options);
  } catch (error) {
    throw new ApiError(0, "offline", "offline", 0);
  }
  let data = null;
  try {
    data = await response.json();
  } catch (error) {
    data = null;
  }
  if (!response.ok) {
    const info = data && data.error ? data.error : {};
    throw new ApiError(response.status, info.code || "error", info.message || "", Number(response.headers.get("Retry-After")) || 0);
  }
  return data;
}

function describe(error) {
  switch (error.code) {
    case "offline":
      return t("offline");
    case "busy":
      return t("busy");
    case "rate_limited":
    case "locked":
      return t("limited");
    case "cell_empty":
      return t("cellEmpty");
    case "deck_not_used":
      return t("deckEmpty");
    case "unauthorized":
      return t("tokenWrong");
    default:
      return error.message || t("failed");
  }
}

async function act(task, refresh) {
  try {
    await task();
    if (refresh) {
      schedule(60);
    }
  } catch (error) {
    if (error.status === 401) {
      lockOut();
      return;
    }
    toast(describe(error), true);
  }
}

const sending = new Map();
function setControl(id, value, quick) {
  let entry = sending.get(id);
  if (!entry) {
    entry = { value, timer: 0, last: 0, quick: false };
    sending.set(id, entry);
  }
  entry.value = value;
  entry.quick = entry.quick || Boolean(quick);
  ui.held.set(id, Date.now() + PRESS_HOLD_MS);
  if (entry.timer) {
    return;
  }
  const wait = Math.max(0, 100 - (Date.now() - entry.last));
  entry.timer = setTimeout(() => {
    entry.timer = 0;
    entry.last = Date.now();
    const refresh = entry.quick;
    entry.quick = false;
    act(() => api("PUT", "/controls/" + id, { value: entry.value }), refresh);
  }, wait);
}

function fire(path) {
  act(() => api("POST", path), true);
}

function percent(value) {
  return Math.round(value * 100) + "%";
}
)NVJ";

const char* kJs2 = R"NVJ(function slider(id, onValue) {
  const input = h("input", { type: "range", min: "0", max: "1000", step: "1", value: "0", "aria-label": id });
  const label = h("span", { class: "val", text: "0%" });
  input.addEventListener("input", () => {
    const value = Number(input.value) / 1000;
    label.textContent = percent(value);
    setControl(id, value);
  });
  return { input, label, set(value) {
    const until = ui.held.get(id) || 0;
    if (Date.now() < until || document.activeElement === input) {
      return;
    }
    input.value = String(Math.round(value * 1000));
    label.textContent = percent(value);
  } };
}

function buildGate(message) {
  app.textContent = "";
  const input = h("input", { type: "password", autocomplete: "off", spellcheck: "false", "aria-label": t("token"), placeholder: t("token") });
  const error = h("p", { class: "err", text: message || "" });
  const go = () => {
    const token = input.value.trim().toLowerCase();
    if (!/^[0-9a-f]{64}$/.test(token)) {
      error.textContent = t("tokenWrong");
      return;
    }
    ui.token = token;
    try {
      sessionStorage.setItem(TOKEN_KEY, token);
    } catch (failure) {
      ui.token = token;
    }
    ui.failures = 0;
    start();
  };
  input.addEventListener("keydown", (event) => {
    if (event.key === "Enter") {
      go();
    }
  });
  app.appendChild(h("div", { class: "gate" }, [
    h("h1", { text: t("appTitle") }),
    h("p", { text: t("tokenHelp") }),
    input,
    error,
    h("button", { type: "button", class: "on", onclick: go, text: t("connect") })
  ]));
  ui.nodes = { toast: h("div", { id: "toast", role: "status" }) };
  app.appendChild(ui.nodes.toast);
  input.focus();
}

function lockOut() {
  clearTimeout(ui.timer);
  forgetToken();
  buildGate(t("tokenWrong"));
}

function buildMain() {
  app.textContent = "";
  const n = {};
  n.dot = h("span", { class: "dot" });
  n.state = h("span", { text: t("connecting") });
  n.comp = h("span", { class: "comp" });
  n.bpm = h("span");
  n.fps = h("span");
  n.res = h("span");
  n.blackout = h("button", { type: "button", class: "hot", onclick: () => setBlackout(), text: t("blackout") });
  n.tap = h("button", { type: "button", onclick: () => fire("/controls/master/tap"), text: t("tap") });
  n.forget = h("button", { type: "button", onclick: () => lockOut(), text: t("forget") });
  app.appendChild(h("header", { class: "bar" }, [
    h("div", { class: "title" }, [h("h1", { text: t("appTitle") }), n.comp]),
    h("div", { class: "meta" }, [h("span", {}, [n.dot, n.state]), n.bpm, n.fps, n.res]),
    h("div", { class: "row" }, [n.tap, n.blackout, n.forget])
  ]));

  n.master = slider("master/level");
  app.appendChild(h("section", { class: "panel" }, [
    h("h2", { text: t("master") }),
    h("div", { class: "master" }, [h("label", { text: t("level") }), n.master.input, n.master.label])
  ]));

  n.decks = [];
  const deckGrid = h("div", { class: "grid16" });
  for (let i = 1; i <= 16; i += 1) {
    const button = h("button", { type: "button", onclick: () => fire("/decks/" + i + "/activate"), text: String(i) });
    n.decks.push(button);
    deckGrid.appendChild(button);
  }
  app.appendChild(h("section", { class: "panel" }, [
    h("h2", { text: t("decks") }),
    deckGrid,
    h("div", { class: "row" }, [
      h("button", { type: "button", onclick: () => fire("/decks/previous"), text: t("previous") }),
      h("button", { type: "button", onclick: () => fire("/decks/next"), text: t("next") })
    ])
  ]));

  const columns = h("div", { class: "grid8" });
  for (let i = 1; i <= 8; i += 1) {
    columns.appendChild(h("button", { type: "button", onclick: () => fire("/columns/" + i + "/trigger"), text: String(i) }));
  }
  app.appendChild(h("section", { class: "panel" }, [h("h2", { text: t("columns") }), columns]));

  n.layers = [];
  const layerBox = h("section", { class: "panel" }, [h("h2", { text: t("layers") })]);
  for (let layer = 1; layer <= 4; layer += 1) {
    layerBox.appendChild(buildLayer(layer, n));
  }
  app.appendChild(layerBox);

  n.factory = h("ul", { class: "items" });
  n.mine = h("ul", { class: "items" });
  app.appendChild(h("section", { class: "panel" }, [
    h("h2", { text: t("presets") }),
    h("div", { class: "lists" }, [
      h("div", {}, [h("h2", { text: t("factory") }), n.factory]),
      h("div", {}, [h("h2", { text: t("mine") }), n.mine])
    ])
  ]));

  n.outputs = h("ul", { class: "items" });
  app.appendChild(h("section", { class: "panel" }, [h("h2", { text: t("outputs") }), n.outputs]));
  n.toast = h("div", { id: "toast", role: "status" });
  app.appendChild(n.toast);
  ui.nodes = n;
}
)NVJ";

const char* kJs3 = R"NVJ(function buildLayer(layer, n) {
  const node = { clips: [] };
  node.opacity = slider("layer/" + layer + "/opacity");
  node.bypass = h("button", { type: "button", class: "warn", onclick: () => toggleLayer(layer, "bypass"), text: t("bypass") });
  node.solo = h("button", { type: "button", class: "warn", onclick: () => toggleLayer(layer, "solo"), text: t("solo") });
  const grid = h("div", { class: "grid8" });
  for (let column = 1; column <= 8; column += 1) {
    const name = h("span", { text: t("emptyCell") });
    const clip = h("button", { type: "button", class: "clip", onclick: () => fire("/layers/" + layer + "/clips/" + column + "/trigger") }, [h("small", { text: String(column) }), name]);
    node.clips.push({ clip, name });
    grid.appendChild(clip);
  }
  const box = h("div", { class: "layer" }, [
    h("header", {}, [
      h("strong", { text: t("layer") + " " + layer }),
      h("div", { class: "opac" }, [h("span", { class: "empty", text: t("opacity") }), node.opacity.input, node.opacity.label]),
      node.bypass,
      node.solo,
      h("button", { type: "button", onclick: () => fire("/layers/" + layer + "/clear"), text: t("clear") })
    ]),
    grid
  ]);
  n.layers.push(node);
  return box;
}

function toggleLayer(layer, what) {
  const info = ui.snap && ui.snap.layers ? ui.snap.layers[layer - 1] : null;
  const current = info ? Boolean(info[what]) : false;
  setControl("layer/" + layer + "/" + what, current ? 0 : 1, true);
}

function setBlackout() {
  const current = ui.snap && ui.snap.status ? Boolean(ui.snap.status.blackout) : false;
  setControl("master/blackout", current ? 0 : 1, true);
}

function mark(button, on) {
  button.classList.toggle("on", on);
  button.setAttribute("aria-pressed", on ? "true" : "false");
}

function fillList(list, names, current, source) {
  list.textContent = "";
  if (!names.length) {
    list.appendChild(h("li", { class: "empty", text: t("none") }));
    return;
  }
  for (const name of names) {
    const item = h("li", { class: name === current ? "cur" : "" }, [
      h("span", { text: name }),
      h("button", { type: "button", onclick: () => loadPreset(source, name), text: t("load") })
    ]);
    list.appendChild(item);
  }
}

function loadPreset(source, name) {
  act(async () => {
    await api("POST", "/presets/load", { source, name });
    toast(t("loaded", { name }));
    ui.slow = 0;
  }, true);
}

function showOutputs(list) {
  const n = ui.nodes;
  n.outputs.textContent = "";
  if (!list.length) {
    n.outputs.appendChild(h("li", { class: "empty", text: t("none") }));
    return;
  }
  for (const out of list) {
    let state = t("closed");
    if (!out.enabled) {
      state = t("off");
    } else if (out.open) {
      state = out.fullscreen ? t("fullscreen") : t("open");
    }
    const label = out.name + (out.ndi ? " (NDI)" : "");
    n.outputs.appendChild(h("li", {}, [h("span", { text: label }), h("span", { class: "empty", text: state })]));
  }
}

function render() {
  const n = ui.nodes;
  const snap = ui.snap;
  if (!snap || !snap.status) {
    return;
  }
  const s = snap.status;
  n.dot.className = "dot live";
  n.state.textContent = t("connected");
  n.comp.textContent = s.composition;
  n.comp.setAttribute("title", s.composition);
  n.bpm.textContent = Number(s.bpm).toFixed(1) + " BPM";
  n.fps.textContent = Number(s.fps).toFixed(0) + " FPS";
  n.res.textContent = s.output.width + "x" + s.output.height;
  mark(n.blackout, Boolean(s.blackout));
  n.master.set(s.masterLevel);
  for (const deck of snap.decks) {
    const button = n.decks[deck.index - 1];
    button.disabled = !deck.used;
    button.setAttribute("title", deck.name);
    button.setAttribute("aria-label", t("deck") + " " + deck.index + (deck.name ? ": " + deck.name : ""));
    mark(button, deck.active);
  }
  for (const layer of snap.layers) {
    const node = n.layers[layer.index - 1];
    node.opacity.set(layer.opacity);
    mark(node.bypass, layer.bypass);
    mark(node.solo, layer.solo);
    for (const cell of layer.clips) {
      const view = node.clips[cell.column - 1];
      view.name.textContent = cell.clip === null ? t("emptyCell") : cell.clip;
      view.clip.disabled = cell.clip === null;
      mark(view.clip, layer.activeColumn === cell.column);
    }
  }
}
)NVJ";

const char* kJs4 = R"NVJ(async function poll() {
  const [status, layers, decks] = await Promise.all([api("GET", "/status"), api("GET", "/layers"), api("GET", "/decks")]);
  if (!ui.token) {
    return;
  }
  ui.snap = { status, layers: layers.layers, decks: decks.decks };
  render();
  if (ui.slow <= 0) {
    const [presets, outputs] = await Promise.all([api("GET", "/presets"), api("GET", "/outputs")]);
    if (!ui.token) {
      return;
    }
    fillList(ui.nodes.factory, presets.factory, status.composition, "factory");
    fillList(ui.nodes.mine, presets.user, status.composition, "user");
    showOutputs(outputs.outputs);
    ui.slow = 12;
  }
  ui.slow -= 1;
}

function schedule(delay) {
  clearTimeout(ui.timer);
  if (!ui.token) {
    return;
  }
  ui.timer = setTimeout(tick, delay);
}

async function tick() {
  if (ui.busy) {
    schedule(100);
    return;
  }
  ui.busy = true;
  try {
    await poll();
    ui.failures = 0;
    ui.busy = false;
    schedule(document.hidden ? 3000 : 450);
  } catch (error) {
    ui.busy = false;
    if (!ui.token) {
      return;
    }
    if (error.status === 401) {
      lockOut();
      return;
    }
    ui.failures += 1;
    ui.nodes.dot.className = "dot";
    ui.nodes.state.textContent = describe(error);
    const base = error.retry > 0 ? error.retry * 1000 : Math.min(6000, 450 * Math.pow(2, ui.failures));
    schedule(base);
  }
}

function start() {
  ui.slow = 0;
  buildMain();
  schedule(0);
}

async function boot() {
  const wanted = (navigator.language || "en").slice(0, 2).toLowerCase();
  try {
    const response = await fetch("/lang.json", { cache: "no-store", credentials: "omit" });
    const all = await response.json();
    ui.lang = all[wanted] ? wanted : "en";
    ui.texts = Object.assign({}, all.en, all[ui.lang]);
  } catch (error) {
    ui.texts = {};
  }
  document.documentElement.lang = ui.lang;
  document.title = t("appTitle");
  ui.token = readToken();
  if (ui.token) {
    start();
  } else {
    buildGate("");
  }
}

document.addEventListener("visibilitychange", () => {
  if (!document.hidden) {
    schedule(0);
  }
});

boot();
)NVJ";

}

const PageAsset* findPageAsset(const std::string& path) {
    static const std::string js = join({kJs1, "\n", kJs2, "\n", kJs3, "\n", kJs4});
    static const std::string css = join({kCss1, kCss2});
    static const std::string lang = pageLanguageJson();
    static const PageAsset assets[] = {
        {"/", "text/html; charset=utf-8", kHtml},
        {"/app.js", "text/javascript; charset=utf-8", js.c_str()},
        {"/app.css", "text/css; charset=utf-8", css.c_str()},
        {"/lang.json", "application/json; charset=utf-8", lang.c_str()},
    };
    for (const PageAsset& asset : assets) {
        if (path == asset.path) {
            return &asset;
        }
    }
    return nullptr;
}

}
