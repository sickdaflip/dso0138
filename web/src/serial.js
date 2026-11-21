// SPDX-License-Identifier: MIT
/**
 * Thin wrapper around a Web Serial port with a byte buffer, timed reads
 * and line events for the telemetry monitor.
 */
export const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

export class SerialLink {
	constructor(port) {
		this.port = port;
		this.buffer = [];
		this.waiters = [];
		this.reader = null;
		this.writer = null;
		this.onLine = null;
		this.lineBuf = '';
		this.decoder = new TextDecoder();
		this.loop = null;
	}

	/** parity: 'none' for the firmware console, 'even' for the STM32 bootloader */
	async open(baudRate, parity = 'none') {
		await this.port.open({ baudRate, parity, bufferSize: 65536 });
		this.writer = this.port.writable.getWriter();
		this.reader = this.port.readable.getReader();
		this.loop = this.readLoop();
	}

	async readLoop() {
		// Framing, parity or overrun errors end the current reader but leave
		// the port usable: take a new reader and keep going. Only a closed or
		// unplugged port ends the loop.
		while (this.reader) {
			try {
				for (;;) {
					const { value, done } = await this.reader.read();
					if (done) return;
					if (!value) continue;
					for (const b of value) this.buffer.push(b);
					if (this.onLine) this.feedLines(value);
					this.wake();
				}
			} catch (e) {
				if (this.closing || !this.port.readable) return;
				this.onError?.(e);
				this.lineBuf = '';
				try {
					this.reader.releaseLock();
				} catch { /* already released */ }
				this.reader = this.port.readable.getReader();
			}
		}
	}

	feedLines(bytes) {
		this.lineBuf += this.decoder.decode(bytes, { stream: true });
		let idx;
		while ((idx = this.lineBuf.indexOf('\n')) >= 0) {
			const line = this.lineBuf.slice(0, idx).replace(/\r$/, '');
			this.lineBuf = this.lineBuf.slice(idx + 1);
			this.onLine(line);
		}
		if (this.lineBuf.length > 4096) this.lineBuf = '';
		this.buffer.length = 0;  // line mode does not use the byte buffer
	}

	wake() {
		const waiters = this.waiters;
		this.waiters = [];
		waiters.forEach((w) => w());
	}

	/** Reads exactly n bytes or throws after `timeout` ms. */
	async read(n, timeout = 500) {
		const deadline = performance.now() + timeout;
		while (this.buffer.length < n) {
			const left = deadline - performance.now();
			if (left <= 0) throw new Error('timeout');
			await Promise.race([
				new Promise((r) => this.waiters.push(r)),
				sleep(Math.min(left, 50)),
			]);
		}
		return Uint8Array.from(this.buffer.splice(0, n));
	}

	drain() {
		this.buffer.length = 0;
	}

	async write(bytes) {
		await this.writer.write(bytes instanceof Uint8Array ? bytes : Uint8Array.from(bytes));
	}

	async writeText(text) {
		await this.write(new TextEncoder().encode(text));
	}

	async close() {
		this.closing = true;
		try {
			if (this.reader) {
				await this.reader.cancel();
				this.reader.releaseLock();
			}
		} catch { /* already released */ }
		try {
			if (this.writer) this.writer.releaseLock();
		} catch { /* already released */ }
		try {
			await this.loop;
		} catch { /* ignore */ }
		try {
			await this.port.close();
		} catch { /* already closed */ }
		this.reader = this.writer = null;
	}
}
