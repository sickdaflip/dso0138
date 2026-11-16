// SPDX-License-Identifier: MIT
/**
 * Live waveform view: parses frames sent by the firmware ("w" / "s1"
 * commands) and draws them on a canvas.
 */

/** Collects "#frame" header, sample lines and "#end" into frame objects. */
export class FrameParser {
	constructor(onFrame, onInfo) {
		this.onFrame = onFrame;
		this.onInfo = onInfo;
		this.cur = null;
	}

	line(text) {
		if (text.startsWith('#frame')) {
			const meta = {};
			for (const kv of text.slice(7).trim().split(/\s+/)) {
				const [k, v] = kv.split('=');
				meta[k] = Number(v);
			}
			this.cur = { ...meta, samples: [] };
		} else if (text.startsWith('#end')) {
			if (this.cur && this.cur.samples.length) this.onFrame(this.cur);
			this.cur = null;
		} else if (this.cur && /^[\d,]+$/.test(text)) {
			for (const v of text.split(',')) if (v) this.cur.samples.push(Number(v));
		} else if (text && !text.startsWith('#')) {
			this.onInfo?.(text);
		}
	}
}

export function volts(frame) {
	return frame.samples.map((s) => (s - frame.zero) * frame.vpc);
}

export function measure(frame) {
	const v = volts(frame);
	const max = Math.max(...v), min = Math.min(...v);
	const mean = v.reduce((a, b) => a + b, 0) / v.length;
	const rms = Math.sqrt(v.reduce((a, b) => a + b * b, 0) / v.length);
	const mid = (max + min) / 2, hyst = (max - min) / 8;
	let armed = false, first = -1, last = -1, edges = 0;
	if (max - min > frame.vdiv * 0.2) {
		for (let i = 1; i < v.length; i++) {
			if (v[i] < mid - hyst) armed = true;
			if (armed && v[i] >= mid && v[i - 1] < mid) {
				const pos = i - 1 + (mid - v[i - 1]) / (v[i] - v[i - 1]);
				if (first < 0) first = pos;
				last = pos;
				edges++;
				armed = false;
			}
		}
	}
	const freq = edges >= 2 ? (edges - 1) / ((last - first) * frame.dt) : 0;
	return { max, min, vpp: max - min, mean, rms, freq };
}

export function si(value, unit, digits = 3) {
	if (!Number.isFinite(value)) return '–';
	const abs = Math.abs(value);
	const prefixes = [[1e6, 'M'], [1e3, 'k'], [1, ''], [1e-3, 'm'], [1e-6, 'µ'], [1e-9, 'n']];
	let [scale, sym] = prefixes[2];
	if (abs) for ([scale, sym] of prefixes) if (abs >= scale * 0.9995) break;
	const x = value / scale;
	const d = Math.max(0, digits - 1 - Math.floor(Math.log10(Math.abs(x) || 1)));
	return `${x.toFixed(d)} ${sym}${unit}`;
}

/** Draws a 12 x 8 division scope screen in the firmware's colors. */
export function draw(canvas, frame) {
	const ctx = canvas.getContext('2d');
	const dpr = window.devicePixelRatio || 1;
	const w = canvas.clientWidth, h = canvas.clientHeight;
	if (canvas.width !== Math.round(w * dpr)) {
		canvas.width = Math.round(w * dpr);
		canvas.height = Math.round(h * dpr);
	}
	ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
	ctx.fillStyle = '#020617';
	ctx.fillRect(0, 0, w, h);
	const dx = w / 12, dy = h / 8;
	ctx.strokeStyle = '#1e293b';
	ctx.lineWidth = 1;
	ctx.beginPath();
	for (let i = 1; i < 12; i++) { ctx.moveTo(i * dx + 0.5, 0); ctx.lineTo(i * dx + 0.5, h); }
	for (let i = 1; i < 8; i++) { ctx.moveTo(0, i * dy + 0.5); ctx.lineTo(w, i * dy + 0.5); }
	ctx.stroke();
	ctx.strokeStyle = '#334155';
	ctx.strokeRect(0.5, 0.5, w - 1, h - 1);
	if (!frame) return;

	const v = volts(frame);
	const tdiv = frame.tdiv, vdiv = frame.vdiv;
	const yOf = (x) => h / 2 - (x / vdiv) * dy;
	// time of sample i relative to the trigger; screen spans 12 divisions
	const col = Number.isFinite(frame.col) ? frame.col : 150;
	const t0 = -(col / 25) * tdiv;
	const xOf = (i) => frame.roll
		? w - (v.length - 1 - i) * frame.dt / tdiv * dx
		: ((i - frame.trig) * frame.dt - t0) / tdiv * dx;

	ctx.strokeStyle = '#38bdf8';
	ctx.setLineDash([3, 4]);
	ctx.beginPath();
	ctx.moveTo(0, yOf(0) + 0.5);
	ctx.lineTo(w, yOf(0) + 0.5);
	ctx.stroke();
	ctx.setLineDash([]);

	ctx.strokeStyle = '#f8fafc';
	ctx.lineWidth = 1.5;
	ctx.lineJoin = 'round';
	ctx.beginPath();
	let started = false;
	for (let i = 0; i < v.length; i++) {
		const x = xOf(i);
		if (x < -dx || x > w + dx) continue;
		const y = yOf(v[i]);
		if (!started) { ctx.moveTo(x, y); started = true; } else ctx.lineTo(x, y);
	}
	ctx.stroke();
}

export function toCsv(frame) {
	const v = volts(frame);
	const rows = ['time_s,volts'];
	for (let i = 0; i < v.length; i++) {
		rows.push(`${((i - frame.trig) * frame.dt).toExponential(6)},${v[i].toFixed(5)}`);
	}
	return rows.join('\n') + '\n';
}
