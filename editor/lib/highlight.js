'use strict';

const tokenTypes = ['namespace', 'type', 'parameter', 'variable', 'function',
  'label', 'number', 'keyword', 'operator', 'comment', 'string'];
const tokenModifiers = ['declaration', 'readonly'];

// Positions and lengths are JavaScript UTF-16 offsets, as required by VS Code.
function lexLine(line) {
  const tokens = [];
  let pos = 0;
  const emit = (start, kind) => tokens.push({ start, length: pos - start, text: line.slice(start, pos), kind });
  while (pos < line.length) {
    if (/\s/u.test(line[pos])) { pos++; continue; }
    const start = pos;
    if (line[pos] === '#') { pos = line.length; emit(start, 'comment'); }
    else if (line[pos] === '"') {
      pos++;
      while (pos < line.length) {
        if (line[pos] === '\\') pos = Math.min(line.length, pos + 2);
        else if (line[pos++] === '"') break;
      }
      emit(start, 'string');
    } else if (line.startsWith('=>', pos) || line.startsWith('->', pos)) {
      pos += 2; emit(start, 'operator');
    } else if (':,()=&'.includes(line[pos])) {
      pos++; emit(start, 'operator');
    } else {
      while (pos < line.length && !/\s/u.test(line[pos]) && !':,()=&#"'.includes(line[pos]) &&
        !line.startsWith('=>', pos) && !line.startsWith('->', pos)) pos++;
      emit(start, 'name');
    }
  }
  return tokens;
}

function classify(text) {
  const result = [];
  let block = null;
  for (const [lineNumber, line] of text.split(/\r?\n/).entries()) {
    const tokens = lexLine(line);
    const code = tokens.filter(token => token.kind !== 'comment');
    if (!code.length) {
      for (const token of tokens) result.push({ ...token, line: lineNumber, type: token.kind, modifiers: [] });
      continue;
    }
    const indent = (line.match(/^[ \t]*/u)[0].match(/\t| /gu) || []).reduce((sum, char) => sum + (char === '\t' ? 4 : 1), 0);
    if (block && indent <= block.indent) block = null;
    const variables = new Set(block?.variables || []);
    const overrides = new Map();
    let allArrow = -1;
    const keyword = index => overrides.set(index, { type: 'keyword', modifiers: [] });
    const declaration = (index, type) => overrides.set(index, { type, modifiers: ['declaration'] });

    if (code[0].text === 'import' && code[1]?.kind === 'name') {
      keyword(0); overrides.set(1, { type: 'namespace', modifiers: [] });
    } else if (code[0].text === 'verify' && code[2]?.text === '=') {
      keyword(0); declaration(1, 'label');
    } else if (code[0].text === 'all' && code[1]?.kind === 'name') {
      allArrow = code.findIndex(token => token.text === '=>');
      keyword(0);
      const end = allArrow < 0 ? code.length : allArrow;
      for (let i = 1; i < end; i++) {
        if (code[i].kind !== 'name') continue;
        if (code[i - 1]?.text === ':') overrides.set(i, { type: 'type', modifiers: [] });
        else {
          variables.add(code[i].text);
          declaration(i, 'parameter');
        }
      }
      if (allArrow === code.length - 1) block = { indent, variables: new Set(variables) };
    }

    for (let i = 0; i < code.length; i++) {
      const token = code[i];
      if (token.text === 'exists' && !overrides.has(i) && code[i + 1]?.kind === 'name') {
        const arrow = code.findIndex((candidate, index) => index > i && candidate.text === '=>');
        if (arrow > i) {
          keyword(i);
          for (let j = i + 1; j < arrow; j++) {
            if (code[j].kind === 'name') {
              variables.add(code[j].text);
              declaration(j, 'parameter');
            }
          }
        }
      }
      let classified = overrides.get(i);
      if (!classified) {
        if (token.kind === 'string') classified = { type: 'string', modifiers: [] };
        else if (token.kind === 'operator') classified = { type: 'operator', modifiers: [] };
        else if (code[i - 1]?.text === ':') classified = { type: 'type', modifiers: [] };
        else if (code[i + 1]?.text === '(') classified = { type: 'function', modifiers: [] };
        else if (variables.has(token.text)) classified = { type: 'parameter', modifiers: [] };
        else if (/^\d+$/u.test(token.text)) classified = { type: 'number', modifiers: [] };
        else classified = { type: 'variable', modifiers: ['readonly'] };
      }
      result.push({ ...token, line: lineNumber, ...classified });
    }
    for (const token of tokens.filter(token => token.kind === 'comment'))
      result.push({ ...token, line: lineNumber, type: 'comment', modifiers: [] });
  }
  return result;
}

module.exports = { classify, lexLine, tokenTypes, tokenModifiers };
