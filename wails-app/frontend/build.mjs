import { cp, mkdir, rm } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const frontend = dirname(fileURLToPath(import.meta.url));
const root = join(frontend, '..', '..');
const dist = join(frontend, 'dist');
const files = [
  'Z.png',
  'app.js',
  'apple-touch-icon.svg',
  'black-hole-interaction.js',
  'black-hole-panels.css',
  'black-hole-threejs.html',
  'browser.css',
  'empty-chat.css',
  'favicon.svg',
  'index.html',
  'native-browser-bridge.js',
  'style.css',
  'themes.css',
  'themes.js',
  'trace.css',
];

await rm(dist, { force: true, recursive: true });
await mkdir(dist, { recursive: true });
for (const file of files) await cp(join(root, file), join(dist, file), { recursive: true });
await cp(join(root, 'assets'), join(dist, 'assets'), { recursive: true });
