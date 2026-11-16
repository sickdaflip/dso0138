// SPDX-License-Identifier: MIT
import Alpine from './vendor/alpine.esm.js';
import { SerialLink, sleep } from './serial.js';
import { Stm32Boot, CHIP_NAMES } from './stm32.js';
import { connect, erase, program, verify, backup } from './flash.js';
import { loadImage, parseHex, FLASH_BASE } from './hex.js';
import { FrameParser, draw, measure, si, toCsv } from './scope.js';
import { languages, detectLanguage, translate } from './i18n.js';

const ORIGINAL = 'firmware/original/113-13801-061.hex';

Alpine.data('dso', () => ({
	languages,
	lang: localStorage.getItem('dso138.lang') || detectLanguage(),
	supported: 'serial' in navigator,
	manifest: null,
	source: 'release',
	file: null,
	verifyAfter: true,
	keepSettings: true,
	busy: false,
	progress: 0,
	status: '',
	statusKind: 'info',
	chipInfo: '',
	backupUrl: '',
	log: [],

	// live view
	link: null,
	streaming: false,
	frame: null,
	stats: null,
	device: '',
	frameCount: 0,
	capturedAt: '',

	init() {
		this.$watch('lang', (l) => {
			localStorage.setItem('dso138.lang', l);
			document.documentElement.lang = l;
		});
		document.documentElement.lang = this.lang;
		fetch('firmware/manifest.json')
			.then((r) => (r.ok ? r.json() : null))
			.then((m) => {
				this.manifest = m;
				if (!m) this.source = 'original';
			})
			.catch(() => { this.source = 'original'; });
		window.addEventListener('resize', () => this.redraw());
		this.$nextTick(() => this.redraw());
	},

	t(key, vars) {
		return translate(this.lang, key, vars);
	},

	addLog(text) {
		const time = new Date().toLocaleTimeString();
		this.log.push(`${time}  ${text}`);
		if (this.log.length > 200) this.log.shift();
	},

	setStatus(key, kind = 'info', vars) {
		this.status = this.t(key, vars);
		this.statusKind = kind;
		this.addLog(this.status);
	},

	pickFile(e) {
		this.file = e.target.files[0] || null;
	},

	async image() {
		if (this.source === 'release') {
			const r = await fetch(`firmware/${this.manifest.bin}`);
			return loadImage(this.manifest.bin, await r.arrayBuffer());
		}
		if (this.source === 'original') {
			const r = await fetch(ORIGINAL);
			return parseHex(await r.text());
		}
		if (!this.file) throw new Error(this.t('noFile'));
		return loadImage(this.file.name, await this.file.arrayBuffer());
	},

	/**
	 * Opens the bootloader. First tries a chip that is already in boot mode
	 * (JP1/JP2 closed), then asks a running DSO138 Open firmware to jump into
	 * the bootloader by itself, so no jumpers are needed.
	 */
	async openBootloader(port) {
		let link = new SerialLink(port);
		await link.open(115200, 'even');
		let boot = new Stm32Boot(link);
		try {
			return { link, boot, id: await connect(boot, null, 2) };
		} catch {
			await link.close();
		}
		this.addLog('No bootloader answer, asking the firmware to start it');
		link = new SerialLink(port);
		await link.open(115200);
		await link.writeText('\nboot\n');
		await sleep(400);
		await link.close();
		link = new SerialLink(port);
		await link.open(115200, 'even');
		boot = new Stm32Boot(link);
		try {
			return { link, boot, id: await connect(boot, null, 4) };
		} catch {
			await link.close();
			throw new Error(this.t('errNoAnswer'));
		}
	},

	async bootSession(work) {
		await this.disconnect();
		const port = await navigator.serial.requestPort();
		this.busy = true;
		this.progress = 0;
		let link = null;
		try {
			this.setStatus('phase_connect');
			const s = await this.openBootloader(port);
			link = s.link;
			const { boot, id } = s;
			this.chipInfo = this.t('chip', {
				name: CHIP_NAMES[id] || 'STM32',
				id: id.toString(16),
				version: `${boot.version >> 4}.${boot.version & 15}`,
			});
			this.addLog(this.chipInfo);
			await work(boot);
		} catch (e) {
			this.status = e.message;
			this.statusKind = 'error';
			this.addLog(`Error: ${e.message}`);
		} finally {
			this.busy = false;
			if (link) await link.close();
		}
	},

	async flash() {
		let img;
		try {
			img = await this.image();
		} catch (e) {
			this.status = e.message;
			this.statusKind = 'error';
			return;
		}
		this.addLog(`Image: ${img.data.length} bytes at 0x${img.base.toString(16)}`);
		await this.bootSession(async (boot) => {
			this.setStatus('phase_erase');
			// Updates of DSO138 Open keep calibration and settings; other
			// images get a full erase so no stale data is left behind.
			const keep = this.source === 'release' && this.keepSettings;
			await erase(boot, (ev) => ev === 'unprotect' && this.setStatus('phase_unprotect', 'warn'), img, keep);
			this.setStatus('phase_write');
			await program(boot, img, (p) => { this.progress = this.verifyAfter ? p * 0.7 : p; });
			if (this.verifyAfter) {
				this.setStatus('phase_verify');
				await verify(boot, img, (p) => { this.progress = 0.7 + p * 0.3; });
			}
			this.setStatus('phase_go');
			await boot.go(FLASH_BASE);
			this.progress = 1;
			this.setStatus('phase_done', 'ok');
		});
	},

	async backup() {
		await this.bootSession(async (boot) => {
			this.setStatus('phase_backup');
			let data;
			try {
				data = await backup(boot, 65536, (p) => { this.progress = p; });
			} catch (e) {
				if (e.nack) throw new Error(this.t('errProtected'));
				throw e;
			}
			if (this.backupUrl) URL.revokeObjectURL(this.backupUrl);
			this.backupUrl = URL.createObjectURL(new Blob([data], { type: 'application/octet-stream' }));
			this.setStatus('phase_backupDone', 'ok');
		});
	},

	// Live view -------------------------------------------------------------
	async connectMonitor() {
		const port = await navigator.serial.requestPort();
		const link = new SerialLink(port);
		await link.open(115200);
		const parser = new FrameParser(
			(f) => {
				this.frameCount++;
				this.frame = f;
				this.stats = measure(f);
				this.redraw();
			},
			(info) => {
				if (info.startsWith('DSO138')) this.device = info;
				this.addLog(info);
			},
		);
		link.onLine = (l) => parser.line(l);
		this.link = link;
		await link.writeText('i\n');
		await sleep(100);
		await this.setStream(true);
	},

	async disconnect() {
		if (!this.link) return;
		try {
			await this.link.writeText('s0\n');
		} catch { /* port gone */ }
		await this.link.close();
		this.link = null;
		this.streaming = false;
	},

	async setStream(on) {
		if (!this.link) return;
		this.streaming = on;
		if (on) this.capturedAt = '';
		await this.link.writeText(on ? 's1\n' : 's0\n');
	},

	/**
	 * Single capture: stops streaming and fetches one frame, so the view
	 * freezes on it. Gives feedback when nothing arrives.
	 */
	async capture() {
		if (!this.link) return;
		if (this.streaming) await this.setStream(false);
		const before = this.frameCount;
		this.capturedAt = '';
		await this.link.writeText('w\n');
		for (let i = 0; i < 40 && this.frameCount === before; i++) await sleep(50);
		this.capturedAt = this.frameCount === before
			? this.t('noFrame')
			: this.t('capturedAt', { time: new Date().toLocaleTimeString() });
	},

	exportCsv() {
		if (!this.frame) return;
		const url = URL.createObjectURL(new Blob([toCsv(this.frame)], { type: 'text/csv' }));
		const a = document.createElement('a');
		a.href = url;
		a.download = `dso138-${new Date().toISOString().replace(/[:.]/g, '-')}.csv`;
		a.click();
		setTimeout(() => URL.revokeObjectURL(url), 1000);
	},

	redraw() {
		const c = this.$refs.scope;
		if (c) draw(c, this.frame);
	},

	si,
}));

Alpine.start();
