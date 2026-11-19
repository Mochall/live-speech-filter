"""Assembles the payload of the plugin-only installer (no OBS inside) and writes a manifest.

    .venv/Scripts/python.exe tools/build-plugin-payload.py <version> [--data-root DIR]

--data-root  checkout holding the ignored work/ and downloads/ folders (default: this checkout; a git
             worktree has none of them). Patches, standalone/ and the plugin sources come from this
             checkout (its upstream/obs-profanity-filter clone when it has one, else the data root's).

Layout (installed as C:/ProgramData/obs-studio/plugins/obs-profanity-filter):
    bin/64bit/  plugin DLL + its DLLs      (OBS 28-32, still read by OBS 33)
    ./          the same DLLs              (OBS 33 layout; OBS 34 will read only this one)
    data/       dictionary, word list, locale, bundled X-ASR model (shared by both)
    data/sounds/   animal replacement sounds (CC0; README.md lists authors and links)
    data/vbcable/  VB-Audio's original VBCABLE_Driver_Pack46.zip, unmodified (the installer's task and the
                plugin's 安装 VB-CABLE button open VB-Audio's own setup from it)
    licenses/   sources and licenses
Sources: plugin DLL from work/obs-compat/build-<last patch number>, the three runtime DLLs and the data folder from
the 0.7.0 payload (word list from upstream/obs-profanity-filter/data) (same files that were tested there), plugin sources from upstream/obs-profanity-filter.
"""
import hashlib
import json
import re
import pathlib
import shutil
import sys

SRC = pathlib.Path(__file__).resolve().parents[1]  # this checkout: patches/, standalone/
args = sys.argv[1:]
ROOT = SRC
if '--data-root' in args:
    i = args.index('--data-root')
    ROOT = pathlib.Path(args[i + 1]).resolve()
    del args[i:i + 2]
ENG = SRC / 'upstream/obs-profanity-filter'
if not (ENG / 'src').is_dir():
    ENG = ROOT / 'upstream/obs-profanity-filter'
version = args[0]
NAME = 'obs-profanity-filter'
old = ROOT / 'work/installer-payload-0.7.0'
out = ROOT / f'work/installer-payload-plugin-{version}'
if out.exists():
    shutil.rmtree(out)
last_patch = sorted(p.name[:4] for p in (SRC / 'patches').glob('[0-9][0-9][0-9][0-9]-*.patch'))[-1]
dlls = {NAME + '.dll': ROOT / 'work/obs-compat' / ('build-' + last_patch) / (NAME + '.dll')}
# The sponsor QR is compiled into the plugin DLL (cmake -DSPONSOR_IMAGE=work/sponsor/sponsor-qr.png),
# not shipped as a file; the image itself stays in the ignored work folder.
for d in ('onnxruntime.dll', 'onnxruntime_providers_shared.dll', 'sherpa-onnx-c-api.dll'):
    dlls[d] = old / 'obs-plugins/64bit' / d
for sub in ('bin/64bit', '.'):
    (out / sub).mkdir(parents=True, exist_ok=True)
    for name, src in dlls.items():
        shutil.copy2(src, out / sub / name)
shutil.copytree(old / 'data/obs-plugins' / NAME, out / 'data')
# The word list follows the plugin sources (0039 removed 渣滓); the rest of data/ (dictionary, locale,
# model, models.json, runtime.json) is unchanged since 0.7.0.
src_data = ENG / 'data'
shutil.copy2(src_data / 'builtin_dirty_words.txt', out / 'data/builtin_dirty_words.txt')
for f in ('models.json', 'runtime.json'):
    assert (src_data / f).read_bytes() == (out / 'data' / f).read_bytes(), f

# Animal replacement sounds (0050): the standalone app's CC0 files and their attribution README.
shutil.copytree(SRC / 'standalone/data/sounds', out / 'data/sounds')
assert sorted(p.name for p in (out / 'data/sounds').iterdir()) == ['README.md', 'bird.wav', 'cat.wav', 'dog.wav', 'duck.wav', 'sheep.wav']

VBCABLE_ZIP = ROOT /'downloads/vbcable-pack46/VBCABLE_Driver_Pack46.zip'
VBCABLE_SHA = 'BC44E8974191A5A2427D6C979E2DAB08DBB09925F3F435DFA2F0BA3E45B6EB90'  # as published by VB-Audio
(out / 'data/vbcable').mkdir()
shutil.copy2(VBCABLE_ZIP, out / 'data/vbcable')

lic = out / 'licenses'
shutil.copytree(old / 'licenses', lic, ignore=shutil.ignore_patterns('patches', 'modified-src', 'SOURCES.txt'))
(lic / 'patches').mkdir()
for p in sorted((SRC / 'patches').glob('*')):
    shutil.copy2(p, lic / 'patches' / p.name)
src = ENG
shutil.copytree(src / 'src', lic / 'modified-src')
shutil.copy2(src / 'CMakeLists.txt', lic / 'modified-src/CMakeLists.txt')


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest().upper()


plugin_sha = sha(dlls[NAME + '.dll'])
assert sha(out / 'data/vbcable/VBCABLE_Driver_Pack46.zip') == VBCABLE_SHA, 'VB-CABLE pack differs from the recorded original'
lines = (old / 'licenses/SOURCES.txt').read_text(encoding='utf-8').splitlines()
assert lines[0].startswith('OBS Studio 31.0.2') and lines[2].startswith('Plugin (GPL-2.0)')
text = '\n'.join(lines[2:]) + '\n'
text = text.replace('patches/0001-0024', 'patches/0001-' + last_patch)
text = re.sub(r'DLL SHA256 [0-9A-F]{64}', 'DLL SHA256 ' + plugin_sha, text)
text = text.replace('data' + chr(92) + 'obs-plugins' + chr(92) + NAME + chr(92), 'data' + chr(92))
text = text.replace('Statically linked', 'OBS Studio is not included; the plugin is installed into the OBS Studio already on this computer.' + chr(10) + 'Statically linked')
text += ('\nVB-CABLE by VB-Audio Software (www.vb-cable.com): data\\vbcable\\VBCABLE_Driver_Pack46.zip is VB-Audio\'s original\n'
         '  driver pack, unmodified (SHA256 ' + VBCABLE_SHA + '), from https://vb-audio.com/Cable/ . It is not part of this\n'
         '  plugin and is distributed as is under https://vb-audio.com/Services/licensing.htm .\n'
         '  VB-CABLE is a donationware, all participations are welcome.\n'
         '\nReplacement sounds in data\\sounds (bird, cat, dog, duck, sheep): CC0 1.0 recordings from Freesound,\n'
         '  trimmed and level-matched; authors and links in data\\sounds\\README.md.\n')
(lic / 'SOURCES.txt').write_text(text, encoding='utf-8')

# rcc stores resource names as UTF-16 big-endian.
sponsor_in_dll = "sponsor-qr.png".encode("utf-16-be") in dlls[NAME + ".dll"].read_bytes()
files = sorted(p for p in out.rglob('*') if p.is_file())
manifest = {'version': version, 'patches': '0001-' + last_patch, 'sponsor_image_in_dll': sponsor_in_dll, 'plugin_dll_sha256': plugin_sha, 'vbcable_pack46_sha256': VBCABLE_SHA, 'file_count': len(files),
            'total_bytes': sum(p.stat().st_size for p in files),
            'dlls': {n: sha(p) for n, p in dlls.items()}}
(ROOT / f'work/installer-manifest-plugin-{version}.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
print(json.dumps(manifest, indent=2))
