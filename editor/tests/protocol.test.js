'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const { spawn } = require('node:child_process');
const { PassThrough } = require('node:stream');
const { once } = require('node:events');
const { RpcConnection } = require('../lib/rpc');
const { collectSymbols } = require('../server/symbols');

const server = path.resolve(__dirname, '../server/server.js');
function frame(value) {
  const body = Buffer.from(JSON.stringify({ jsonrpc: '2.0', ...value }));
  return Buffer.concat([Buffer.from(`Content-Length: ${body.length}\r\n\r\n`), body]);
}
function harness(directory) {
  const child = spawn(process.execPath, [server], { cwd: directory, stdio: ['pipe', 'pipe', 'pipe'] });
  let stderr = '';
  child.stderr.setEncoding('utf8').on('data', text => { stderr += text; });
  const rpc = new RpcConnection(child.stdout, child.stdin);
  const messages = [];
  rpc.on('message', message => messages.push(message));
  rpc.on('error', error => { throw error; });
  return {
    rpc, child,
    async initialize(options = { diagnostics: { enable: false } }) {
      const result = await rpc.request('initialize', {
        processId: process.pid, rootUri: pathToFileURL(directory).href,
        capabilities: {}, initializationOptions: options,
      });
      rpc.notify('initialized', {});
      return result;
    },
    open(uri, text, version = 1) { rpc.notify('textDocument/didOpen', { textDocument: { uri, languageId: 'rin', text, version } }); },
    change(uri, text, version = 2) { rpc.notify('textDocument/didChange', { textDocument: { uri, version }, contentChanges: [{ text }] }); },
    async waitFor(predicate, timeout = 4000) {
      const existing = messages.find(predicate);
      if (existing) return existing;
      return new Promise((resolve, reject) => {
        const listener = message => {
          if (!predicate(message)) return;
          clearTimeout(timer); rpc.off('message', listener); resolve(message);
        };
        const timer = setTimeout(() => { rpc.off('message', listener); reject(new Error(`No matching notification. stderr=${stderr}`)); }, timeout);
        rpc.on('message', listener);
      });
    },
    async stop() {
      if (child.exitCode !== null) return;
      const exited = once(child, 'exit');
      await rpc.request('shutdown', null);
      rpc.notify('exit');
      const [code] = await exited;
      assert.equal(code, 0, stderr);
      rpc.dispose();
      assert.equal(stderr, '');
    },
  };
}
function workspace(t) {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'rin 编辑器 '));
  t.after(() => fs.rmSync(directory, { recursive: true, force: true }));
  return directory;
}

// The mock is itself a real spawned executable, with a path containing spaces.
// On Windows use RIN_TEST_CLI for the real compiler test below instead.
function fakeChecker(directory) {
  const filename = path.join(directory, 'checker ; spaces');
  fs.writeFileSync(filename, `#!${process.execPath}\n` + `
const fs = require('node:fs');
let text = '';
process.stdin.setEncoding('utf8').on('data', s => text += s).on('end', () => {
  fs.writeFileSync(${JSON.stringify(path.join(directory, 'args.json'))}, JSON.stringify({ args: process.argv.slice(2), text }));
  if (text.includes('BAD')) { process.stderr.write('<source>:2: 模拟语法错误\\n'); process.exitCode = 2; }
  else if (text.includes('IMPORT_ERROR')) { process.stderr.write(${JSON.stringify(path.join(directory, 'broken.rin'))} + ':1: imported error\\n'); process.exitCode = 2; }
});\n`, { mode: 0o755 });
  return filename;
}

test('Content-Length framing handles UTF-8 fragments and adjacent messages', () => {
  const input = new PassThrough(), output = new PassThrough();
  const rpc = new RpcConnection(input, output);
  const messages = [];
  rpc.on('message', message => messages.push(message));
  rpc.on('error', error => { throw error; });
  const payload = Buffer.concat([frame({ method: 'hello', params: '苏格拉底' }), frame({ id: 7, result: '😀' })]);
  for (let i = 0; i < payload.length; i += 3) input.write(payload.subarray(i, i + 3));
  assert.equal(messages.length, 2);
  assert.equal(messages[0].params, '苏格拉底');
  assert.equal(messages[1].result, '😀');
  let sent = Buffer.alloc(0);
  output.on('data', chunk => { sent = Buffer.concat([sent, chunk]); });
  rpc.notify('unicode', { value: '柏拉图' });
  const boundary = sent.indexOf('\r\n\r\n');
  assert.equal(Number(/Content-Length: (\d+)/.exec(sent.subarray(0, boundary).toString())[1]), sent.length - boundary - 4);
  rpc.dispose();
});

