/* =========================================================
   Stacks IDE — Frontend application
   - Monaco editor, tabs, file tree
   - xterm.js terminal bridged to backend (with offline fallback)
   - Command palette, quick open
   - Run configurations
   - IndexedDB persistence when no backend
   ========================================================= */

import { Backend } from "./assets/backend.js";
import { VFS } from "./assets/vfs.js";
import { TEMPLATES } from "./assets/templates.js";

const $ = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => [...r.querySelectorAll(s)];

const languageByExt = (name) => {
  const ext = name.includes(".") ? name.split(".").pop().toLowerCase() : "";
  return ({
    js: "javascript", mjs: "javascript", cjs: "javascript",
    ts: "typescript", tsx: "typescript", jsx: "javascript",
    json: "json", md: "markdown", css: "css", scss: "scss",
    html: "html", htm: "html", xml: "xml",
    py: "python", rb: "ruby", go: "go", rs: "rust",
    java: "java", kt: "kotlin", cs: "csharp",
    cpp: "cpp", cc: "cpp", cxx: "cpp", hpp: "cpp", h: "cpp",
    c: "c", sh: "shell", bash: "shell", ps1: "powershell",
    yaml: "yaml", yml: "yaml", toml: "ini", ini: "ini",
    sql: "sql", php: "php", lua: "lua", swift: "swift",
  }[ext]) || "plaintext";
};

const iconByName = (name, isDir) => {
  if (isDir) return "📁";
  const ext = name.split(".").pop().toLowerCase();
  return ({
    js: "🟨", ts: "🔷", py: "🐍", cpp: "🟦", c: "🟦", h: "🟦", hpp: "🟦",
    html: "🟧", css: "🎨", json: "🧾", md: "📝", sh: "💲",
    java: "☕", rs: "🦀", go: "🐹", rb: "💎", txt: "📄",
  }[ext]) || "📄";
};

const state = {
  backend: new Backend(),
  vfs: null,             // VFS instance
  models: new Map(),     // path -> monaco model
  dirty: new Set(),      // paths with unsaved changes
  openTabs: [],          // ordered paths
  current: null,         // current path
  theme: localStorage.getItem("stacks.theme") || "dark",
  commands: [],          // palette entries
  term: null,            // xterm instance
  termFit: null,
  ws: null,              // terminal ws
  editor: null,
};

/* ---------- theme ---------- */
function applyTheme(t) {
  state.theme = t;
  document.body.classList.toggle("theme-dark", t === "dark");
  document.body.classList.toggle("theme-light", t === "light");
  localStorage.setItem("stacks.theme", t);
  if (window.monaco) monaco.editor.setTheme(t === "dark" ? "stacks-dark" : "stacks-light");
  if (state.term) state.term.options.theme = termTheme();
}
function termTheme() {
  const isDark = state.theme === "dark";
  return isDark
    ? { background: "#181818", foreground: "#d4d4d4", cursor: "#60cdff", selectionBackground: "#264f78" }
    : { background: "#f7f7f7", foreground: "#1a1a1a", cursor: "#0078d4", selectionBackground: "#cde3ff" };
}

/* ---------- Monaco boot ---------- */
require.config({
  paths: { vs: "https://cdnjs.cloudflare.com/ajax/libs/monaco-editor/0.45.0/min/vs" },
});

require(["vs/editor/editor.main"], async () => {
  defineMonacoThemes();
  applyTheme(state.theme);

  state.editor = monaco.editor.create($("#editor"), {
    model: null,
    theme: state.theme === "dark" ? "stacks-dark" : "stacks-light",
    automaticLayout: true,
    fontSize: 13.5,
    fontFamily: '"Cascadia Code", "JetBrains Mono", Consolas, monospace',
    fontLigatures: true,
    minimap: { enabled: true, scale: 0.7 },
    smoothScrolling: true,
    cursorBlinking: "smooth",
    cursorSmoothCaretAnimation: "on",
    renderWhitespace: "selection",
    bracketPairColorization: { enabled: true },
    guides: { bracketPairs: true, indentation: true },
    padding: { top: 8 },
    tabSize: 2,
    wordWrap: "on",
  });

  state.editor.onDidChangeCursorPosition((e) => {
    $("#sbCursor").textContent = `Ln ${e.position.lineNumber}, Col ${e.position.column}`;
  });

  // initialize VFS + load workspace
  state.vfs = new VFS();
  await state.vfs.open();
  await seedIfEmpty();
  await refreshTree();

  // try backend; fallback to offline
  await connectBackend();

  // initial file
  const first = await firstFile();
  if (first) await openFile(first);

  // terminal
  initTerminal();

  buildCommands();
  bindUI();
});

