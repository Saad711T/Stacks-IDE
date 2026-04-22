/* Talks to the native Stacks IDE daemon (core/) over HTTP + WebSocket. */

const DEFAULT_HOST = "127.0.0.1:17890";

export class Backend {
  constructor(host = DEFAULT_HOST) {
    this.host = host;
    this.online = false;
    this.token = localStorage.getItem("stacks.token") || "";
    this._runSocket = null;
  }

  url(path) { return `http://${this.host}${path}`; }

  async ping() {
    try {
      const r = await fetch(this.url("/api/health"), { method: "GET" });
      if (!r.ok) throw new Error("bad status");
      const j = await r.json();
      this.online = !!j.ok;
      if (j.token && !this.token) { this.token = j.token; localStorage.setItem("stacks.token", this.token); }
      return this.online;
    } catch (e) {
      this.online = false;
      return false;
    }
  }

  _headers() {
    return { "Content-Type": "application/json", "X-Stacks-Token": this.token || "" };
  }

  async toolchains() {
    const r = await fetch(this.url("/api/toolchains"), { headers: this._headers() });
    return r.json();
  }

  async writeFile(path, content) {
    const r = await fetch(this.url("/api/fs/write"), {
      method: "POST",
      headers: this._headers(),
      body: JSON.stringify({ path, content }),
    });
    return r.ok;
  }

  async readFile(path) {
    const r = await fetch(this.url(`/api/fs/read?path=${encodeURIComponent(path)}`), {
      headers: this._headers(),
    });
    if (!r.ok) return null;
    const j = await r.json();
    return j.content;
  }

  async run({ path, language, args, onOutput, onExit }) {
    return new Promise((resolve) => {
      try { this._runSocket?.close(); } catch {}
      const ws = new WebSocket(`ws://${this.host}/ws/run`);
      this._runSocket = ws;
      ws.onopen = () => ws.send(JSON.stringify({ path, language, args, token: this.token }));
      ws.onmessage = (e) => {
        const msg = JSON.parse(e.data);
        if (msg.type === "stdout" || msg.type === "stderr") onOutput?.(msg.data, msg.type);
        else if (msg.type === "exit") { onExit?.(msg.code); ws.close(); resolve(msg.code); }
      };
      ws.onerror = () => { onOutput?.("\nbackend error\n", "stderr"); resolve(-1); };
      ws.onclose = () => resolve(0);
    });
  }

  stop() {
    try { this._runSocket?.close(); } catch {}
  }
}
