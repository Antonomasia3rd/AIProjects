// Validate exports with the installed Windhawk UI's actual js-yaml parser,
// schema validator and nested-to-flat converter. No UI module is executed.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..', 'preferences');
const webview = process.argv[2] || 'C:/Program Files/Windhawk/UI/resources/app/extensions/windhawk/webview/main.00022388fd0b5324.js';
const source = fs.readFileSync(webview, 'utf8');
function slice(start, end) {
  const a = source.indexOf(start), b = source.indexOf(end, a);
  assert(a >= 0 && b > a, `Windhawk parser boundary missing: ${start}`);
  return source.slice(a, b);
}
const library = slice('function mY(', 'var SZ=qX.load');
const schemaHelpers = slice('var jZ=function', 'function qZ(');
const converters = slice('function QZ(', 'function ZZ(');
const context = vm.createContext({ Buffer });
vm.runInContext(library + '\nvar SZ=qX.load;\n' + schemaHelpers + '\n' + converters +
  '\nthis.exportsForTest={YamlConverter,YamlSchemaValidator};', context, { timeout: 5000 });
const { YamlConverter, YamlSchemaValidator } = context.exportsForTest;
const toNative = object => JSON.parse(JSON.stringify(object));
const stylerSchema = [
  {key:'win10Spotlight', value:false}, {key:'syncDismissalFade', value:false},
  {key:'controlStyles',value:[[{key:'target',value:''},{key:'styles',value:['']}]]},
  {key:'styleConstants',value:['']}, {key:'themeResourceVariables',value:['']},
];
for (const [stem, yamlName, schema] of [
  ['windows-10-lockscreen','windows-10-lockscreen.wh.preferences.yaml',stylerSchema],
  ['lockapp-xaml-dumper.capture','lockapp-xaml-dumper.capture.preferences.yaml',null],
  ['compatibility/windows-10-lockscreen.preferences-only','compatibility/windows-10-lockscreen.preferences-only.yaml',stylerSchema.slice(2)],
]) {
  const canonical = JSON.parse(fs.readFileSync(path.join(root,stem+'.preset.json'),'utf8'));
  const flat = JSON.parse(fs.readFileSync(path.join(root,stem+'.advanced-settings.json'),'utf8'));
  const validator = new YamlSchemaValidator(schema || Object.keys(canonical).map(key => ({key,value:0})));
  const text = fs.readFileSync(path.join(root,yamlName),'utf8');
  const parsed = YamlConverter.fromYaml(text, validator, (key, values) => JSON.stringify({key,values}));
  assert.equal(parsed.error, null);
  assert.deepEqual(toNative(parsed.settings), flat, 'YAML and raw JSON must write identical settings');
  assert.deepEqual(toNative(YamlConverter.nestedToFlat(canonical)), flat);
  assert(Object.values(flat).every(value => typeof value === 'string' || Number.isInteger(value)));
  assert.equal(fs.readFileSync(path.join(root,yamlName.replace('.yaml','.txt')),'utf8'),text);
  if (schema && Object.hasOwn(canonical,'win10Spotlight')) {
    assert.equal(flat.win10Spotlight,1);
    assert.equal(flat.syncDismissalFade,1);
    const targets = Object.keys(flat).filter(key => /^controlStyles\[\d+\]\.target$/.test(key));
    assert.equal(targets.length,canonical.controlStyles.length);
    assert.equal(flat['controlStyles[0].target'],'Grid#LockScreenTextContent');
    assert.equal(flat['styleConstants[0]'],'mediaBackdropOpacity=0.6');
    assert(!Object.hasOwn(flat,'controlStyles'));
    const wrongBooleans = YamlConverter.fromYaml(JSON.stringify({...canonical,win10Spotlight:true}),validator,key=>key);
    assert(wrongBooleans.error, 'Windhawk YAML switches must be numeric, not true/false');
    const brokenImport = {win10Spotlight:'true',syncDismissalFade:'true',controlStyles:'[object Object]',styleConstants:'mediaBackdropOpacity=0.6',themeResourceVariables:''};
    const normalized = toNative(YamlConverter.flatToNested(brokenImport,stylerSchema));
    assert.equal(normalized.win10Spotlight,0);
    assert.equal(normalized.syncDismissalFade,0);
    assert.equal(normalized.controlStyles[0].target,'');
    console.log(`Reproduced the reported broken import; corrected export preserves all ${targets.length} targets.`);
  } else if (schema) {
    assert(!Object.hasOwn(flat,'win10Spotlight') && !Object.hasOwn(flat,'syncDismissalFade'));
    const full = JSON.parse(fs.readFileSync(path.join(root,'windows-10-lockscreen.preset.json'),'utf8'));
    for (const rule of canonical.controlStyles) {
      assert(!rule.target.startsWith('LockApp.') && rule.target !== 'TextBlock#TitleText');
      assert(full.controlStyles.some(original => JSON.stringify(original) === JSON.stringify(rule)));
    }
  }
  console.log(`PASS ${stem}: ${Object.keys(flat).length} flat entries, actual Windhawk YAML/schema round-trip`);
}