function defineMonacoThemes() {
  monaco.editor.defineTheme("stacks-dark", {
    base: "vs-dark", inherit: true,
    rules: [
      { token: "comment", foreground: "6a9955", fontStyle: "italic" },
      { token: "keyword", foreground: "569cd6" },
      { token: "string",  foreground: "ce9178" },
      { token: "number",  foreground: "b5cea8" },
      { token: "type",    foreground: "4ec9b0" },
    ],
    colors: {
      "editor.background": "#1e1e1e",
      "editor.lineHighlightBackground": "#2a2a2a55",
      "editorGutter.background": "#1e1e1e",
      "editorLineNumber.foreground": "#858585",
      "editorLineNumber.activeForeground": "#c6c6c6",
      "editorCursor.foreground": "#60cdff",
    },
  });
  monaco.editor.defineTheme("stacks-light", {
    base: "vs", inherit: true, rules: [],
    colors: {
      "editor.background": "#ffffff",
      "editor.lineHighlightBackground": "#f1f5ff",
      "editorCursor.foreground": "#0078d4",
    },
  });
}

/* ---------- seed ---------- */
async function seedIfEmpty() {
  const all = await state.vfs.listAll();
  if (all.length === 0) {
    await state.vfs.writeFile("welcome.md", TEMPLATES.welcome);
    await state.vfs.writeFile("index.html", TEMPLATES.html);
    await state.vfs.writeFile("main.py", TEMPLATES.python);
    await state.vfs.writeFile("hello.cpp", TEMPLATES.cpp);
    await state.vfs.writeFile("scripts/build.sh", TEMPLATES.shell);
  }
}

async function firstFile() {
  const all = await state.vfs.listAll();
  const files = all.filter((e) => e.type === "file").map((e) => e.path);
  files.sort();
  return files.find((p) => p.endsWith(".md")) || files[0] || null;
}

/* ---------- tree ---------- */
async function refreshTree() {
  const all = await state.vfs.listAll();
  const tree = buildTree(all);
  const root = $("#fileTree");
  root.innerHTML = "";
  root.appendChild(renderTree(tree, 0));
}

function buildTree(entries) {
  const root = { name: "/", type: "dir", children: new Map(), path: "" };
  for (const e of entries) {
    const parts = e.path.split("/").filter(Boolean);
    let node = root;
    for (let i = 0; i < parts.length; i++) {
      const name = parts[i];
      const isLast = i === parts.length - 1;
      const isDir = isLast ? e.type === "dir" : true;
      if (!node.children.has(name)) {
        node.children.set(name, {
          name,
          type: isDir ? "dir" : "file",
          children: new Map(),
          path: parts.slice(0, i + 1).join("/"),
        });
      }
      node = node.children.get(name);
    }
  }
  return root;
}

