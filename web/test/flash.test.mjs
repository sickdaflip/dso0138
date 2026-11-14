// SPDX-License-Identifier: MIT
// Runs the flasher against a simulated STM32 bootloader.
// Usage: node test/flash.test.mjs [firmware.bin|firmware.hex]
import { readFileSync } from 'node:fs';
import assert from 'node:assert/strict';
import { Stm32Boot } from '../src/stm32.js';
import { connect, erase, program, verify, backup } from '../src/flash.js';
import { parseHex, loadImage, FLASH_BASE } from '../src/hex.js';

class FakeChip {
	constructor({ protectedChip = false } = {}) {
		this.flash = new Uint8Array(65536).fill(0xff);
		this.protected = protectedChip;
		this.out = [];
		this.inBuf = [];
		this.synced = false;
		this.state = null;
	}
	reply(...b) { this.out.push(...b); }
	feed(bytes) {
		for (const b of bytes) this.inBuf.push(b);
		this.process();
	}
	take(n) { return this.inBuf.length >= n ? this.inBuf.splice(0, n) : null; }
	process() {
		for (;;) {
			if (!this.synced) {
				const b = this.take(1);
				if (!b) return;
				if (b[0] === 0x7f) { this.synced = true; this.reply(0x79); }
				continue;
			}
			if (!this.state) {
				const c = this.take(2);
				if (!c) return;
				assert.equal(c[0] ^ c[1], 0xff, 'command checksum');
				this.state = { cmd: c[0], step: 0 };
				this.startCmd();
				continue;
			}
			if (!this.step()) return;
		}
	}
	startCmd() {
		const s = this.state;
		switch (s.cmd) {
		case 0x00:
			this.reply(0x79, 7, 0x22, 0x00, 0x01, 0x02, 0x11, 0x21, 0x31, 0x43, 0x79);
			this.state = null; break;
		case 0x02: this.reply(0x79, 1, 0x04, 0x10, 0x79); this.state = null; break;
		case 0x11:
			if (this.protected) { this.reply(0x1f); this.state = null; break; }
			this.reply(0x79); break;
		case 0x31: case 0x21: this.reply(0x79); break;
		case 0x43:
			if (this.protected) { this.reply(0x1f); this.state = null; break; }
			this.reply(0x79); break;
		case 0x92:
			this.flash.fill(0xff); this.protected = false;
			this.reply(0x79, 0x79); this.state = null; this.synced = false; break;
		default: this.reply(0x1f); this.state = null;
		}
	}
	addr() {
		const a = this.take(5);
		if (!a) return null;
		assert.equal(a[0] ^ a[1] ^ a[2] ^ a[3], a[4], 'address checksum');
		return ((a[0] << 24) | (a[1] << 16) | (a[2] << 8) | a[3]) >>> 0;
	}
	step() {
		const s = this.state;
		if (s.cmd === 0x11) {
			if (s.step === 0) { const a = this.addr(); if (a === null) return false; s.a = a; s.step = 1; this.reply(0x79); return true; }
			const n = this.take(2); if (!n) return false;
			const len = n[0] + 1, off = s.a - FLASH_BASE;
			this.reply(0x79, ...this.flash.subarray(off, off + len)); this.state = null; return true;
		}
		if (s.cmd === 0x31) {
			if (s.step === 0) { const a = this.addr(); if (a === null) return false; s.a = a; s.step = 1; this.reply(0x79); return true; }
			if (this.inBuf.length < 1) return false;
			const len = this.inBuf[0] + 1;
			const p = this.take(len + 2); if (!p) return false;
			let x = 0; for (const b of p.slice(0, len + 1)) x ^= b;
			assert.equal(x, p[len + 1], 'write checksum');
			assert.equal(len % 4, 0, 'write length multiple of 4');
			this.flash.set(p.slice(1, len + 1), s.a - FLASH_BASE);
			this.reply(0x79); this.state = null; return true;
		}
		if (s.cmd === 0x43) {
			const p = this.take(2); if (!p) return false;
			assert.deepEqual(p, [0xff, 0x00]);
			this.flash.fill(0xff); this.reply(0x79); this.state = null; return true;
		}
		if (s.cmd === 0x21) {
			const a = this.addr(); if (a === null) return false;
			this.ran = a; this.reply(0x79); this.state = null; return true;
		}
		return false;
	}
}

class FakeLink {
	constructor(chip) { this.chip = chip; }
	drain() { this.chip.out.length = 0; }
	async write(bytes) { this.chip.feed(Array.from(bytes)); }
	async read(n) {
		if (this.chip.out.length < n) throw new Error('timeout');
		return Uint8Array.from(this.chip.out.splice(0, n));
	}
}

const path = process.argv[2] ?? '../.pio/build/dso138/firmware.bin';
const image = loadImage(path, readFileSync(path));
assert.equal(image.base, FLASH_BASE);
assert.ok(image.data.length > 1024 && image.data.length <= 64512, 'image size');

for (const protectedChip of [false, true]) {
	const chip = new FakeChip({ protectedChip });
	const boot = new Stm32Boot(new FakeLink(chip));
	const id = await connect(boot);
	assert.equal(id, 0x410);
	const events = [];
	await erase(boot, (e) => events.push(e));
	assert.equal(events.includes('unprotect'), protectedChip);
	await program(boot, image);
	await verify(boot, image);
	const dump = await backup(boot, image.data.length);
	assert.deepEqual(dump, image.data);
	await boot.go(FLASH_BASE);
	assert.equal(chip.ran, FLASH_BASE);
	console.log(`ok - ${protectedChip ? 'protected' : 'blank'} chip, ${image.data.length} bytes`);
}

const original = parseHex(readFileSync('../original-firmware/113-13801-061.hex', 'utf8'));
assert.equal(original.base, FLASH_BASE);
console.log(`ok - original firmware parses (${original.data.length} bytes)`);
