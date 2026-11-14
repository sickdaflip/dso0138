// SPDX-License-Identifier: MIT
// Builds the static flasher site into dist/.
// Bundled firmware is taken from public/firmware/ and ../original-firmware/.
import { execFileSync } from 'node:child_process';
import { cpSync, existsSync, mkdirSync, rmSync } from 'node:fs';

const out = 'dist';
rmSync(out, { recursive: true, force: true });
mkdirSync(`${out}/src/vendor`, { recursive: true });
mkdirSync(`${out}/firmware/original`, { recursive: true });

cpSync('index.html', `${out}/index.html`);
for (const f of ['app.js', 'flash.js', 'hex.js', 'i18n.js', 'scope.js', 'serial.js', 'stm32.js']) {
	cpSync(`src/${f}`, `${out}/src/${f}`);
}
cpSync('node_modules/alpinejs/dist/module.esm.min.js', `${out}/src/vendor/alpine.esm.js`);
cpSync('../original-firmware/113-13801-061.hex', `${out}/firmware/original/113-13801-061.hex`);
if (existsSync('public')) cpSync('public', out, { recursive: true });

execFileSync('npx', ['@tailwindcss/cli', '-i', 'src/app.css', '-o', `${out}/app.css`, '--minify'], { stdio: 'inherit' });
console.log(`Site built in ${out}/`);