function renderTree(node, depth) {
  const ul = document.createElement("ul");
  ul.className = depth === 0 ? "tree" : "tree nested";
  const entries = [...node.children.values()].sort((a, b) => {
    if (a.type !== b.type) return a.type === "dir" ? -1 : 1;
    return a.name.localeCompare(b.name);
  });
  for (const child of entries) {
    const li = document.createElement("li");
    li.className = child.type === "dir" ? "dir" : "file";
    li.dataset.path = child.path;
    if (child.path === state.current) li.classList.add("active");
    if (state.dirty.has(child.path)) li.classList.add("dirty");
    const icon = document.createElement("span");
    icon.className = "icon";
    icon.textContent = iconByName(child.name, child.type === "dir");
    const label = document.createElement("span");
    label.textContent = child.name;
    const dot = document.createElement("span");
    dot.className = "dot";
    li.append(icon, label, dot);
    li.style.paddingLeft = `${8 + depth * 12}px`;
    li.addEventListener("click", (e) => {
      e.stopPropagation();
      if (child.type === "file") openFile(child.path);
      else li.querySelector(":scope > ul")?.classList.toggle("collapsed");
    });
    li.addEventListener("contextmenu", (e) => showCtxMenu(e, child));
    ul.appendChild(li);
    if (child.type === "dir" && child.children.size) {
      const sub = renderTree(child, depth + 1);
      li.appendChild(sub);
    }
  }
  return ul;
}

/* ---------- tabs ---------- */
function renderTabs() {
  const strip = $("#tabstrip");
  strip.innerHTML = "";
  for (const p of state.openTabs) {
    const t = document.createElement("div");
    t.className = "tab" + (p === state.current ? " active" : "") + (state.dirty.has(p) ? " dirty" : "");
    t.dataset.path = p;
    const name = document.createElement("span");
    name.textContent = p.split("/").pop();
    const close = document.createElement("span");
    close.className = "close";
    close.addEventListener("click", (e) => {
      e.stopPropagation();
      closeTab(p);
    });
    t.append(name, close);
    t.addEventListener("click", () => openFile(p));
    strip.appendChild(t);
  }

  // breadcrumbs
  const bc = $("#breadcrumbs");
  bc.innerHTML = "";
  if (state.current) {
    state.current.split("/").forEach((part, i, arr) => {
      const span = document.createElement("span");
      span.textContent = part;
      bc.appendChild(span);
      if (i < arr.length - 1) {
        const sep = document.createElement("span");
        sep.textContent = "›";
        bc.appendChild(sep);
      }
    });
  }
}

async function openFile(path) {
  let model = state.models.get(path);
  if (!model) {
    const content = await state.vfs.readFile(path);
    model = monaco.editor.createModel(content ?? "", languageByExt(path));
    state.models.set(path, model);
    model.onDidChangeContent(() => {
      if (!state.dirty.has(path)) {
        state.dirty.add(path);
        renderTabs();
        refreshTree();
      }
    });
  }
  state.editor.setModel(model);
  state.current = path;
  if (!state.openTabs.includes(path)) state.openTabs.push(path);
  $("#sbLang").textContent = model.getLanguageId();
  renderTabs();
  refreshTree();
}

async function closeTab(path) {
  if (state.dirty.has(path)) {
    if (!confirm(`Close '${path}' without saving?`)) return;
    state.dirty.delete(path);
  }
  const idx = state.openTabs.indexOf(path);
  state.openTabs.splice(idx, 1);
  if (state.current === path) {
    state.current = state.openTabs[idx] || state.openTabs[idx - 1] || null;
    if (state.current) state.editor.setModel(state.models.get(state.current));
    else state.editor.setModel(null);
  }
  renderTabs();
  refreshTree();
}

/* ---------- save ---------- */
async function saveCurrent() {
  if (!state.current) return;
  const content = state.models.get(state.current).getValue();
  await state.vfs.writeFile(state.current, content);
  if (state.backend.online) await state.backend.writeFile(state.current, content).catch(() => {});
  state.dirty.delete(state.current);
  renderTabs();
  refreshTree();
  toast(`Saved ${state.current}`, "ok");
}

async function saveAll() {
  for (const p of state.dirty) {
    const c = state.models.get(p).getValue();
    await state.vfs.writeFile(p, c);
    if (state.backend.online) await state.backend.writeFile(p, c).catch(() => {});
  }
  state.dirty.clear();
  renderTabs();
  refreshTree();
  toast("All files saved", "ok");
}

