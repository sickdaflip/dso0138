// SPDX-License-Identifier: MIT
// Frame parser: complete frames pass, damaged ones are rejected.
import assert from 'node:assert/strict';
import { FrameParser } from '../src/scope.js';

const frames = [];
const info = [];
const p = new FrameParser((f) => frames.push(f), (t) => info.push(t));
const head = (n) => `#frame dt=8.000000e-4 n=${n} append=0 trig=512 zero=2060 vpc=1.017000e-2 vdiv=1.000000e0 tdiv=2.000000e-2 cpl=2 roll=0 col=150 color=5`;
const samples = Array.from({ length: 64 }, (_, i) => 2060 + (i % 7) * 40);
const rows = [samples.slice(0, 32).join(','), samples.slice(32).join(',')];
const sum = samples.reduce((a, v) => (a + v) & 0xffff, 0);
const feed = (lines) => lines.forEach((l) => p.line(l));

feed([head(64), ...rows, `#end n=64 sum=${sum}`]);
assert.equal(frames.length, 1, 'good frame accepted');

feed([...rows, `#end n=64 sum=${sum}`]);                                  // header lost
feed([head(64), rows[0], `#end n=64 sum=${sum}`]);                         // row lost
feed([head(64), rows[0], ',' + rows[1].slice(5), `#end n=64 sum=${sum}`]); // bytes lost in a row
feed([head(64), rows[0], rows[1].replace('2100', '2109'), `#end n=64 sum=${sum}`]); // corrupted digit
feed(['#frame dt=8.0e-4 n=64 tr', ...rows, `#end n=64 sum=${sum}`]);      // damaged header
assert.equal(frames.length, 1, 'damaged frames rejected');
assert.ok(info.every((t) => t.startsWith('#dropped')), 'no sample rows in the log');

feed([head(64), ...rows, '#end']);                                          // firmware 1.4.0 end marker
assert.equal(frames.length, 2, 'old end marker still accepted');
console.log(`ok - parser: ${frames.length} accepted, ${p.dropped} rejected`);
