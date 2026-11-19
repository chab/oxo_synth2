#!/usr/bin/env python3
"""Synth2 vs the original Synth1: CPU per scenario, same jobs, same session.

  tools/bench/vs_synth1.py [--runs N] [--only substr] [--no-s1]

Both run as x86_64 under Rosetta (Synth1 = build/s1ref/s1ref, ours =
build/vs_synth1/bench-x86_64, the plugin's -O3), runs alternated, best and
median kept; ours also native arm64 (build/vs_synth1/bench-arm64). Builds the
benches and s1ref first. Prints % of one core per scenario (render only:
s1ref's S1REF_TIME, our RenderJob's renderSeconds) and the cost per voice
over idle. Rosetta is a proxy for a PC (AGENTS.md "VS THE ORIGINAL").
"""
import os
import re
import statistics
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT, 'build', 'vs_synth1')
ME = os.path.join(ROOT, 'Shared', 'Resources', 'Synth2 Starter Banks', 'Madame Eljay')
INIT = os.path.join(ROOT, 'Shared', 'Resources', 'Synth2 Factory', '001.sy1')
LEN = 10.0
OSC1_DETUNE = 76


def sy1(path):
    lines = open(path, encoding='latin-1').read().splitlines()
    ver = next((int(l[4:]) for l in lines if l.startswith('ver=')), 0)
    if ver < 112:
        sys.exit(f'{path}: legacy ver {ver}, convert it first')
    vals = {}
    for l in lines[1:]:
        m = re.fullmatch(r'\s*(\d+)\s*,\s*(-?\d+)\s*', l)
        if m:
            vals[int(m[1])] = int(m[2])
    return vals


def held(keys):
    return [(0.0, LEN + 1, k) for k in keys]


def scrub():
    step = 10 * 256 / 44100  # tools/bench Scrub: a key every 60 ms, released after two
    return [(k * step, (k + 2) * step, 48 + k % 24) for k in range(int(LEN / step) + 1)]


CHORD4 = [48, 52, 55, 59]
KEYS16 = list(range(40, 72, 2))


def scenarios():
    init = sy1(INIT)
    saw = {**init, OSC1_DETUNE: 10}
    d50, pad = sy1(os.path.join(ME, '010.sy1')), sy1(os.path.join(ME, '035.sy1'))
    return [
        ('init idle', init, 0, []),
        ('init 1 voice', init, 1, held([60])),
        ('init 4 voices', init, 4, held(CHORD4)),
        ('init 16 voices', init, 16, held(KEYS16)),
        ('init+detune10 1 voice', saw, 1, held([60])),
        ('init+detune10 4 voices', saw, 4, held(CHORD4)),
        ('init+detune10 16 voices', saw, 16, held(KEYS16)),
        ('D-50 idle', d50, 0, []),
        ('D-50 4 keys (16 voices)', d50, 16, held(CHORD4)),
        ('D-50 scrub', d50, None, scrub()),
        ('Harmony Pad idle', pad, 0, []),
        ('Harmony Pad 4 keys (8 v.)', pad, 8, held(CHORD4)),
        ('Harmony Pad scrub', pad, None, scrub()),
    ]


def write_jobs(path, scens):
    os.makedirs(os.path.join(OUT, 'wav'), exist_ok=True)
    with open(path, 'w') as f:
        for i, (name, vals, _, notes) in enumerate(scens):
            f.write(f'job {os.path.join(OUT, "wav", f"s{i:02d}.wav")}\nlen {LEN}\npreroll 0.25\n')
            for k, v in sorted(vals.items()):
                f.write(f'n {k} {v}\n')
            for on, off, key in notes:
                f.write(f'note {on:.6f} {off:.6f} {key} 100\n')
            f.write('end\n')


def run(cmd, env=None):
    p = subprocess.run(cmd, capture_output=True, text=True, env=env)
    if p.returncode:
        sys.exit(f'{cmd[0]} failed:\n{p.stderr[-2000:]}')
    return [100.0 / float(x) for x in re.findall(r'x([\d.]+) realtime', p.stdout + p.stderr)]


def build():
    os.makedirs(OUT, exist_ok=True)
    src = os.path.join(ROOT, 'tools', 'bench', 'main.cpp')
    for arch in ('x86_64', 'arm64'):
        subprocess.run(['clang++', '-std=c++20', '-O3', '-DNDEBUG', '-arch', arch, src, '-o',
                        os.path.join(OUT, f'bench-{arch}')], check=True)
    subprocess.run(['make', '-s', '-C', os.path.join(ROOT, 'tools', 's1ref')], check=True)


def main():
    args = sys.argv[1:]
    runs = int(args[args.index('--runs') + 1]) if '--runs' in args else 5
    only = args[args.index('--only') + 1] if '--only' in args else ''
    s1 = '--no-s1' not in args
    build()
    scens = [s for s in scenarios() if only.lower() in s[0].lower()]
    jobs = os.path.join(OUT, 'vs_synth1_jobs.txt')
    write_jobs(jobs, scens)
    t = {k: [[] for _ in scens] for k in ('s1', 'x64', 'arm')}
    env = dict(os.environ, S1REF_TIME='1')
    for r in range(runs):
        print(f'run {r + 1}/{runs}', file=sys.stderr)
        sides = [('x64', [os.path.join(OUT, 'bench-x86_64'), jobs], None),
                 ('arm', [os.path.join(OUT, 'bench-arm64'), jobs], None)]
        if s1:
            sides.insert(r % 2, ('s1', [os.path.join(ROOT, 'build', 's1ref', 's1ref'), 'batch', jobs], env))
        for key, cmd, e in sides:
            for i, v in enumerate(run(cmd, e)):
                t[key][i].append(v)

    def best(key, i):
        return min(t[key][i]) if t[key][i] else float('nan')

    def med(key, i):
        return statistics.median(t[key][i]) if t[key][i] else float('nan')

    print(f'% of one core (best of {runs}; median in brackets), x86_64 under Rosetta unless arm64')
    print(f'{"scenario":26} {"Synth1":>15} {"Synth2":>15} {"ratio":>6} {"Synth2 arm64":>15}')
    for i, (name, *_rest) in enumerate(scens):
        ratio = best('x64', i) / best('s1', i) if s1 else float('nan')
        print(f'{name:26} {best("s1", i):6.3f} ({med("s1", i):6.3f}) {best("x64", i):6.3f} ({med("x64", i):6.3f})'
              f' {ratio:6.2f} {best("arm", i):6.3f} ({med("arm", i):6.3f})')
    idle = {}
    for i, (name, _, voices, _n) in enumerate(scens):
        if voices == 0:
            idle[name.split(' idle')[0]] = i
    print('per voice over idle (% of one core)')
    for i, (name, _, voices, _n) in enumerate(scens):
        base = next((j for p, j in idle.items() if name.startswith(p)), None)
        if voices and base is not None:
            per = [(best(k, i) - best(k, base)) / voices for k in ('s1', 'x64', 'arm')]
            ratio = per[1] / per[0] if s1 else float('nan')
            print(f'{name:26} {per[0]:7.4f} {per[1]:7.4f} {ratio:6.2f} {per[2]:7.4f}')


if __name__ == '__main__':
    main()
