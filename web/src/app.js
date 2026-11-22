// SPDX-License-Identifier: MIT
import Alpine from './vendor/alpine.esm.js';
import { SerialLink, sleep } from './serial.js';
import { Stm32Boot, CHIP_NAMES } from './stm32.js';
import { connect, erase, program, verify, backup } from './flash.js';
import { loadImage, parseHex, FLASH_BASE } from './hex.js';
import { FrameParser, draw, measure, si, toCsv, TRACE_COLORS } from './scope.js';
import { languages, detectLanguage, translate } from './i18n.js';
import { Recorder } from './recorder.js';

// Kept outside Alpine's reactive data: thousands of samples per second
// would otherwise trigger an update for every single value.
const recorder = new Recorder();

const ORIGINAL = 'firmware/original/113-13801-061.hex';

// Same tables as the firmware (model.cpp, i18n.cpp)
const TIMEBASES = [10e-6, 20e-6, 50e-6, 100e-6, 200e-6, 500e-6, 1e-3, 2e-3, 5e-3, 10e-3, 20e-3,
	50e-3, 100e-3, 200e-3, 500e-3, 1, 2, 5, 10];
const RANGES = [5, 2, 1, 0.5, 0.2, 0.1, 0.05, 0.02, 0.01];
const HOLDOFFS = [0, 10e-6, 20e-6, 50e-6, 100e-6, 200e-6, 500e-6, 1e-3, 2e-3, 5e-3, 10e-3, 20e-3,
	50e-3, 100e-3, 200e-3, 500e-3, 1];
const DEVICE_LANGS = ['English (US)', 'English (UK)', 'Deutsch', 'Español', 'Français'];

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
	dev: null,          // device settings from "#state"
	genDesc: ['', ''],
	TIMEBASES,
	RANGES,
	HOLDOFFS,
	DEVICE_LANGS,
	TRACE_COLORS,
	frameCount: 0,
	capturedAt: '',
	snapshot: null,     // frame kept by "Capture" for the CSV export
	recOn: false,
	recCount: 0,
	recText: '',

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
		await sleep(600);  // firmware resets into the ROM bootloader
		await link.close();
		link = new SerialLink(port);
		await link.open(115200, 'even');
		boot = new Stm32Boot(link);
		try {
			return { link, boot, id: await connect(boot, null, 6) };
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
				// older firmware could send an unreadable timebase; use the
				// timebase from the device state instead
				if (!Number.isFinite(f.tdiv) && this.dev) f.tdiv = TIMEBASES[this.dev.tb];
				// Roll mode streams only the new samples (append=1): add them to
				// the visible window, which holds one screen of 300 samples.
				const fresh = f.append ? f.samples.slice() : null;
				if (f.append) {
					const prev = this.frame;
					if (!prev || !prev.roll || prev.dt !== f.dt) return;  // wait for a full frame
					f.samples = prev.samples.concat(f.samples).slice(-300);
					f.n = f.samples.length;
				}
				recorder.add(f, fresh);
				this.frameCount++;
				this.frame = f;
				this.stats = measure(f);
				this.redraw();
			},
			(info) => {
				if (info.startsWith('DSO138')) this.device = info;
				this.addLog(info);
			},
			(st) => { this.dev = st; },
			(i, text) => { this.genDesc[i] = text; },
		);
		link.onLine = (l) => parser.line(l);
		link.onError = (e) => this.addLog(`Serial error: ${e.name || e.message}, reading continues`);
		this.link = link;
		await link.writeText('i\n');
		await sleep(100);
		await link.writeText('state\n');
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
		this.dev = null;
		this.snapshot = null;
		this.capturedAt = '';
	},

	// Remote control ----------------------------------------------------------
	async send(command) {
		if (!this.link) return;
		await this.link.writeText(command + '\n');
	},

	set(key, value) {
		if (this.dev) this.dev[key] = Number(value);
		this.send(`set ${key} ${Math.round(Number(value))}`);
	},

	toggleRun() {
		if (this.dev) this.send(`run ${this.dev.run ? 0 : 1}`);
	},

	setGen(mode) {
		this.genDesc = ['', ''];
		this.send(`gen ${mode}`);
	},

	genStep(which, dir) {
		if (!this.dev) return;
		const steps = this.dev['gs' + which];
		const v = Math.min(steps - 1, Math.max(0, this.dev['gp' + which] + dir));
		this.send(`genp ${which} ${v}`);
	},

	levelText() {
		return this.dev ? si(this.dev.level * this.dev.vpc, 'V') : '';
	},

	vposText() {
		return this.dev ? si(this.dev.vpos * RANGES[this.dev.range] / 25, 'V') : '';
	},

	hposText() {
		return this.dev ? si(this.dev.hpos * TIMEBASES[this.dev.tb] / 25, 's') : '';
	},

	async setStream(on) {
		if (!this.link) return;
		this.streaming = on;
		await this.link.writeText(on ? 's1\n' : 's0\n');
	},

	/**
	 * Takes a snapshot for the CSV export. While streaming, the frame on the
	 * screen is kept and the live view keeps running; without a stream one
	 * frame is requested from the device.
	 */
	async capture() {
		if (!this.link) return;
		if (!this.streaming || !this.frame) {
			const before = this.frameCount;
			await this.link.writeText('w\n');
			for (let i = 0; i < 40 && this.frameCount === before; i++) await sleep(50);
			if (this.frameCount === before) {
				this.capturedAt = this.t('noFrame');
				return;
			}
		}
		this.snapshot = { ...this.frame, samples: this.frame.samples.slice() };
		this.capturedAt = this.t('capturedAt', { time: new Date().toLocaleTimeString() });
	},

	// Recording -----------------------------------------------------------
	async toggleRecording() {
		if (recorder.active) {
			recorder.stop();
			clearInterval(this.recTimer);
			this.updateRecStatus();
			return;
		}
		if (!this.streaming) await this.setStream(true);
		recorder.begin();
		this.updateRecStatus();
		this.recTimer = setInterval(() => this.updateRecStatus(), 500);
	},

	updateRecStatus() {
		this.recOn = recorder.active;
		this.recCount = recorder.count;
		const s = Math.floor(recorder.duration);
		const mm = String(Math.floor(s / 60)).padStart(2, '0');
		const ss = String(s % 60).padStart(2, '0');
		const n = recorder.count.toLocaleString(this.lang);
		const kind = recorder.mode === 'roll' ? this.t('recGapless')
			: recorder.mode === 'frames' ? this.t('recFrames', { n: recorder.segments }) : '';
		this.recText = `${mm}:${ss} · ${n} ${this.t('recValues')}${kind ? ' · ' + kind : ''}`;
	},

	exportRecording() {
		if (!recorder.count) return;
		const url = URL.createObjectURL(new Blob([recorder.toCsv()], { type: 'text/csv' }));
		const a = document.createElement('a');
		a.href = url;
		a.download = `dso138-recording-${new Date().toISOString().replace(/[:.]/g, '-')}.csv`;
		a.click();
		setTimeout(() => URL.revokeObjectURL(url), 1000);
	},

	exportCsv() {
		const frame = this.snapshot || this.frame;
		if (!frame) return;
		const url = URL.createObjectURL(new Blob([toCsv(frame)], { type: 'text/csv' }));
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
