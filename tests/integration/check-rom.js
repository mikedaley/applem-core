import { readFileSync } from 'fs';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';
import { createContext, runInContext } from 'vm';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

async function loadWasmModule() {
  const wasmJsPath = join(__dirname, '..', 'public', 'a2e.js');
  const wasmDir = dirname(wasmJsPath);
  const code = readFileSync(wasmJsPath, 'utf8');
  const context = {
    module: { exports: {} },
    exports: {},
    globalThis: global,
    console: console,
    require: (await import('module')).createRequire(import.meta.url),
    __dirname: wasmDir,
    __filename: wasmJsPath,
    process: process,
    WebAssembly: WebAssembly,
    URL: URL,
    TextDecoder: TextDecoder,
    setTimeout: setTimeout,
    clearTimeout: clearTimeout,
    performance: { now: () => Date.now() }
  };
  createContext(context);
  runInContext(code, context);
  return context.module.exports;
}

async function runTest() {
  const createA2EModule = await loadWasmModule();
  const Module = await createA2EModule();
  Module._init();

  // Check ROM at FE89 and surrounding area
  console.log('ROM at $FE80-$FE9F:');
  for (let row = 0; row < 2; row++) {
    const addr = 0xFE80 + row * 16;
    let line = '$' + addr.toString(16).toUpperCase() + ': ';
    for (let col = 0; col < 16; col++) {
      line += Module._readMemory(addr + col).toString(16).padStart(2, '0').toUpperCase() + ' ';
    }
    console.log(line);
  }

  // Check IRQ/BRK vectors
  console.log('\nVectors:');
  const nmilo = Module._readMemory(0xFFFA);
  const nmihi = Module._readMemory(0xFFFB);
  console.log('NMI ($FFFA): $' + ((nmihi << 8) | nmilo).toString(16).toUpperCase());
  
  const resetlo = Module._readMemory(0xFFFC);
  const resethi = Module._readMemory(0xFFFD);
  console.log('RESET ($FFFC): $' + ((resethi << 8) | resetlo).toString(16).toUpperCase());
  
  const irqlo = Module._readMemory(0xFFFE);
  const irqhi = Module._readMemory(0xFFFF);
  console.log('IRQ/BRK ($FFFE): $' + ((irqhi << 8) | irqlo).toString(16).toUpperCase());

  // What should be at FE89 in real Apple IIe ROM?
  console.log('\n$FE89 contains: $' + Module._readMemory(0xFE89).toString(16).toUpperCase());
  console.log('$FE8A contains: $' + Module._readMemory(0xFE8A).toString(16).toUpperCase());
  console.log('$FE8B contains: $' + Module._readMemory(0xFE8B).toString(16).toUpperCase());
}

runTest().catch(err => { console.error(err); process.exit(1); });
