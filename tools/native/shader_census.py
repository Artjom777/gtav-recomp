"""Run every dumped Xenos shader through XenosRecomp (to HLSL) and DXC (to SPIR-V), one at a
time, and group the failures. Output: OUT/hlsl/<name>.hlsl, OUT/census.txt.

usage: python3 shader_census.py SHADER_DIR OUT_DIR [-j N] [--limit N]
"""
import collections, concurrent.futures as cf, os, pathlib, re, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
WIN = os.name == 'nt'
XR = pathlib.Path(os.environ['GTAV_XR']) if 'GTAV_XR' in os.environ else (
    ROOT / ('build-xr/XenosRecomp/XenosRecomp.exe' if WIN else 'build/xenosrecomp/XenosRecomp/XenosRecomp'))
XR_INCLUDE = ROOT / 'thirdparty/XenosRecomp/XenosRecomp/shader_common.h'
DXC_DIR = ROOT / 'thirdparty/XenosRecomp/thirdparty/dxc-bin'
DXC = DXC_DIR / ('bin/x64/dxc.exe' if WIN else 'bin/x64/dxc-linux')
ENV = dict(os.environ) if WIN else dict(os.environ, LD_LIBRARY_PATH=str(DXC_DIR / 'lib/x64'))


def dxc_args(pixel, src, out):
    args = [str(DXC), '-T', 'ps_6_0' if pixel else 'vs_6_0', '-HV', '2021', '-all-resources-bound',
            '-spirv', '-fvk-use-dx-layout', '-Wno-ignored-attributes', '-Qstrip_debug',
            '-Fo', str(out), str(src)]
    if not pixel:
        args.insert(-3, '-fvk-invert-y')
    return args


def norm(msg):
    msg = re.sub(r"'[A-Za-z_]*\d+[A-Za-z_]*'", "'<id>'", msg)
    msg = re.sub(r'\d+', 'N', msg)
    return msg.strip()[:160]


def run(path, out):
    name = path.stem
    pixel = name.startswith('ps_')
    hlsl = out / 'hlsl' / f'{name}.hlsl'
    spv = out / 'spirv' / f'{name}.spv'
    r = subprocess.run([str(XR), str(path), str(hlsl), str(XR_INCLUDE)], capture_output=True, text=True,
                       env=ENV, timeout=120)
    if r.returncode != 0 or not hlsl.exists():
        tail = (r.stderr or r.stdout).strip().splitlines()[-1:] or [f'exit {r.returncode}']
        return name, 'recomp', norm(tail[0] if r.returncode >= 0 else f'signal {-r.returncode}')
    r = subprocess.run(dxc_args(pixel, hlsl, spv), capture_output=True, text=True, env=ENV, timeout=300)
    if r.returncode != 0:
        errs = [l for l in r.stderr.splitlines() if 'error:' in l]
        return name, 'dxc', norm(errs[0].split('error:', 1)[1] if errs else r.stderr[-160:])
    # Vertex shaders with computed-index fetches need the raw streams; the native renderer looks
    # for this marker next to the SPIR-V.
    marker = out / 'spirv' / f'{name}.indirect'
    if '= vfetchIndirect(' in hlsl.read_text():
        marker.touch()
    elif marker.exists():
        marker.unlink()
    return name, 'ok', ''


def main():
    args = sys.argv[1:]
    jobs = int(args[args.index('-j') + 1]) if '-j' in args else os.cpu_count()
    limit = int(args[args.index('--limit') + 1]) if '--limit' in args else None
    src, out = pathlib.Path(args[0]), pathlib.Path(args[1])
    (out / 'hlsl').mkdir(parents=True, exist_ok=True)
    (out / 'spirv').mkdir(parents=True, exist_ok=True)
    files = sorted(src.glob('*.bin'))[:limit]
    results = []
    with cf.ThreadPoolExecutor(jobs) as ex:
        for i, res in enumerate(ex.map(lambda p: run(p, out), files)):
            results.append(res)
            if (i + 1) % 250 == 0:
                print(f'{i + 1}/{len(files)}', flush=True)
    stages = collections.Counter(s for _, s, _ in results)
    groups = collections.defaultdict(list)
    for name, stage, msg in results:
        if stage != 'ok':
            groups[(stage, msg)].append(name)
    with open(out / 'census.txt', 'w') as f:
        f.write(f'{len(results)} shaders: ' + ', '.join(f'{k}={v}' for k, v in stages.items()) + '\n\n')
        for (stage, msg), names in sorted(groups.items(), key=lambda kv: -len(kv[1])):
            f.write(f'{len(names):5d} [{stage}] {msg}\n      e.g. {" ".join(names[:3])}\n')
    print((out / 'census.txt').read_text()[:4000])


if __name__ == '__main__':
    main()