/* ---------- run ---------- */
async function runCurrent() {
  if (!state.current) return;
  const lang = pickLanguage();
  await saveCurrent();
  setPanelTab(lang === "html" ? "preview" : "terminal");

  if (lang === "html") {
    const html = state.models.get(state.current).getValue();
    const blob = new Blob([html], { type: "text/html" });
    $("#previewFrame").src = URL.createObjectURL(blob);
    return;
  }

  if (lang === "js-browser") {
    try {
      const fn = new Function(state.models.get(state.current).getValue());
      printOut(`▶ Running ${state.current} (browser JS)\n`);
      const origLog = console.log;
      console.log = (...a) => { printOut(a.map(String).join(" ") + "\n"); origLog(...a); };
      fn();
      console.log = origLog;
    } catch (err) {
      printOut(`✖ ${err}\n`, true);
    }
    return;
  }

  // Backend execution
  if (!state.backend.online) {
    toast("No backend: start the Stacks IDE daemon to run native code", "err");
    printOut("✖ Run requires the Stacks IDE backend on http://127.0.0.1:17890\n", true);
    return;
  }

  printOut(`▶ Running ${state.current} as ${lang}\n`);
  const args = $("#runArgs").value.trim();
  await state.backend.run({
    path: state.current,
    language: lang,
    args,
    onOutput: (chunk, stream) => printOut(chunk, stream === "stderr"),
    onExit: (code) => printOut(`\n[process exited with code ${code}]\n`, code !== 0),
  });
}

function pickLanguage() {
  const sel = $("#runLang").value;
  if (sel !== "auto") return sel;
  const ext = state.current.split(".").pop().toLowerCase();
  return ({
    py: "python", js: "js-browser", mjs: "js-browser",
    html: "html", htm: "html",
    cpp: "cpp", cc: "cpp", cxx: "cpp",
    c: "c",
    java: "java",
    sh: "shell", bash: "shell",
    rs: "rust", go: "go",
  }[ext]) || "shell";
}

function setPanelTab(name) {
  $$(".panel-tab").forEach((b) => b.classList.toggle("active", b.dataset.tab === name));
  $$(".panel-view").forEach((v) => v.classList.toggle("active", v.dataset.view === name));
  if (name === "terminal" && state.termFit) setTimeout(() => state.termFit.fit(), 50);
}
function printOut(text, isErr = false) {
  const out = $("#output");
  const span = document.createElement("span");
  span.textContent = text;
  if (isErr) span.style.color = "#f48771";
  out.appendChild(span);
  out.scrollTop = out.scrollHeight;
  if (!$(".panel-tab.active")?.dataset.tab?.match(/output|terminal/)) setPanelTab("output");
}

/* ---------- terminal ---------- */
function initTerminal() {
  const Term = window.Terminal;
  const FitAddon = window.FitAddon?.FitAddon;
  if (!Term || !FitAddon) {
    // xterm not loaded — show a fallback
    $("#terminal").innerHTML = '<div style="padding:12px;color:var(--fg-3);font-family:var(--font-mono)">Terminal unavailable (xterm.js failed to load).</div>';
    return;
  }
  state.term = new Term({
    fontFamily: '"Cascadia Code", Consolas, monospace',
    fontSize: 13,
    theme: termTheme(),
    cursorBlink: true,
    scrollback: 5000,
  });
  state.termFit = new FitAddon();
  state.term.loadAddon(state.termFit);
  state.term.open($("#terminal"));
  state.termFit.fit();
  state.term.writeln("\x1b[1;36mStacks IDE Terminal\x1b[0m");
  state.term.writeln("Type 'help' or connect the native backend to get a real shell.\n");
  promptOffline();

  let buf = "";
  state.term.onData((data) => {
    if (state.ws && state.ws.readyState === 1) {
      state.ws.send(JSON.stringify({ type: "stdin", data }));
      return;
    }
    // offline echo shell
    if (data === "\r") {
      state.term.write("\r\n");
      handleOfflineCmd(buf.trim());
      buf = "";
      promptOffline();
    } else if (data === "\u007F") {
      if (buf.length) { buf = buf.slice(0, -1); state.term.write("\b \b"); }
    } else {
      buf += data; state.term.write(data);
    }
  });

  new ResizeObserver(() => { try { state.termFit.fit(); } catch {} }).observe($("#terminal"));
}

