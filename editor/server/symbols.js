'use strict';
const fs = require('node:fs');
const path = require('node:path');
const { fileURLToPath, pathToFileURL } = require('node:url');

const KEYWORDS = ['all', 'exists', 'verify', 'import'];
const OPERATORS = ['=>', '->', ':', '&'];
const NAME = '[^\\s:,()=|&#"<>]+';

function validModule(name) {
  return name && !name.startsWith('.') && !name.endsWith('.suka') &&
    !/[\s"':,=()|&\\/]/u.test(name) && !name.includes('->');
}

function documentDirectory(document, rootDir) {
  try { if (document.uri.startsWith('file:')) return path.dirname(fileURLToPath(document.uri)); } catch {}
  return rootDir;
}

function sameDirectory(filename, directory) {
  try { return fs.statSync(filename).isFile() && path.dirname(fs.realpathSync(filename)) === fs.realpathSync(directory); }
  catch { return false; }
}

function withoutComment(line) {
  let quoted = false;
  for (let i = 0; i < line.length; i++) {
    if (line[i] === '"' && line[i - 1] !== '\\') quoted = !quoted;
    if (line[i] === '#' && !quoted) return line.slice(0, i);
  }
  return line;
}

function indexText(text, symbols, origin) {
  const put = (name, kind, detail, priority) => {
    if (!name || name === '-' || KEYWORDS.includes(name) || OPERATORS.includes(name)) return;
    if (!symbols.has(name) || symbols.get(name).priority < priority)
      symbols.set(name, { label: name, kind, detail: `${detail} · ${origin}`, priority });
  };
  for (let line of text.split(/\r?\n/)) {
    line = withoutComment(line).trim();
    if (/^import\s/.test(line) || /^type\s+[^\s=]+\s*=/u.test(line)) continue;
    // A verify label is not a type or an object.
    line = line.replace(/^verify\s+[^=]+=\s*/, '');
    for (const match of line.matchAll(new RegExp(NAME, 'gu')))
      put(match[0], 6, '对象或变量', 1);
    for (const match of line.matchAll(new RegExp(`(${NAME})\\s*\\(`, 'gu')))
      put(match[1], 3, '关系或构造项', 2);
    for (const match of line.matchAll(new RegExp(`:\\s*(${NAME})`, 'gu')))
      put(match[1], 7, '类型', 3);
  }
}

function collectSymbols(document, documents, rootDir) {
  const symbols = new Map();
  const seen = new Set();
  let bytes = 0;
  const visit = (uri, text, directory, label) => {
    if (seen.has(uri) || seen.size >= 256 || bytes > 2 * 1024 * 1024) return;
    seen.add(uri);
    bytes += Buffer.byteLength(text, 'utf8');
    indexText(text, symbols, label);
    for (const raw of text.split(/\r?\n/)) {
      const match = /^\s*import\s+(\S+)\s*$/.exec(withoutComment(raw));
      if (!match || !validModule(match[1])) continue;
      const filename = path.resolve(directory, match[1] + '.suka');
      const importedUri = pathToFileURL(filename).href;
      if (seen.has(importedUri)) continue;
      try {
        if (fs.existsSync(filename) && !sameDirectory(filename, directory)) continue;
        const open = documents.get(importedUri);
        if (open) visit(importedUri, open.text, path.dirname(filename), match[1]);
        else if (fs.statSync(filename).isFile() && fs.statSync(filename).size <= 512 * 1024)
          visit(importedUri, fs.readFileSync(filename, 'utf8'), path.dirname(filename), match[1]);
      } catch { /* An incomplete or missing import is diagnosed by the compiler. */ }
    }
  };
  let directory = rootDir;
  let label = '当前文件';
  if (document.uri.startsWith('file:')) {
    try { const filename = fileURLToPath(document.uri); directory = path.dirname(filename); label = path.basename(filename); }
    catch { /* Untitled/non-file documents use the workspace directory. */ }
  }
  visit(document.uri, document.text, directory, label);
  return [...symbols.values()].map(({ priority, ...item }) => item);
}

function completionItems(document, position, documents, rootDir) {
  const line = document.text.split(/\r?\n/)[position.line] || '';
  const before = line.slice(0, position.character);
  if (withoutComment(before) !== before) return [];
  const importing = /^\s*import\s+([^\s]*)$/.exec(before);
  if (importing) {
    const directory = documentDirectory(document, rootDir);
    try {
      return fs.readdirSync(directory, { withFileTypes: true }).filter(entry => {
        const name = entry.name.endsWith('.suka') ? entry.name.slice(0, -5) : '';
        const filename = path.join(directory, entry.name);
        return validModule(name) && name.startsWith(importing[1]) &&
          pathToFileURL(filename).href !== document.uri &&
          (entry.isFile() || entry.isSymbolicLink()) && sameDirectory(filename, directory);
      }).map(entry => ({ label: entry.name.slice(0, -5), kind: 9, detail: `导入同目录 ${entry.name}` }));
    } catch { return []; }
  }
  const symbols = collectSymbols(document, documents, rootDir);
  return [
    ...KEYWORDS.map(label => ({ label, kind: 14, detail: 'Suka 关键字' })),
    ...OPERATORS.map(label => ({ label, kind: 24, detail: 'Suka 符号' })),
    ...symbols,
  ];
}

module.exports = { collectSymbols, completionItems, withoutComment };
