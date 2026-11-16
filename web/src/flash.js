// SPDX-License-Identifier: MIT
/**
 * High level flash operations on top of Stm32Boot.
 */
import { sleep } from './serial.js';
import { FLASH_BASE } from './hex.js';

const BLOCK = 256;
const PAGE = 1024;

/** The firmware keeps its settings in the last page of the 64 KiB flash. */
export const SETTINGS_PAGE = 63;

/** Connects to the bootloader; removes read protection if `unprotect`. */
export async function connect(boot, log, attempts) {
	await boot.sync(attempts);
	const info = await boot.get();
	const id = await boot.getId();
	log?.('info', { version: `${info.version >> 4}.${info.version & 15}`, id });
	return id;
}

/**
 * Erases the flash. With `image` and `keepSettings` only the pages the image
 * covers are erased, so calibration and settings survive an update.
 */
export async function erase(boot, log, image, keepSettings = false) {
	try {
		if (keepSettings && image) {
			const first = Math.floor((image.base - FLASH_BASE) / PAGE);
			const last = Math.floor((image.base - FLASH_BASE + image.data.length - 1) / PAGE);
			const pages = [];
			for (let p = first; p <= last; p++) if (p !== SETTINGS_PAGE) pages.push(p);
			await boot.erasePages(pages);
		} else {
			await boot.eraseAll();
		}
	} catch (e) {
		if (!e.nack) throw e;
		// Write protected or read protected (stock JYE firmware): unprotect.
		log?.('unprotect');
		await boot.readoutUnprotect();
		await sleep(1500);  // the chip resets into the bootloader again
		await boot.sync();
		await boot.get();
		await boot.eraseAll();
	}
}

export async function program(boot, image, onProgress) {
	const { base, data } = image;
	const len = (data.length + 3) & ~3;
	const padded = new Uint8Array(len).fill(0xff);
	padded.set(data);
	for (let off = 0; off < len; off += BLOCK) {
		const chunk = padded.subarray(off, Math.min(off + BLOCK, len));
		if (!chunk.every((b) => b === 0xff)) await boot.write(base + off, Array.from(chunk));
		onProgress?.(Math.min(off + BLOCK, len) / len);
	}
}

export async function verify(boot, image, onProgress) {
	const { base, data } = image;
	for (let off = 0; off < data.length; off += BLOCK) {
		const n = Math.min(BLOCK, data.length - off);
		const got = await boot.read(base + off, n);
		for (let i = 0; i < n; i++) {
			if (got[i] !== data[off + i]) {
				throw new Error(`Verify failed at 0x${(base + off + i).toString(16)}`);
			}
		}
		onProgress?.((off + n) / data.length);
	}
}

export async function backup(boot, size, onProgress) {
	const out = new Uint8Array(size);
	for (let off = 0; off < size; off += BLOCK) {
		const n = Math.min(BLOCK, size - off);
		out.set(await boot.read(FLASH_BASE + off, n), off);
		onProgress?.((off + n) / size);
	}
	return out;
}