function promptOffline() { state.term.write("\x1b[1;32mstacks\x1b[0m $ "); }
function handleOfflineCmd(cmd) {
  if (!cmd) return;
  const [c, ...rest] = cmd.split(/\s+/);
  switch (c) {
    case "help": state.term.writeln("Commands: help, clear, ls, cat <file>, date, echo <msg>"); break;
    case "clear": state.term.clear(); break;
    case "date": state.term.writeln(new Date().toString()); break;
    case "echo": state.term.writeln(rest.join(" ")); break;
    case "ls":
      state.vfs.listAll().then((all) => {
        all.filter((e) => e.type === "file").forEach((e) => state.term.writeln(e.path));
        promptOffline();
      });
      return;
    case "cat":
      state.vfs.readFile(rest.join(" ")).then((c) => {
        state.term.writeln(c ?? `cat: ${rest.join(" ")}: no such file`);
        promptOffline();
      });
      return;
    default: state.term.writeln(`${c}: command not found (connect the backend for a real shell)`);
  }
}

/* ---------- backend connect ---------- */
async function connectBackend() {
  const ok = await state.backend.ping();
  const sb = $("#sbBackend");
  const bar = $("#statusbar");
  if (ok) {
    sb.textContent = `◉ Backend ${state.backend.host}`;
    bar.classList.remove("offline");
    await probeToolchains();
    openPty();
  } else {
    sb.textContent = "◉ Offline mode";
    bar.classList.add("offline");
    $("#tcList").innerHTML = `<li class="missing">Backend not connected</li>`;
  }
}

async function probeToolchains() {
  const ul = $("#tcList");
  ul.innerHTML = "";
  const tools = await state.backend.toolchains().catch(() => ({}));
  const rows = [
    ["Python", tools.python],
    ["Node.js", tools.node],
    ["g++ / clang++", tools.cpp],
    ["gcc / clang", tools.c],
    ["Java", tools.java],
    ["Shell", tools.shell],
  ];
  for (const [name, ok] of rows) {
    const li = document.createElement("li");
    li.className = ok ? "ok" : "missing";
    li.textContent = name;
    ul.appendChild(li);
  }
}

function openPty() {
  try {
    state.ws = new WebSocket(`ws://${state.backend.host}/ws/pty`);
    state.ws.onopen = () => state.term.writeln("\x1b[1;32m✓ PTY connected\x1b[0m");
    state.ws.onmessage = (e) => {
      const msg = JSON.parse(e.data);
      if (msg.type === "stdout" || msg.type === "stderr") state.term.write(msg.data);
    };
    state.ws.onclose = () => state.term.writeln("\r\n\x1b[33mPTY closed\x1b[0m");
  } catch {}
}

/* ---------- command palette ---------- */
function buildCommands() {
  state.commands = [
    { id: "file.new", label: "File: New File…", kbd: "Ctrl+N", run: () => openModal("new-file") },
    { id: "file.newFolder", label: "File: New Folder…", run: () => openModal("new-folder") },
    { id: "file.save", label: "File: Save", kbd: "Ctrl+S", run: saveCurrent },
    { id: "file.saveAll", label: "File: Save All", kbd: "Ctrl+K S", run: saveAll },
    { id: "run.file", label: "Run: Run Active File", kbd: "F5", run: runCurrent },
    { id: "view.toggleTheme", label: "View: Toggle Theme", run: () => applyTheme(state.theme === "dark" ? "light" : "dark") },
    { id: "view.togglePanel", label: "View: Toggle Bottom Panel", kbd: "Ctrl+`", run: togglePanel },
    { id: "term.focus", label: "Terminal: Focus", run: () => { setPanelTab("terminal"); state.term?.focus(); } },
    { id: "term.clear", label: "Terminal: Clear", run: () => state.term?.clear() },
  ];
}

