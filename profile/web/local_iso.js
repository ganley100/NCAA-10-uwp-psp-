/* Local ISO9660 disc access. The File stays in the browser; only requested
 * ranges enter the runtime's existing bounded disc cache. */
(function (root) {
  'use strict';
  const SECTOR = 2048;
  const text = new TextDecoder('utf-8', { fatal: true });
  function fail(message) { throw new Error(message); }
  function integer(value) { return Number.isSafeInteger(value) && value >= 0; }
  function sfo(bytes) {
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    if (bytes.length < 20 || view.getUint32(0, true) !== 0x46535000) fail('Invalid PARAM.SFO in this ISO.');
    const keys = view.getUint32(8, true), values = view.getUint32(12, true), count = view.getUint32(16, true);
    if (count > 1024 || 20 + count * 16 > keys || keys > values || values > bytes.length) fail('Truncated PARAM.SFO.');
    const result = {};
    for (let i = 0; i < count; ++i) {
      const at = 20 + i * 16, key = keys + view.getUint16(at, true);
      const format = view.getUint16(at + 2, true), length = view.getUint32(at + 4, true);
      const start = values + view.getUint32(at + 12, true), stop = start + length;
      if (key >= values || stop > bytes.length) fail('Invalid PARAM.SFO field.');
      const keyEnd = bytes.indexOf(0, key);
      if (keyEnd < key || keyEnd >= values) fail('Invalid PARAM.SFO key.');
      if (format === 0x204) {
        result[text.decode(bytes.subarray(key, keyEnd))] = text.decode(bytes.subarray(start, stop)).replace(/\0+$/, '');
      }
    }
    return result;
  }
  async function sha256(bytes) {
    const digest = new Uint8Array(await root.crypto.subtle.digest('SHA-256', bytes));
    return Array.from(digest, value => value.toString(16).padStart(2, '0')).join('');
  }
  class IsoDisc {
    constructor(file) {
      this.file = file;
      this.entries = new Map();
      this.stats = { indexBytes: 0, gameBytes: 0, gameReads: 0, largestGameRead: 0 };
    }
    async bytes(start, length, game = false) {
      if (!integer(start) || !integer(length) || start + length > this.file.size) fail('ISO read is outside the selected file.');
      const result = new Uint8Array(await this.file.slice(start, start + length).arrayBuffer());
      if (result.length !== length) fail('The selected ISO is truncated or no longer readable.');
      if (game) {
        this.stats.gameBytes += length;
        ++this.stats.gameReads;
        this.stats.largestGameRead = Math.max(this.stats.largestGameRead, length);
      } else this.stats.indexBytes += length;
      return result;
    }
    static async index(file, progress = () => {}) {
      if (!file || !integer(file.size) || file.size < 17 * SECTOR || typeof file.slice !== 'function') fail('Choose an uncompressed PSP ISO file.');
      const disc = new IsoDisc(file), pvd = await disc.bytes(16 * SECTOR, SECTOR);
      progress('Reading disc index…');
      const header = new DataView(pvd.buffer);
      if (pvd[0] !== 1 || text.decode(pvd.subarray(1, 6)) !== 'CD001' || pvd[6] !== 1 || header.getUint16(128, true) !== SECTOR) {
        fail('Choose an uncompressed ISO9660 image. ZIP, CSO and ZSO files are not supported here.');
      }
      const record = (bytes, at, parent) => {
        const n = bytes[at], nameLength = bytes[at + 32];
        if (n < 34 || at + n > bytes.length || at % SECTOR + n > SECTOR || nameLength === 0 || 33 + nameLength > n) fail('Invalid ISO directory record.');
        const view = new DataView(bytes.buffer, bytes.byteOffset + at, n);
        const offset = view.getUint32(2, true) * SECTOR, size = view.getUint32(10, true), flags = bytes[at + 25];
        if (offset + size > file.size || (flags & 128) || bytes[at + 1] || bytes[at + 26] || bytes[at + 27]) fail('Truncated or unsupported multi-extent/interleaved ISO.');
        const rawName = bytes.subarray(at + 33, at + 33 + nameLength);
        if (nameLength === 1 && rawName[0] <= 1) return null;
        if (Array.from(rawName).some(c => c < 32 || c > 126)) fail('Unsupported ISO filename.');
        const name = text.decode(rawName).replace(/;\d+$/, '');
        if (!name || /[\\/:\r\n]/.test(name) || name === '.' || name === '..') fail('Invalid ISO path.');
        return { path: parent ? parent + '/' + name : name, offset, size, directory: !!(flags & 2) };
      };
      if (pvd[156] < 34 || !(pvd[181] & 2)) fail('Invalid ISO root directory.');
      const queue = [{ path: '', offset: header.getUint32(158, true) * SECTOR, size: header.getUint32(166, true), depth: 0 }];
      const visited = new Set();
      for (let q = 0; q < queue.length; ++q) {
        const directory = queue[q];
        if (directory.depth > 16 || directory.size > 16 * 1024 * 1024 || disc.stats.indexBytes + directory.size > 32 * 1024 * 1024 || visited.has(directory.offset)) fail('Invalid or excessively large ISO directory tree.');
        visited.add(directory.offset);
        const bytes = await disc.bytes(directory.offset, directory.size);
        for (let at = 0; at < bytes.length;) {
          if (bytes[at] === 0) { at = (Math.floor(at / SECTOR) + 1) * SECTOR; continue; }
          const entry = record(bytes, at, directory.path);
          at += bytes[at];
          if (!entry) continue;
          const key = entry.path.toLowerCase();
          if (disc.entries.has(key) || disc.entries.size >= 100000) fail('Duplicate or excessive ISO entries.');
          disc.entries.set(key, entry);
          if (entry.directory) queue.push({ ...entry, depth: directory.depth + 1 });
        }
      }
      disc.entries.set('__umd.iso', { path: '__umd.iso', offset: 0, size: file.size, directory: false });
      return disc;
    }
    async verify(config, progress = () => {}) {
      const metadata = this.entries.get('psp_game/param.sfo'), executable = this.entries.get('psp_game/sysdir/eboot.bin');
      if (!metadata || metadata.directory || metadata.size > 1024 * 1024 || !executable || executable.directory || executable.size > 32 * 1024 * 1024) fail('This ISO is missing the NCAA PSP disc metadata or executable.');
      progress('Checking NCAA title…');
      const info = sfo(await this.bytes(metadata.offset, metadata.size));
      if (info.DISC_ID !== 'ULUS10440' || !/FOOTBALL 10/i.test(info.TITLE || '')) fail('This build requires NCAA Football 10 PSP US (ULUS10440).');
      progress('Checking executable revision…');
      const hash = await sha256(await this.bytes(executable.offset, executable.size));
      if (!Array.isArray(config.ebootHashes) || !config.ebootHashes.includes(hash)) fail('This executable revision does not match the NCAA recomp build. Choose the matching US ISO.');
      this.identity = { title: info.TITLE, discId: info.DISC_ID, ebootSha256: hash };
      return this;
    }
    manifest() {
      return Array.from(this.entries.values()).sort((a, b) => a.path.localeCompare(b.path))
        .map(entry => entry.directory ? 'D ' + entry.path : 'F ' + entry.size + ' ' + entry.path).join('\n') + '\n';
    }
    async read(url, start, end) {
      if (!url.startsWith('disc/')) fail('Invalid local disc request.');
      const entry = this.entries.get(url.slice(5).toLowerCase());
      if (!entry || entry.directory || !integer(start) || !integer(end) || end < start || end >= entry.size) fail('Invalid range in local disc request: ' + url);
      return (await this.bytes(entry.offset + start, end - start + 1, true)).buffer;
    }
  }
  // Shared by the lightweight launcher and Emscripten preRun. Retain the File
  // only in this page, so choosing once never copies the ISO into site storage.
  const selections = new WeakMap();
  function prepare({ document, config }) {
    if (selections.has(document)) return selections.get(document);
    const panel = document.getElementById('disc-loader'), input = document.getElementById('disc-file');
    const message = document.getElementById('disc-message'), status = document.getElementById('status');
    let complete;
    const ready = new Promise(resolve => { complete = resolve; });
    selections.set(document, ready);
    panel.hidden = false;
    input.disabled = false;
    status.textContent = 'Choose your NCAA ISO';
    status.className = 'status';
    message.textContent = 'Select NCAA Football 10 PSP US (ULUS10440). The ISO stays on your device.';
    let busy = false, started = false;
    input.addEventListener('change', async function () {
      const file = input.files && input.files[0];
      if (!file || busy || started) return;
      busy = true;
      input.disabled = true;
      status.textContent = 'Checking local NCAA ISO…';
      status.className = 'status';
      message.textContent = 'Checking ' + file.name + '…';
      message.className = '';
      try {
        const progress = text => { message.textContent = file.name + ' · ' + text; };
        const disc = await (await IsoDisc.index(file, progress)).verify(config, progress);
        message.textContent = file.name + ' · ' + disc.identity.discId + ' · loaded locally';
        status.textContent = 'Loading NCAA Football 10 engine…';
        started = true;
        console.log('[local-iso] ' + JSON.stringify(disc.identity) + ', entries=' + disc.entries.size);
        complete(disc);
      } catch (error) {
        message.textContent = error.message;
        message.className = 'disc-error';
        status.textContent = 'Choose a matching NCAA ISO';
        input.disabled = false;
        input.value = '';
      } finally { busy = false; }
    });
    return ready;
  }
  function install(options) {
    const { Module, FS, addRunDependency, removeRunDependency, document } = options;
    addRunDependency('local-iso');
    return prepare(options).then(disc => {
      FS.mkdirTree('/game');
      FS.writeFile('/game/local-disc.manifest', disc.manifest());
      Module.pspReadDisc = disc.read.bind(disc);
      Module.pspLocalDisc = disc;
      document.getElementById('status').textContent = 'Starting NCAA Football 10…';
      removeRunDependency('local-iso');
    }).catch(error => {
      // Keep startup gated if the manifest could not be installed.
      const status = document.getElementById('status');
      status.textContent = 'Could not mount the local ISO. Reload to retry. ' + error.message;
      status.className = 'status err';
    });
  }
  root.PspLocalIso = { IsoDisc, sfo, sha256, prepare, install };
})(globalThis);
