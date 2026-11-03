'use strict';
const vscode = require('vscode');
const path = require('node:path');
const { spawn } = require('node:child_process');
const { RpcConnection } = require('./lib/rpc');
const highlighting = require('./lib/highlight');

let disposeServer;

function activate(context) {
  const output = vscode.window.createOutputChannel('Rin Language Server');
  const diagnostics = vscode.languages.createDiagnosticCollection('rin');
  context.subscriptions.push(output, diagnostics);
  const legend = new vscode.SemanticTokensLegend(highlighting.tokenTypes, highlighting.tokenModifiers);
  context.subscriptions.push(vscode.languages.registerDocumentSemanticTokensProvider({ language: 'rin' }, {
    provideDocumentSemanticTokens(document, cancellation) {
      const builder = new vscode.SemanticTokensBuilder(legend);
      for (const token of highlighting.classify(document.getText())) {
        if (cancellation.isCancellationRequested) break;
        const modifiers = token.modifiers.reduce((bits, modifier) =>
          bits | (1 << highlighting.tokenModifiers.indexOf(modifier)), 0);
        builder.push(token.line, token.start, token.length, highlighting.tokenTypes.indexOf(token.type), modifiers);
      }
      return builder.build();
    },
  }, legend));
  let connection, child, ready;
  let stopping = false;
  let started = false;
  const synced = new Map();
  const configuration = () => {
    const config = vscode.workspace.getConfiguration('rin');
    let executable = config.get('executable', 'rin');
    const workspace = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
    if (workspace && !path.isAbsolute(executable) && /[\\/]/.test(executable)) executable = path.resolve(workspace, executable);
    return { executable, diagnostics: { enable: config.get('diagnostics.enable', true) } };
  };
  const notify = (method, params) => {
    if (started) connection?.notify(method, params);
    else if (ready) ready.then(() => connection?.notify(method, params)).catch(error => output.appendLine(error.message));
  };
  const open = document => {
    if (!ready || document.languageId !== 'rin' || synced.has(document.uri.toString())) return;
    synced.set(document.uri.toString(), document.version);
    notify('textDocument/didOpen', { textDocument: { uri: document.uri.toString(), languageId: 'rin',
      version: document.version, text: document.getText() } });
  };
  const start = () => {
    if (ready || !vscode.workspace.isTrusted) return;
    child = spawn(process.execPath, [context.asAbsolutePath('server/server.js')], {
      env: { ...process.env, ELECTRON_RUN_AS_NODE: '1' }, windowsHide: true,
      stdio: ['pipe', 'pipe', 'pipe'], shell: false,
    });
    connection = new RpcConnection(child.stdout, child.stdin);
    connection.on('error', error => output.appendLine(error.message));
    connection.on('message', message => {
      if (message.method === 'textDocument/publishDiagnostics') {
        const params = message.params;
        const uri = vscode.Uri.parse(params.uri);
        const openDocument = vscode.workspace.textDocuments.find(document => document.uri.toString() === params.uri);
        if (params.version !== undefined && openDocument && params.version !== openDocument.version) return;
        diagnostics.set(uri, params.diagnostics.map(item => {
          const range = new vscode.Range(item.range.start.line, item.range.start.character,
            item.range.end.line, item.range.end.character);
          const result = new vscode.Diagnostic(range, item.message,
            item.severity === 2 ? vscode.DiagnosticSeverity.Warning : vscode.DiagnosticSeverity.Error);
          result.source = 'rin';
          return result;
        }));
      } else if (message.method === 'window/logMessage') output.appendLine(message.params.message);
      else if (message.id !== undefined && message.method) connection.error(message.id, -32601, 'Client method not supported');
    });
    child.stderr.on('data', data => output.append(data.toString()));
    child.on('error', error => {
      output.appendLine(error.message);
      connection.dispose(error);
    });
    child.on('exit', (code, signal) => {
      connection.dispose();
      if (!stopping) {
        output.appendLine(`Language server exited (${code ?? signal}). Reload the window to restart it.`);
        vscode.window.showWarningMessage('Rin language server stopped. See the Rin Language Server output.');
      }
    });
    ready = connection.request('initialize', {
      processId: process.pid,
      rootUri: vscode.workspace.workspaceFolders?.[0]?.uri.toString() || null,
      workspaceFolders: vscode.workspace.workspaceFolders?.map(folder => ({ uri: folder.uri.toString(), name: folder.name })) || null,
      capabilities: { general: { positionEncodings: ['utf-16'] }, textDocument: { completion: { completionItem: { snippetSupport: false } } } },
      initializationOptions: configuration(),
    }).then(() => { started = true; connection.notify('initialized', {}); });
    ready.catch(error => output.appendLine(`Initialization failed: ${error.message}`));
    vscode.workspace.textDocuments.forEach(open);
  };

  context.subscriptions.push(
    vscode.workspace.onDidGrantWorkspaceTrust(start),
    vscode.workspace.onDidOpenTextDocument(open),
    vscode.workspace.onDidChangeTextDocument(event => {
      if (event.document.languageId !== 'rin') return;
      open(event.document);
      const uri = event.document.uri.toString();
      if (synced.get(uri) === event.document.version) return;
      synced.set(uri, event.document.version);
      notify('textDocument/didChange', { textDocument: { uri, version: event.document.version },
        contentChanges: [{ text: event.document.getText() }] });
    }),
    vscode.workspace.onDidSaveTextDocument(document => {
      if (document.languageId === 'rin') notify('textDocument/didSave', { textDocument: { uri: document.uri.toString() } });
    }),
    vscode.workspace.onDidCloseTextDocument(document => {
      const uri = document.uri.toString();
      if (synced.delete(uri)) notify('textDocument/didClose', { textDocument: { uri } });
      diagnostics.delete(document.uri);
    }),
    vscode.workspace.onDidChangeConfiguration(event => {
      if (event.affectsConfiguration('rin')) notify('workspace/didChangeConfiguration', { settings: { rin: configuration() } });
    }),
    vscode.languages.registerCompletionItemProvider({ language: 'rin' }, {
      async provideCompletionItems(document, position, token) {
        if (!ready || token.isCancellationRequested) return [];
        try {
          await ready;
          open(document);
          const result = await connection.request('textDocument/completion', {
            textDocument: { uri: document.uri.toString() }, position: { line: position.line, character: position.character },
          });
          if (token.isCancellationRequested) return [];
          return (result.items || result || []).map(item => {
            // LSP CompletionItemKind values are one-based, VS Code values zero-based.
            const completion = new vscode.CompletionItem(item.label, Math.max(0, (item.kind || 1) - 1));
            completion.detail = item.detail;
            completion.insertText = item.insertText || item.label;
            return completion;
          });
        } catch (error) { output.appendLine(error.message); return []; }
      },
    }, ':', '('),
    vscode.commands.registerCommand('rin.run', async uri => {
      if (!vscode.workspace.isTrusted) {
        vscode.window.showWarningMessage('Trust this workspace before running Rin.'); return;
      }
      const target = uri instanceof vscode.Uri ? uri : vscode.window.activeTextEditor?.document.uri;
      if (!target) return;
      const document = await vscode.workspace.openTextDocument(target);
      if (document.languageId !== 'rin') return;
      if (!await document.save()) return;
      if (document.uri.scheme !== 'file') {
        vscode.window.showWarningMessage('Save the Rin file to disk before running it.'); return;
      }
      const filename = document.uri.fsPath;
      const task = new vscode.Task({ type: 'rin', file: filename },
        vscode.workspace.getWorkspaceFolder(document.uri) || vscode.TaskScope.Global,
        `Run ${path.basename(filename)}`, 'rin',
        new vscode.ProcessExecution(configuration().executable, [filename], { cwd: path.dirname(filename) }));
      task.presentationOptions = { reveal: vscode.TaskRevealKind.Always, panel: vscode.TaskPanelKind.Shared, clear: true };
      await vscode.tasks.executeTask(task);
    }),
  );
  const watcher = vscode.workspace.createFileSystemWatcher('**/*.rin');
  const changed = (uri, type) => notify('workspace/didChangeWatchedFiles', { changes: [{ uri: uri.toString(), type }] });
  context.subscriptions.push(watcher, watcher.onDidChange(uri => changed(uri, 2)),
    watcher.onDidCreate(uri => changed(uri, 1)), watcher.onDidDelete(uri => changed(uri, 3)));
  start();
  disposeServer = async () => {
    stopping = true;
    if (!connection) return;
    try { await connection.request('shutdown', null, 1000); connection.notify('exit'); } catch {}
    child?.kill();
    connection.dispose();
  };
  context.subscriptions.push({ dispose: () => { if (disposeServer) void disposeServer(); } });
}
async function deactivate() { if (disposeServer) await disposeServer(); }
module.exports = { activate, deactivate };