function openPalette() {
  const ov = $("#cmdOverlay");
  const inp = $("#cmdInput");
  ov.hidden = false;
  inp.value = "";
  inp.focus();
  renderPalette("");
}
function closePalette() { $("#cmdOverlay").hidden = true; }
function renderPalette(query) {
  const list = $("#cmdList");
  list.innerHTML = "";
  const q = query.toLowerCase().trim();
  // commands
  let items = state.commands.filter((c) => !q || c.label.toLowerCase().includes(q));
  items.forEach((c) => {
    const li = document.createElement("li");
    li.innerHTML = `<span>${c.label}</span><span class="kbd">${c.kbd ?? ""}</span>`;
    li.addEventListener("click", () => { closePalette(); c.run(); });
    list.appendChild(li);
  });
  // files (when prefixed with > or empty)
  state.vfs.listAll().then((all) => {
    const files = all.filter((e) => e.type === "file").map((e) => e.path);
    const matches = files.filter((p) => !q || p.toLowerCase().includes(q)).slice(0, 20);
    matches.forEach((p) => {
      const li = document.createElement("li");
      li.innerHTML = `<span>📄  ${p}</span><span class="kbd">open</span>`;
      li.addEventListener("click", () => { closePalette(); openFile(p); });
      list.appendChild(li);
    });
  });
}

/* ---------- modal ---------- */
function openModal(kind, initial = "") {
  const ov = $("#modalOverlay");
  const title = $("#modalTitle");
  const input = $("#modalInput");
  const extRow = $("#modalExtRow");
  ov.dataset.kind = kind;
  title.textContent = kind === "new-file" ? "New File" : kind === "new-folder" ? "New Folder" : "Rename";
  input.placeholder = kind === "new-folder" ? "src" : "example.py";
  input.value = initial;
  extRow.style.display = kind === "new-file" ? "flex" : "none";
  ov.hidden = false;
  input.focus();
}
function closeModal() { $("#modalOverlay").hidden = true; }

async function modalSubmit() {
  const kind = $("#modalOverlay").dataset.kind;
  const name = $("#modalInput").value.trim();
  if (!name) return;
  if (kind === "new-file") {
    const tpl = $("#modalExt").value;
    const finalName = name.includes(".") || !tpl ? name : `${name}.${tpl}`;
    const content = TEMPLATES.byExt(finalName.split(".").pop()) ?? "";
    await state.vfs.writeFile(finalName, content);
    await refreshTree();
    openFile(finalName);
  } else if (kind === "new-folder") {
    await state.vfs.mkdir(name);
    await refreshTree();
  }
  closeModal();
}

/* ---------- context menu ---------- */
function showCtxMenu(e, node) {
  e.preventDefault();
  const ctx = $("#ctxMenu");
  ctx.innerHTML = "";
  const add = (label, fn) => {
    const b = document.createElement("button");
    b.textContent = label;
    b.onclick = () => { ctx.hidden = true; fn(); };
    ctx.appendChild(b);
  };
  if (node.type === "file") {
    add("Open", () => openFile(node.path));
    add("Rename…", async () => {
      const nn = prompt("New name:", node.name);
      if (!nn || nn === node.name) return;
      const newPath = node.path.replace(/[^/]+$/, nn);
      await state.vfs.rename(node.path, newPath);
      await refreshTree();
    });
    add("Delete", async () => {
      if (!confirm(`Delete ${node.path}?`)) return;
      await state.vfs.deleteFile(node.path);
      closeTab(node.path);
      await refreshTree();
    });
  } else {
    add("New File Here…", () => openModal("new-file", node.path + "/"));
  }
  ctx.style.left = e.clientX + "px";
  ctx.style.top = e.clientY + "px";
  ctx.hidden = false;
}
document.addEventListener("click", () => { $("#ctxMenu").hidden = true; });

