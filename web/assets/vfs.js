/* Very small IndexedDB-backed virtual file system used when no backend is connected. */

const DB_NAME = "stacks-ide";
const STORE = "files";

export class VFS {
  constructor() { this.db = null; }

  open() {
    return new Promise((resolve, reject) => {
      const req = indexedDB.open(DB_NAME, 1);
      req.onupgradeneeded = () => {
        const db = req.result;
        if (!db.objectStoreNames.contains(STORE))
          db.createObjectStore(STORE, { keyPath: "path" });
      };
      req.onsuccess = () => { this.db = req.result; resolve(); };
      req.onerror = () => reject(req.error);
    });
  }

  _tx(mode) {
    return this.db.transaction(STORE, mode).objectStore(STORE);
  }

  listAll() {
    return new Promise((res) => {
      const out = [];
      const c = this._tx("readonly").openCursor();
      c.onsuccess = () => {
        const cur = c.result;
        if (!cur) return res(out);
        out.push(cur.value);
        cur.continue();
      };
    });
  }

  readFile(path) {
    return new Promise((res) => {
      const r = this._tx("readonly").get(path);
      r.onsuccess = () => res(r.result?.content ?? null);
    });
  }

  writeFile(path, content) {
    return new Promise((res) => {
      // make parent dirs implicitly
      this._tx("readwrite").put({ path, type: "file", content, mtime: Date.now() }).onsuccess = () => res();
    });
  }

  mkdir(path) {
    return new Promise((res) => {
      this._tx("readwrite").put({ path, type: "dir", mtime: Date.now() }).onsuccess = () => res();
    });
  }

  deleteFile(path) {
    return new Promise((res) => {
      this._tx("readwrite").delete(path).onsuccess = () => res();
    });
  }

  async rename(oldPath, newPath) {
    const content = await this.readFile(oldPath);
    await this.writeFile(newPath, content ?? "");
    await this.deleteFile(oldPath);
  }
}
