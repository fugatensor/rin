#!/usr/bin/env node
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const { spawn } = require('node:child_process');
const { fileURLToPath, pathToFileURL } = require('node:url');
const { RpcConnection } = require('../lib/rpc');
const { completionItems } = require('./symbols');

const connection = new RpcConnection(process.stdin, process.stdout);
const documents = new Map();
const jobs = new Map();
const revisions = new Map();
const reports = new Map();
let rootDir = process.cwd();
let settings = { executable: 'suka', diagnostics: { enable: true } };
let initialized = false;
let shutdown = false;

function diagnostic(message, severity = 1, line = 0, length = 1) {
  return { range: { start: { line, character: 0 }, end: { line, character: Math.max(1, length) } },
    severity, source: 'suka', message };
}

function updateReports(owner, report) {
  const affected = new Set([...(reports.get(owner) || new Map()).keys(), ...report.keys()]);
  if (report.size) reports.set(owner, report); else reports.delete(owner);
  for (const uri of affected) {
    const diagnostics = [];
    const seen = new Set();
    for (const contribution of reports.values()) {
      for (const item of contribution.get(uri) || []) {
        const key = JSON.stringify(item);
        if (!seen.has(key)) { seen.add(key); diagnostics.push(item); }
      }
    }
    const params = { uri, diagnostics };
    if (documents.has(uri)) params.version = documents.get(uri).version;
    connection.notify('textDocument/publishDiagnostics', params);
  }
}

function cancel(uri) {
  const job = jobs.get(uri);
  if (job) { clearTimeout(job.timer); if (job.child) job.child.kill(); jobs.delete(uri); }
}

function baseDir(uri) {
  try { if (uri.startsWith('file:')) return path.dirname(fileURLToPath(uri)); } catch {}
  return rootDir;
}

function parseDiagnostics(stderr, document, directory) {
  const report = new Map([[document.uri, []]]);
  for (const line of stderr.split(/\r?\n/)) {
    const match = /^(.*):(\d+):\s*(.*)$/.exec(line);
    if (!match) continue;
    let uri = document.uri;
    if (!['<source>', '<stdin>', '-'].includes(match[1])) {
      try { uri = pathToFileURL(path.resolve(directory, match[1])).href; } catch {}
    }
    let text = documents.get(uri)?.text || '';
    if (!text && uri !== document.uri) {
      try { text = fs.readFileSync(fileURLToPath(uri), 'utf8'); } catch {}
    }
    const lines = text.split(/\r?\n/);
    const lineNumber = Math.max(0, Math.min(Number(match[2]) - 1, lines.length - 1));
    if (!report.has(uri)) report.set(uri, []);
    report.get(uri).push(diagnostic(match[3], 1, lineNumber, (lines[lineNumber] || '').length));
  }
  return report;
}

function validate(uri, revision) {
  const document = documents.get(uri);
  if (!document || revisions.get(uri) !== revision || shutdown) return;
  if (!settings.diagnostics.enable) { updateReports(uri, new Map([[uri, []]])); return; }
  const directory = baseDir(uri);
  const child = spawn(settings.executable, ['--check', '--stdin', '--base-dir', directory],
    { cwd: rootDir, shell: false, windowsHide: true, stdio: ['pipe', 'ignore', 'pipe'] });
  const job = { child, timer: undefined };
  jobs.set(uri, job);
  let stderr = '';
  let finished = false;
  const finish = (code, message) => {
    if (finished) return;
    finished = true;
    clearTimeout(job.timer);
    if (jobs.get(uri) === job) jobs.delete(uri);
    if (revisions.get(uri) !== revision || !documents.has(uri) || shutdown) return;
    const report = parseDiagnostics(stderr, document, directory);
    if (message) report.get(uri).push(diagnostic(message, 2));
    else if (code !== 0 && [...report.values()].every(items => items.length === 0))
      report.get(uri).push(diagnostic(stderr.trim() || `Suka checker exited with status ${code}`));
    updateReports(uri, report);
  };
  child.on('error', error => finish(null,
    `Cannot run Suka checker: ${error.message}. Configure suka.executable.`));
  child.stderr.setEncoding('utf8');
  child.stderr.on('data', chunk => {
    stderr += chunk.toString('utf8');
    if (stderr.length > 1024 * 1024) {
      child.kill();
      finish(null, 'Suka checker produced too much diagnostic output.');
    }
  });
  child.stdin.on('error', () => {}); // The checker may exit after an early parse error.
  child.on('close', code => finish(code));
  job.timer = setTimeout(() => { child.kill(); finish(null, 'Suka syntax check timed out.'); }, 5000);
  child.stdin.end(document.text, 'utf8');
}

