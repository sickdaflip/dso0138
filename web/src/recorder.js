// SPDX-License-Identifier: MIT
/**
 * Long recordings from the live stream.
 *
 * Roll mode (Auto, 100 ms/div and slower) streams every sample once, so the
 * recording is gap-free: samples are appended with their exact spacing dt.
 * Triggered modes deliver separate frames; each one is stored with its
 * arrival time, so there are gaps between frames.
 */
export class Recorder {
	constructor() {
		this.reset();
	}

	reset() {
		this.active = false;
		this.start = 0;
		this.times = [];
		this.volts = [];
		this.segment = [];
		this.segments = 0;
		this.next = 0;       // time of the next continuous roll sample (s)
		this.dt = 0;
		this.mode = '';      // 'roll' or 'frames'
		this.base = null;    // roll: sample index that maps to `origin`
		this.origin = 0;
		this.expect = null;  // roll: index of the next expected sample
	}

	begin() {
		this.reset();
		this.active = true;
		this.start = performance.now();
	}

	stop() {
		this.active = false;
	}

	get count() {
		return this.volts.length;
	}

	get duration() {
		return this.active ? (performance.now() - this.start) / 1000 : (this.times.at(-1) ?? 0);
	}

	push(t, v) {
		this.times.push(t);
		this.volts.push(v);
		this.segment.push(this.segments);
	}

	/**
	 * Adds a frame as received. `fresh` holds the new raw samples of a roll
	 * append frame, otherwise null.
	 */
	add(frame, fresh) {
		if (!this.active) return;
		const now = (performance.now() - this.start) / 1000;
		const toVolts = (s) => (s - frame.zero) * frame.vpc;
		if (frame.roll) {
			this.mode = 'roll';
			const hasPos = Number.isFinite(frame.pos);
			if (!fresh) {
				// full roll frame: the stream (re)started, begin a new segment
				if (this.count) this.segments++;
				this.next = now;
				this.dt = frame.dt;
				this.base = null;
				this.expect = hasPos ? frame.pos + frame.samples.length : null;
				return;
			}
			if (frame.dt !== this.dt) {
				this.segments++;
				this.next = now;
				this.dt = frame.dt;
				this.base = null;
			}
			if (hasPos) {
				// exact timing from the device's sample index
				if (this.base === null) {
					this.base = frame.pos;
					this.origin = this.next;
				} else if (this.expect !== null && frame.pos !== this.expect) {
					this.segments++;  // samples were lost: mark the gap
				}
				this.expect = frame.pos + fresh.length;
				fresh.forEach((s, k) => {
					this.push(this.origin + (frame.pos + k - this.base) * frame.dt, toVolts(s));
				});
				return;
			}
			for (const s of fresh) {
				this.push(this.next, toVolts(s));
				this.next += frame.dt;
			}
		} else {
			this.mode = 'frames';
			this.segments++;
			for (let i = 0; i < frame.samples.length; i++) {
				this.push(now + (i - frame.trig) * frame.dt, toVolts(frame.samples[i]));
			}
		}
	}

	toCsv() {
		const rows = ['time_s,volts,segment'];
		for (let i = 0; i < this.volts.length; i++) {
			rows.push(`${this.times[i].toFixed(6)},${this.volts[i].toFixed(5)},${this.segment[i]}`);
		}
		return rows.join('\n') + '\n';
	}
}