test('LSP initialization, imported completion, full changes, close and shutdown', async t => {
  const directory = workspace(t);
  fs.writeFileSync(path.join(directory, 'facts.rin'), 'import more\n苏格拉底:human\nteacher(苏格拉底,柏拉图)\n');
  fs.writeFileSync(path.join(directory, 'more.rin'), 'import facts\n史努比:dog\n');
  const uri = pathToFileURL(path.join(directory, 'main.rin')).href;
  const h = harness(directory); t.after(() => h.child.kill());
  const init = await h.initialize();
  assert.equal(init.capabilities.textDocumentSync.change, 1);
  assert.equal(init.capabilities.positionEncoding, 'utf-16');
  h.open(uri, 'import facts\nall x,y:human => teacher(x,y) -> respect(y,x)\nverify P1 = 苏格拉底:human\n');
  let result = await h.rpc.request('textDocument/completion', { textDocument: { uri }, position: { line: 3, character: 0 } });
  const labels = new Set(result.items.map(item => item.label));
  for (const label of ['all', 'exists', 'verify', '=>', '->', '&', ':', 'human', 'dog', 'teacher', 'respect', '苏格拉底', '史努比'])
    assert.ok(labels.has(label), `Missing ${label}`);
  assert.ok(!labels.has('P1'));
  assert.ok(!labels.has('-'));
  h.change(uri, 'type nat = 0 | succ(nat)\n');
  result = await h.rpc.request('textDocument/completion', { textDocument: { uri }, position: { line: 1, character: 0 } });
  for (const retired of ['type', 'nat', 'succ', '|'])
    assert.ok(!result.items.some(item => item.label === retired), `Retired syntax suggested ${retired}`);
  h.change(uri, 'type : nat\ntype (x)\n', 3);
  result = await h.rpc.request('textDocument/completion', { textDocument: { uri }, position: { line: 2, character: 0 } });
  for (const ordinary of ['type', 'nat', 'x'])
    assert.ok(result.items.some(item => item.label === ordinary), `Ordinary name missing ${ordinary}`);
  h.change(uri, '新对象:newtype\n', 4);
  result = await h.rpc.request('textDocument/completion', { textDocument: { uri }, position: { line: 1, character: 0 } });
  assert.ok(result.items.some(item => item.label === 'newtype'));
  assert.ok(!result.items.some(item => item.label === 'dog'));
  h.rpc.notify('textDocument/didClose', { textDocument: { uri } });
  result = await h.rpc.request('textDocument/completion', { textDocument: { uri }, position: { line: 0, character: 0 } });
  assert.deepEqual(result.items, []);
  await h.stop();
});

test('completion reads unsaved imported documents and excludes comments/strings', async t => {
  const directory = workspace(t);
  const uri = pathToFileURL(path.join(directory, 'main.rin')).href;
  const imported = pathToFileURL(path.join(directory, 'facts.rin')).href;
  fs.writeFileSync(path.join(directory, 'facts.rin'), 'Old:oldtype\n');
  const h = harness(directory); t.after(() => h.child.kill()); await h.initialize();
  h.open(imported, '新对象:unsavedtype\n');
  h.open(uri, 'import facts\n# all\nimport "f\n');
  const items = (await h.rpc.request('textDocument/completion', { textDocument: { uri }, position: { line: 3, character: 0 } })).items;
  assert.ok(items.some(item => item.label === 'unsavedtype'));
  assert.ok(!items.some(item => item.label === 'oldtype'));
  for (const position of [{ line: 1, character: 5 }, { line: 2, character: 9 }]) {
    const result = await h.rpc.request('textDocument/completion', { textDocument: { uri }, position });
    assert.deepEqual(result.items, []);
  }
  await h.stop();
});

test('checker receives unsaved stdin and base directory, publishes and clears diagnostics', { skip: process.platform === 'win32' }, async t => {
  const directory = workspace(t);
  const executable = fakeChecker(directory);
  const uri = pathToFileURL(path.join(directory, 'main.rin')).href;
  const h = harness(directory); t.after(() => h.child.kill());
  await h.initialize({ executable });
  h.open(uri, '苏格拉底:human\nBAD\n');
  const published = await h.waitFor(message => message.method === 'textDocument/publishDiagnostics' && message.params.uri === uri && message.params.diagnostics.length);
  assert.equal(published.params.version, 1);
  assert.equal(published.params.diagnostics[0].range.start.line, 1);
  assert.equal(published.params.diagnostics[0].message, '模拟语法错误');
  const call = JSON.parse(fs.readFileSync(path.join(directory, 'args.json'), 'utf8'));
  assert.deepEqual(call.args, ['--check', '--stdin', '--base-dir', directory]);
  assert.equal(call.text, '苏格拉底:human\nBAD\n');
  h.change(uri, '苏格拉底:human\n');
  await h.waitFor(message => message.method === 'textDocument/publishDiagnostics' && message.params.uri === uri && message.params.version === 2 && message.params.diagnostics.length === 0);
  await h.stop();
});

