'use strict';

const { EventEmitter } = require('node:events');

// LSP messages use UTF-8 byte lengths, not JavaScript string lengths.
class RpcConnection extends EventEmitter {
  constructor(input, output) {
    super();
    this.output = output;
    this.buffer = Buffer.alloc(0);
    this.pending = new Map();
    this.nextId = 1;
    this.disposed = false;
    input.on('data', chunk => this.receive(chunk));
    input.on('end', () => this.dispose(new Error('Language server connection closed')));
    input.on('error', error => this.emit('error', error));
    output.on('error', error => this.emit('error', error));
  }

  receive(chunk) {
    this.buffer = Buffer.concat([this.buffer, chunk]);
    try {
      while (true) {
        const boundary = this.buffer.indexOf('\r\n\r\n');
        if (boundary < 0) {
          if (this.buffer.length > 8192) throw new Error('LSP header is too large');
          return;
        }
        const header = this.buffer.subarray(0, boundary).toString('ascii');
        const match = /^Content-Length:\s*(\d+)\s*$/im.exec(header);
        if (!match) throw new Error('Missing LSP Content-Length');
        const length = Number(match[1]);
        if (!Number.isSafeInteger(length) || length > 8 * 1024 * 1024)
          throw new Error('LSP message is too large');
        const end = boundary + 4 + length;
        if (this.buffer.length < end) return;
        const message = JSON.parse(this.buffer.subarray(boundary + 4, end).toString('utf8'));
        this.buffer = this.buffer.subarray(end);
        if (message.jsonrpc !== '2.0' || typeof message !== 'object')
          throw new Error('Invalid JSON-RPC message');
        if (!message.method && message.id !== undefined && this.pending.has(message.id)) {
          const pending = this.pending.get(message.id);
          this.pending.delete(message.id);
          clearTimeout(pending.timer);
          if (message.error) pending.reject(new Error(message.error.message));
          else pending.resolve(message.result);
        } else this.emit('message', message);
      }
    } catch (error) {
      this.buffer = Buffer.alloc(0);
      this.emit('error', error);
    }
  }

  send(message) {
    if (this.disposed) return;
    const body = Buffer.from(JSON.stringify({ jsonrpc: '2.0', ...message }), 'utf8');
    this.output.write(Buffer.concat([Buffer.from(`Content-Length: ${body.length}\r\n\r\n`), body]));
  }
  notify(method, params) { this.send({ method, params }); }
  respond(id, result) { this.send({ id, result }); }
  error(id, code, message) { this.send({ id, error: { code, message } }); }
  request(method, params, timeout = 10000) {
    const id = this.nextId++;
    return new Promise((resolve, reject) => {
      if (this.disposed) return reject(new Error('Language server connection closed'));
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`Language server timed out: ${method}`));
      }, timeout);
      this.pending.set(id, { resolve, reject, timer });
      this.send({ id, method, params });
    });
  }
  dispose(error = new Error('Language server connection disposed')) {
    if (this.disposed) return;
    this.disposed = true;
    for (const pending of this.pending.values()) {
      clearTimeout(pending.timer);
      pending.reject(error);
    }
    this.pending.clear();
    this.emit('close');
  }
}

module.exports = { RpcConnection };
