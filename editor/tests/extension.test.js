'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const Module = require('node:module');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const { pathToFileURL, fileURLToPath } = require('node:url');

function mockHost(directory) {
  const handlers = {}, commands = new Map(), tasks = [], warnings = [];
  const disposable = { dispose() {} };
  const event = name => callback => { handlers[name] = callback; return disposable; };
  class Uri {
    constructor(filename) { this.fsPath = filename; this.scheme = 'file'; }
    toString() { return pathToFileURL(this.fsPath).href; }
    static parse(uri) { return new Uri(fileURLToPath(uri)); }
  }
  const folder = { uri: new Uri(directory), name: 'fixture' };
  const documents = [];
  const vscode = {
    Uri,
    workspace: {
      isTrusted: false, workspaceFolders: [folder], textDocuments: documents,
      getConfiguration: () => ({ get: (key, fallback) => key === 'executable' ? './build/my compiler;literal' : key === 'diagnostics.enable' ? false : fallback }),
      getWorkspaceFolder: () => folder,
      openTextDocument: async uri => documents.find(document => document.uri.toString() === uri.toString()),
      onDidGrantWorkspaceTrust: event('grant'), onDidOpenTextDocument: event('open'),
      onDidChangeTextDocument: event('change'), onDidSaveTextDocument: event('save'),
      onDidCloseTextDocument: event('close'), onDidChangeConfiguration: event('configuration'),
      createFileSystemWatcher: () => ({ ...disposable, onDidChange: event('watchChange'), onDidCreate: event('watchCreate'), onDidDelete: event('watchDelete') }),
    },
    window: { createOutputChannel: () => ({ ...disposable, append() {}, appendLine() {} }),
      showWarningMessage: message => warnings.push(message) },
    languages: {
      createDiagnosticCollection: () => ({ ...disposable, set() {}, delete() {} }),
      registerCompletionItemProvider: (selector, provider) => { handlers.completion = provider; return disposable; },
      registerDocumentSemanticTokensProvider: (selector, provider) => { handlers.semantic = provider; return disposable; },
    },
    commands: { registerCommand: (name, callback) => { commands.set(name, callback); return disposable; } },
    tasks: { executeTask: async task => tasks.push(task) },
    ProcessExecution: class { constructor(executable, args, options) { Object.assign(this, { executable, args, options }); } },
    Task: class { constructor(definition, scope, name, source, execution) { Object.assign(this, { definition, scope, name, source, execution }); } },
    CompletionItem: class { constructor(label, kind) { Object.assign(this, { label, kind }); } },
    SemanticTokensLegend: class { constructor(types, modifiers) { Object.assign(this, { types, modifiers }); } },
    SemanticTokensBuilder: class { constructor() { this.tokens = []; } push(...token) { this.tokens.push(token); } build() { return this.tokens; } },
    Diagnostic: class {}, Range: class {}, DiagnosticSeverity: { Error: 0, Warning: 1 },
    TaskScope: { Global: 1 }, TaskRevealKind: { Always: 1 }, TaskPanelKind: { Shared: 1 },
  };
  return { vscode, handlers, commands, tasks, warnings, documents, Uri };
}
function loadExtension(host) {
  const filename = require.resolve('../extension');
  delete require.cache[filename];
  const original = Module._load;
  Module._load = function(request, ...args) { return request === 'vscode' ? host.vscode : original.call(this, request, ...args); };
  try { return require(filename); } finally { Module._load = original; }
}

// This tests the extension API adapter; a real graphical extension host is separate.
test('run command saves the selected file and supplies an argument array', async t => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'rin-extension '));
  t.after(() => fs.rmSync(directory, { recursive: true, force: true }));
  const host = mockHost(directory);
  let saves = 0;
  const document = { uri: new host.Uri(path.join(directory, 'selected ; 文档.rin')),
    languageId: 'rin', version: 1, getText: () => 'Alice:human', save: async () => { saves++; return true; } };
  host.documents.push(document);
  const extension = loadExtension(host);
  extension.activate({ subscriptions: [], asAbsolutePath: name => path.resolve(__dirname, '..', name) });
  await host.commands.get('rin.run')(document.uri);
  assert.equal(host.tasks.length, 0); // Untrusted workspace does not launch a process.
  host.vscode.workspace.isTrusted = true;
  await host.commands.get('rin.run')(document.uri);
  assert.equal(saves, 1);
  assert.equal(host.tasks.length, 1);
  assert.equal(host.tasks[0].execution.executable, path.join(directory, 'build/my compiler;literal'));
  assert.deepEqual(host.tasks[0].execution.args, [document.uri.fsPath]);
  assert.equal(host.tasks[0].execution.options.cwd, directory);
  await extension.deactivate();
});

test('trust transition starts stdio server and synchronizes previously open documents', async t => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'rin-extension '));
  t.after(() => fs.rmSync(directory, { recursive: true, force: true }));
  const host = mockHost(directory);
  const document = { uri: new host.Uri(path.join(directory, 'main.rin')), languageId: 'rin', version: 1,
    getText: () => 'Alice:human\nverify P = Alice:human\n' };
  host.documents.push(document);
  const extension = loadExtension(host);
  extension.activate({ subscriptions: [], asAbsolutePath: name => path.resolve(__dirname, '..', name) });
  host.handlers.open(document);
  host.vscode.workspace.isTrusted = true;
  host.handlers.grant();
  try {
    const items = await host.handlers.completion.provideCompletionItems(document,
      { line: 2, character: 0 }, { isCancellationRequested: false });
    assert.ok(items.some(item => item.label === 'human'));
    assert.ok(items.some(item => item.label === '=>'));
  } finally { await extension.deactivate(); }
});

test('manifest, grammar and snippets register the final syntax', () => {
  const manifest = require('../package.json');
  assert.equal(manifest.main, './extension.js');
  assert.equal(manifest.contributes.menus['editor/title'][0].command, 'rin.run');
  assert.equal(manifest.contributes.configuration.properties['rin.executable'].default, 'rin');
  const snippets = require('../snippets/rin.json');
  assert.match(snippets['Universal rule'].body[0], /=>$/);
  assert.match(snippets.Verification.body, /^verify .* = /);
  assert.match(snippets['Existential conclusion'].body, /^exists .* => /);
  const grammar = require('../syntaxes/rin.tmLanguage.json');
  assert.equal(grammar.scopeName, 'source.rin');
  for (const pattern of grammar.patterns) {
    for (const key of ['match', 'begin', 'end']) if (pattern[key]) assert.doesNotThrow(() => new RegExp(pattern[key], 'u'));
  }
});
