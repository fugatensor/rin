'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { classify, lexLine } = require('../lib/highlight');

let textmate, oniguruma;
try { textmate = require('vscode-textmate'); oniguruma = require('vscode-oniguruma'); } catch {}
const dependencyMissing = !textmate || !oniguruma;
const grammarFile = path.resolve(__dirname, '../syntaxes/rin.tmLanguage.json');
let wasm;
async function registry(theme) {
  if (!wasm) {
    const bytes = fs.readFileSync(require.resolve('vscode-oniguruma/release/onig.wasm'));
    wasm = oniguruma.loadWASM(bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength));
  }
  await wasm;
  return new textmate.Registry({
    theme,
    onigLib: Promise.resolve({ createOnigScanner: patterns => new oniguruma.OnigScanner(patterns),
      createOnigString: text => new oniguruma.OnigString(text) }),
    loadGrammar: async scope => scope === 'source.rin'
      ? textmate.parseRawGrammar(fs.readFileSync(grammarFile, 'utf8'), grammarFile) : null,
  });
}
const tokenizerOptions = { skip: dependencyMissing && 'Install vscode-textmate and vscode-oniguruma as test-only dependencies (NODE_PATH is supported).' };
function scopes(grammar, text) {
  return grammar.tokenizeLine(text, textmate.INITIAL).tokens.map(token => ({
    text: text.slice(token.startIndex, token.endIndex), scopes: token.scopes,
  }));
}
function expectScope(tokens, text, scope) {
  assert.ok(tokens.some(token => token.text === text && token.scopes.includes(scope)),
    `Expected ${JSON.stringify(text)} in ${scope}: ${JSON.stringify(tokens)}`);
}

test('real TextMate/Oniguruma scopes cover objects, types, relations, modules, labels and comments', tokenizerOptions, async () => {
  const reg = await registry();
  try {
    const grammar = await reg.loadGrammar('source.rin');
    let tokens = scopes(grammar, '苏格拉底:human');
    expectScope(tokens, '苏格拉底', 'variable.other.constant.rin');
    expectScope(tokens, 'human', 'entity.name.type.rin');
    tokens = scopes(grammar, 'teacher(Alice,苏格拉底) # 关系');
    expectScope(tokens, 'teacher', 'entity.name.function.rin');
    expectScope(tokens, 'Alice', 'variable.other.constant.rin');
    assert.ok(tokens.some(token => token.scopes.includes('comment.line.number-sign.rin')));
    expectScope(scopes(grammar, 'import facts'), 'facts', 'entity.name.namespace.rin');
    expectScope(scopes(grammar, 'verify P1 = 苏格拉底:human'), 'P1', 'entity.name.label.rin');
  } finally { reg.dispose(); }
});

test('real tokenizer handles quantifier declarations, retired type syntax and adjacent arrows', tokenizerOptions, async () => {
  const reg = await registry();
  try {
    const grammar = await reg.loadGrammar('source.rin');
    const quantified = scopes(grammar, 'all x,y:human,n:nat => exists m => mother(m,x)');
    for (const name of ['x', 'y', 'n', 'm']) expectScope(quantified, name, 'variable.parameter.rin');
    for (const type of ['human', 'nat']) expectScope(quantified, type, 'entity.name.type.rin');
    const retired = scopes(grammar, 'type nat = 0 | succ(nat)');
    assert.ok(retired.every(token => !token.scopes.some(scope =>
      scope === 'meta.declaration.type.rin' || scope === 'entity.name.function.constructor.rin')));
    const compact = scopes(grammar, 'all x:human=>p(x)->q(x)');
    expectScope(compact, '->', 'keyword.operator.rin');
    expectScope(compact, 'p', 'entity.name.function.rin');
    expectScope(compact, 'q', 'entity.name.function.rin');
  } finally { reg.dispose(); }
});

test('semantic tokens reject retired type declarations and split conjunctions', () => {
  const old = classify('type nat = 0 | succ(nat)');
  assert.equal(old.find(token => token.text === 'type').type, 'variable');
  assert.ok(old.every(token => !token.modifiers.includes('declaration')));
  const conjunction = lexLine('p(x)&q(x)');
  assert.equal(conjunction.find(token => token.text === '&').kind, 'operator');
  assert.equal(classify('all x:human => p(x) & q(x) -> r(x)')
    .find(token => token.text === '&').type, 'operator');
});