function schedule(uri) {
  cancel(uri);
  const revision = (revisions.get(uri) || 0) + 1;
  revisions.set(uri, revision);
  jobs.set(uri, { timer: setTimeout(() => validate(uri, revision), 120) });
}
function scheduleAll() { for (const uri of documents.keys()) schedule(uri); }
function configure(options = {}) {
  if (typeof options.executable === 'string' && options.executable.trim()) {
    const executable = options.executable.trim();
    settings.executable = !path.isAbsolute(executable) && /[\\/]/.test(executable)
      ? path.resolve(rootDir, executable) : executable;
  }
  if (typeof options.diagnostics?.enable === 'boolean') settings.diagnostics.enable = options.diagnostics.enable;
}
function stop() { for (const uri of jobs.keys()) cancel(uri); }

connection.on('error', error => { process.stderr.write(`${error.message}\n`); stop(); process.exitCode = 1; process.stdin.destroy(); });
connection.on('close', () => { stop(); process.exit(shutdown ? 0 : 1); });
connection.on('message', message => {
  const { method, params = {}, id } = message;
  try {
    if (method === 'initialize') {
      if (initialized) { connection.error(id, -32600, 'Already initialized'); return; }
      const rootUri = params.rootUri || params.workspaceFolders?.[0]?.uri;
      if (rootUri?.startsWith('file:')) rootDir = fileURLToPath(rootUri);
      configure(params.initializationOptions);
      initialized = true;
      connection.respond(id, { capabilities: { positionEncoding: 'utf-16',
        textDocumentSync: { openClose: true, change: 1, save: { includeText: false } },
        completionProvider: { triggerCharacters: [':', '('], resolveProvider: false } },
        serverInfo: { name: 'suka-language-server', version: '0.1.1' } });
      return;
    }
    if (method === 'exit') { stop(); process.exit(shutdown ? 0 : 1); }
    if (!initialized || shutdown) {
      if (id !== undefined) connection.error(id, -32002, 'Server is not initialized or has shut down');
      return;
    }
    switch (method) {
      case 'initialized': break;
      case 'shutdown': shutdown = true; stop(); connection.respond(id, null); break;
      case 'textDocument/didOpen': {
        const document = params.textDocument;
        documents.set(document.uri, { uri: document.uri, text: document.text, version: document.version });
        scheduleAll();
        break;
      }
      case 'textDocument/didChange': {
        const document = documents.get(params.textDocument.uri);
        if (!document) break;
        if (params.textDocument.version <= document.version) break;
        for (const change of params.contentChanges || []) {
          if (change.range) throw new Error('This server negotiated full document synchronization');
          document.text = change.text;
        }
        document.version = params.textDocument.version;
        scheduleAll();
        break;
      }
      case 'textDocument/didSave': scheduleAll(); break;
      case 'workspace/didChangeWatchedFiles': scheduleAll(); break;
      case 'textDocument/didClose': {
        const uri = params.textDocument.uri;
        revisions.set(uri, (revisions.get(uri) || 0) + 1);
        cancel(uri); documents.delete(uri); updateReports(uri, new Map()); scheduleAll();
        break;
      }
      case 'workspace/didChangeConfiguration': configure(params.settings?.suka || params.settings); scheduleAll(); break;
      case 'textDocument/completion': {
        const document = documents.get(params.textDocument.uri);
        connection.respond(id, { isIncomplete: false,
          items: document ? completionItems(document, params.position, documents, rootDir) : [] });
        break;
      }
      case '$/cancelRequest': break;
      default: if (id !== undefined) connection.error(id, -32601, `Method not found: ${method}`);
    }
  } catch (error) {
    if (id !== undefined) connection.error(id, -32602, error.message);
    else connection.notify('window/logMessage', { type: 1, message: error.message });
  }
});