/* ---------- toasts ---------- */
function toast(msg, kind = "info") {
  const stack = $("#toastStack");
  const t = document.createElement("div");
  t.className = `toast ${kind}`;
  t.textContent = msg;
  stack.appendChild(t);
  setTimeout(() => t.remove(), 3200);
}

/* ---------- panel ---------- */
function togglePanel() {
  const panel = $("#panel");
  const isHidden = panel.classList.toggle("hidden");
  panel.style.display = isHidden ? "none" : "";
  const area = $(".editor-area");
  area.style.gridTemplateRows = isHidden
    ? "34px 24px 1fr 0 0"
    : "34px 24px 1fr 4px 240px";
  setTimeout(() => state.termFit?.fit(), 80);
}

/* ---------- UI bindings ---------- */
function bindUI() {
  // activity bar
  $$(".act-btn").forEach((b) => {
    b.addEventListener("click", () => {
      $$(".act-btn").forEach((x) => x.classList.remove("active"));
      b.classList.add("active");
      const v = b.dataset.view;
      $("#sideTitle").textContent = { explorer: "Explorer", search: "Search", run: "Run & Debug", git: "Source Control", ext: "Extensions", account: "Account" }[v] ?? "Explorer";
      $$(".view").forEach((x) => x.classList.toggle("active", x.classList.contains(`view-${v}`)));
    });
  });

  // top buttons
  $("#newFileBtn").onclick = () => openModal("new-file");
  $("#newFolderBtn").onclick = () => openModal("new-folder");
  $("#refreshBtn").onclick = refreshTree;
  $("#themeBtn").onclick = () => applyTheme(state.theme === "dark" ? "light" : "dark");
  $("#runBtn").onclick = runCurrent;
  $("#stopBtn").onclick = () => state.backend.stop();
  $("#cmdTrigger").onclick = openPalette;
  $("#clearPanelBtn").onclick = () => {
    const tab = $(".panel-tab.active")?.dataset.tab;
    if (tab === "terminal") state.term?.clear();
    if (tab === "output") $("#output").textContent = "";
  };
  $("#togglePanelBtn").onclick = togglePanel;

  // panel tabs
  $$(".panel-tab").forEach((b) => b.addEventListener("click", () => setPanelTab(b.dataset.tab)));

  // modal
  $("#modalOk").onclick = modalSubmit;
  $("#modalCancel").onclick = closeModal;
  $("#modalInput").addEventListener("keydown", (e) => {
    if (e.key === "Enter") modalSubmit();
    if (e.key === "Escape") closeModal();
  });

  // palette
  $("#cmdInput").addEventListener("input", (e) => renderPalette(e.target.value));
  $("#cmdInput").addEventListener("keydown", (e) => {
    if (e.key === "Escape") closePalette();
    if (e.key === "Enter") {
      const first = $("#cmdList li");
      first?.click();
    }
  });
  $("#cmdOverlay").addEventListener("click", (e) => {
    if (e.target.id === "cmdOverlay") closePalette();
  });

  // shortcuts
  document.addEventListener("keydown", (e) => {
    const mod = e.ctrlKey || e.metaKey;
    if (mod && e.shiftKey && e.key.toLowerCase() === "p") { e.preventDefault(); openPalette(); }
    else if (mod && e.key.toLowerCase() === "p") { e.preventDefault(); openPalette(); }
    else if (mod && e.key.toLowerCase() === "s") { e.preventDefault(); saveCurrent(); }
    else if (mod && e.key.toLowerCase() === "n") { e.preventDefault(); openModal("new-file"); }
    else if (e.key === "F5") { e.preventDefault(); runCurrent(); }
    else if (mod && e.key === "`") { e.preventDefault(); togglePanel(); }
  });

  // Menu items (basic)
  $$(".menu-item").forEach((m) => m.addEventListener("click", openPalette));

  // Window controls (best-effort; real impl is in native shell)
  $("#closeBtn").onclick = () => window.close?.();
  $("#minBtn").onclick = () => window.chrome?.webview?.postMessage?.("min");
  $("#maxBtn").onclick = () => window.chrome?.webview?.postMessage?.("max");
}
