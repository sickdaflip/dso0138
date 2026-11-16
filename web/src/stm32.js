// SPDX-License-Identifier: MIT
/**
 * STM32 system memory bootloader over USART (8E1), as documented in ST
 * application note AN3155. Works with any SerialLink-like object offering
 * read(n, timeout), write(bytes) and drain().
 */
import { sleep } from './serial.js';

const ACK = 0x79;
const NACK = 0x1f;

export class NackError extends Error {
	constructor(what) {
		super(`NACK: ${what}`);
		this.nack = true;
	}
}

export class Stm32Boot {
	constructor(link, log = () => {}) {
		this.link = link;
		this.log = log;
		this.commands = [];
		this.version = 0;
	}

	async ack(what, timeout = 1000) {
		const [b] = await this.link.read(1, timeout);
		if (b === ACK) return;
		if (b === NACK) throw new NackError(what);
		throw new Error(`Unexpected reply 0x${b.toString(16)} (${what})`);
	}

	async sync(attempts = 4) {
		for (let i = 0; i < attempts; i++) {
			this.link.drain();
			await this.link.write([0x7f]);
			try {
				const [b] = await this.link.read(1, 400);
				// NACK means the baud rate was already detected earlier
				if (b === ACK || b === NACK) return;
			} catch {
				// no answer, retry
			}
			await sleep(100);
		}
		throw new Error('No answer from the bootloader');
	}

	async command(c, what, timeout) {
		await this.link.write([c, c ^ 0xff]);
		await this.ack(what, timeout);
	}

	async address(addr, what) {
		const b = [(addr >>> 24) & 0xff, (addr >>> 16) & 0xff, (addr >>> 8) & 0xff, addr & 0xff];
		await this.link.write([...b, b[0] ^ b[1] ^ b[2] ^ b[3]]);
		await this.ack(what);
	}

	async get() {
		await this.command(0x00, 'get');
		const [n] = await this.link.read(1);
		const bytes = await this.link.read(n + 1);
		await this.ack('get end');
		this.version = bytes[0];
		this.commands = Array.from(bytes.slice(1));
		return { version: this.version, commands: this.commands };
	}

	async getId() {
		await this.command(0x02, 'get id');
		const [n] = await this.link.read(1);
		const id = await this.link.read(n + 1);
		await this.ack('get id end');
		return id.reduce((a, b) => (a << 8) | b, 0);
	}

	async read(addr, len) {
		await this.command(0x11, 'read');
		await this.address(addr, 'read address');
		await this.link.write([len - 1, (len - 1) ^ 0xff]);
		await this.ack('read length');
		return this.link.read(len, 2000);
	}

	async write(addr, data) {
		await this.command(0x31, 'write');
		await this.address(addr, 'write address');
		const n = data.length - 1;
		let x = n;
		for (const b of data) x ^= b;
		await this.link.write([n, ...data, x]);
		await this.ack('write data', 2000);
	}

	async eraseAll() {
		if (this.commands.includes(0x44)) {
			await this.command(0x44, 'extended erase');
			await this.link.write([0xff, 0xff, 0x00]);
		} else {
			await this.command(0x43, 'erase');
			await this.link.write([0xff, 0x00]);
		}
		await this.ack('mass erase', 40000);
	}

	/** Erases the listed 1 KiB pages (keeps everything else). */
	async erasePages(pages) {
		if (this.commands.includes(0x44)) {
			await this.command(0x44, 'extended erase');
			const n = pages.length - 1;
			const bytes = [n >> 8, n & 0xff];
			for (const p of pages) bytes.push(p >> 8, p & 0xff);
			await this.link.write([...bytes, bytes.reduce((a, b) => a ^ b, 0)]);
		} else {
			await this.command(0x43, 'erase');
			const bytes = [pages.length - 1, ...pages];
			await this.link.write([...bytes, bytes.reduce((a, b) => a ^ b, 0)]);
		}
		await this.ack('page erase', 40000);
	}

	/** Removes read protection; the chip mass erases itself and resets. */
	async readoutUnprotect() {
		await this.command(0x92, 'readout unprotect');
		await this.ack('readout unprotect done', 40000);
	}

	async go(addr) {
		await this.command(0x21, 'go');
		await this.address(addr, 'go address');
	}
}

export const CHIP_NAMES = {
	0x410: 'STM32F10x medium density',
	0x412: 'STM32F10x low density',
	0x414: 'STM32F10x high density',
	0x420: 'STM32F10x value line',
	0x430: 'STM32F10x XL density',
};