test('semantic tokens preserve quantified scope and distinguish object names from variables', () => {
  const text = [
    '苏格拉底:human',
    'all 人,学者:human,n:nat =>',
    '    teacher(人,学者) -> respect(学者,人)',
    '    exists 母亲 => mother(母亲,人)',
    '    mother(母亲,人)', // The existential binding ended on the preceding line.
    'verify P1 = exists 母亲 => mother(母亲,苏格拉底)',
    'mother(母亲,人)', // Neither all nor exists binds these top-level objects.
  ].join('\n');
  const tokens = classify(text);
  const one = (line, name) => tokens.filter(token => token.line === line && token.text === name);
  assert.deepEqual(one(0, '苏格拉底')[0].modifiers, ['readonly']);
  for (const variable of ['人', '学者', 'n']) {
    assert.equal(one(1, variable)[0].type, 'parameter');
    assert.deepEqual(one(1, variable)[0].modifiers, ['declaration']);
  }
  for (const variable of ['人', '学者']) assert.ok(one(2, variable).every(token => token.type === 'parameter'));
  assert.ok(one(3, '母亲').every(token => token.type === 'parameter'));
  assert.equal(one(4, '母亲')[0].type, 'variable');
  assert.equal(one(4, '人')[0].type, 'parameter');
  assert.equal(one(5, 'P1')[0].type, 'label');
  assert.equal(one(5, '苏格拉底')[0].type, 'variable');
  for (const name of ['母亲', '人']) assert.equal(one(6, name)[0].type, 'variable');
});

test('semantic tokens do not extend bindings backwards, across inline rules, or into comments', () => {
  const tokens = classify('all x:human => mother(m,x) -> exists m => known(m)\np(x) # exists y => fake(y)\n');
  const m = tokens.filter(token => token.text === 'm');
  assert.deepEqual(m.map(token => token.type), ['variable', 'parameter', 'parameter']);
  assert.equal(tokens.find(token => token.line === 1 && token.text === 'x').type, 'variable');
  assert.equal(tokens.filter(token => token.type === 'comment').length, 1);
  assert.ok(!tokens.some(token => token.text === 'fake'));
});

test('semantic UTF-16 spans remain correct for non-BMP object names and mixed binders', () => {
  const text = 'all x,y:human,n:nat=>pair(😀,succ(n),x)';
  for (const token of classify(text)) assert.equal(text.slice(token.start, token.start + token.length), token.text);
  const emoji = lexLine(text).find(token => token.text === '😀');
  assert.equal(emoji.length, 2);
  assert.equal(classify(text).find(token => token.text === 'succ').type, 'function');
  assert.equal(classify('import facts')[1].type, 'namespace');
});

function readTheme(filename) {
  const theme = JSON.parse(fs.readFileSync(filename, 'utf8'));
  const inherited = theme.include ? readTheme(path.resolve(path.dirname(filename), theme.include)) : { tokenColors: [], colors: {} };
  return { ...theme, colors: { ...inherited.colors, ...theme.colors }, tokenColors: [...inherited.tokenColors, ...(theme.tokenColors || [])] };
}

test('real Dark+ and Light+ themes give object/parameter/type/function non-default colors', {
  skip: dependencyMissing || !process.env.RIN_VSCODE_THEME_DIR ? 'Set RIN_VSCODE_THEME_DIR and install tokenizer test dependencies to inspect the real VS Code themes.' : false,
}, async t => {
  for (const filename of ['dark_plus.json', 'light_plus.json']) {
    const source = readTheme(path.join(process.env.RIN_VSCODE_THEME_DIR, filename));
    const foreground = source.colors['editor.foreground'];
    assert.ok(foreground, 'The resolved real theme must supply its default foreground');
    const reg = await registry({ name: source.name, settings: [{ settings: { foreground } }, ...source.tokenColors] });
    try {
      const grammar = await reg.loadGrammar('source.rin');
      const color = (line, needle) => {
        const offset = line.indexOf(needle);
        const encoded = grammar.tokenizeLine2(line, textmate.INITIAL).tokens;
        let metadata;
        for (let i = 0; i < encoded.length; i += 2) {
          if (encoded[i] > offset) break;
          metadata = encoded[i + 1];
        }
        return reg.getColorMap()[(metadata & 0x00FF8000) >>> 15].toUpperCase();
      };
      const colors = {
        object: color('苏格拉底:human', '苏格拉底'),
        parameter: color('all x:human => p(x)', 'x'),
        type: color('苏格拉底:human', 'human'),
        function: color('mother(m,苏格拉底)', 'mother'),
      };
      for (const [kind, value] of Object.entries(colors)) assert.notEqual(value, foreground.toUpperCase(), `${source.name}: ${kind} uses default text color`);
      assert.equal(new Set(Object.values(colors)).size, 4, `${source.name}: categories should be visibly different`);
      t.diagnostic(`${source.name}: ${JSON.stringify(colors)}`);
    } finally { reg.dispose(); }
  }
});