test('diagnostics attach imported compiler errors to the imported URI', { skip: process.platform === 'win32' }, async t => {
  const directory = workspace(t);
  const executable = fakeChecker(directory);
  fs.writeFileSync(path.join(directory, 'broken.rin'), 'broken\n');
  const uri = pathToFileURL(path.join(directory, 'main.rin')).href;
  const imported = pathToFileURL(path.join(directory, 'broken.rin')).href;
  const h = harness(directory); t.after(() => h.child.kill()); await h.initialize({ executable });
  h.open(uri, 'IMPORT_ERROR\n');
  await h.waitFor(message => message.method === 'textDocument/publishDiagnostics' && message.params.uri === imported && message.params.diagnostics[0]?.message === 'imported error');
  h.change(uri, 'fixed\n');
  await h.waitFor(message => message.method === 'textDocument/publishDiagnostics' && message.params.uri === imported && message.params.diagnostics.length === 0);
  await h.stop();
});

test('missing checker is a useful warning and does not break completion', async t => {
  const directory = workspace(t);
  const h = harness(directory); t.after(() => h.child.kill());
  await h.initialize({ executable: path.join(directory, 'missing-compiler') });
  const uri = pathToFileURL(path.join(directory, 'main.rin')).href;
  h.open(uri, 'Alice:human\n');
  const message = await h.waitFor(message => message.method === 'textDocument/publishDiagnostics' && message.params.diagnostics[0]?.severity === 2);
  assert.match(message.params.diagnostics[0].message, /rin\.executable/);
  const result = await h.rpc.request('textDocument/completion', { textDocument: { uri }, position: { line: 1, character: 0 } });
  assert.ok(result.items.some(item => item.label === 'human'));
  await h.stop();
});

test('symbol collector does not follow parent-directory imports', t => {
  const directory = workspace(t);
  const childDirectory = path.join(directory, 'sub'); fs.mkdirSync(childDirectory);
  fs.writeFileSync(path.join(directory, 'private.rin'), 'Private:privateType\n');
  const document = { uri: pathToFileURL(path.join(childDirectory, 'main.rin')).href, text: 'import ../private\nAlice:human\n' };
  const symbols = collectSymbols(document, new Map(), childDirectory);
  assert.ok(symbols.some(item => item.label === 'human'));
  assert.ok(!symbols.some(item => item.label === 'privateType'));
});

test('import completion supplies bare module names with UTF-8 and dots', async t => {
  const directory = workspace(t);
  for (const filename of ['main.rin', 'facts.rin', 'math.base.rin', '基础.rin',
                          'two words.rin', '.hidden.rin', 'facts.rin.rin', 'other.txt'])
    fs.writeFileSync(path.join(directory, filename), '');
  fs.mkdirSync(path.join(directory, 'folder.rin'));
  const uri = pathToFileURL(path.join(directory, 'main.rin')).href;
  const h = harness(directory); t.after(() => h.child.kill()); await h.initialize();
  h.open(uri, 'import \n');
  let result = await h.rpc.request('textDocument/completion', { textDocument: { uri }, position: { line: 0, character: 7 } });
  assert.deepEqual(result.items.map(item => item.label).sort(), ['facts', 'math.base', '基础'].sort());
  assert.ok(result.items.every(item => item.kind === 9));
  h.change(uri, 'import math.\n');
  result = await h.rpc.request('textDocument/completion', { textDocument: { uri }, position: { line: 0, character: 12 } });
  assert.deepEqual(result.items.map(item => item.label), ['math.base']);
  await h.stop();
});

test('symbol collector resolves dotted bare modules and ignores retired import syntax', t => {
  const directory = workspace(t);
  fs.writeFileSync(path.join(directory, 'math.base.rin'), 'Imported:moduleType\n');
  const uri = pathToFileURL(path.join(directory, 'main.rin')).href;
  const symbols = collectSymbols({ uri, text: 'import math.base\n' }, new Map(), directory);
  assert.ok(symbols.some(item => item.label === 'moduleType'));
  for (const text of ['import "math.base"\n', 'import math.base.rin\n'])
    assert.deepEqual(collectSymbols({ uri, text }, new Map(), directory), []);
});

test('real Rin checker integration (set RIN_TEST_CLI)', { skip: !process.env.RIN_TEST_CLI }, async t => {
  const directory = workspace(t);
  fs.writeFileSync(path.join(directory, 'facts.rin'), '苏格拉底:human\n');
  const h = harness(directory); t.after(() => h.child.kill());
  await h.initialize({ executable: path.resolve(process.env.RIN_TEST_CLI) });
  const uri = pathToFileURL(path.join(directory, 'main.rin')).href;
  h.open(uri, 'import facts\nall x:human => exists m => mother(m,x)\nverify M = exists m => mother(m,苏格拉底)\n');
  await h.waitFor(message => message.method === 'textDocument/publishDiagnostics' && message.params.uri === uri && message.params.version === 1 && message.params.diagnostics.length === 0);
  h.change(uri, 'verify M = mother(\n');
  const error = await h.waitFor(message => message.method === 'textDocument/publishDiagnostics' && message.params.uri === uri && message.params.version === 2 && message.params.diagnostics.length > 0);
  assert.equal(error.params.diagnostics[0].severity, 1);
  await h.stop();
});
