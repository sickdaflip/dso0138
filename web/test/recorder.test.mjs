// SPDX-License-Identifier: MIT
// Recorder: gap-free roll appends, segments on restart, frames with gaps.
import assert from 'node:assert/strict';
import { Recorder } from '../src/recorder.js';

const roll = (n, append) => ({ roll: 1, append, dt: 8e-3, zero: 2060, vpc: 0.01, trig: 0, samples: Array(n).fill(2160) });
const r = new Recorder();
r.begin();
r.add(roll(300, 0), null);                       // first full frame starts the time base
for (let k = 0; k < 10; k++) r.add(roll(12, 1), Array(12).fill(2160));
assert.equal(r.count, 120, 'only new samples recorded');
const steps = r.times.slice(1).map((t, i) => t - r.times[i]);
assert.ok(steps.every((d) => Math.abs(d - 8e-3) < 1e-9), 'constant spacing dt');
assert.ok(Math.abs(r.volts[0] - 1.0) < 1e-9, 'counts converted to volts');
r.add(roll(300, 0), null);                       // settings changed: new segment
r.add(roll(12, 1), Array(12).fill(2060));
assert.equal(r.segment.at(-1), 1, 'restart opens a new segment');

const f = new Recorder();
f.begin();
const frame = { roll: 0, dt: 1e-3, zero: 2060, vpc: 0.01, trig: 512, samples: Array(1024).fill(2060) };
f.add(frame, null);
f.add(frame, null);
assert.equal(f.count, 2048);
assert.equal(f.segments, 2, 'one segment per triggered frame');
assert.match(f.toCsv().split('\n')[0], /^time_s,volts,segment$/);
// with sample positions: a lost frame leaves a correctly timed gap
const g = new Recorder();
g.begin();
const pr = (pos, n, append) => ({ ...roll(n, append), pos });
g.add(pr(0, 300, 0), null);
g.add(pr(300, 10, 1), Array(10).fill(2160));
g.add(pr(320, 10, 1), Array(10).fill(2160));     // frame 310..319 lost
assert.equal(g.count, 20);
assert.ok(Math.abs((g.times[10] - g.times[9]) - 11 * 8e-3) < 1e-9, 'gap keeps real time');
assert.equal(g.segment[10], 1, 'gap starts a new segment');
console.log('ok - recorder');
