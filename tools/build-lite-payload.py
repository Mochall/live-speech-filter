"""Assembles the payload of the 直播消音 (LiveSpeechFilter) installer and writes a manifest.

    python tools/build-lite-payload.py <version>

Build LiveSpeechFilter (Release, with -DLSF_SPONSOR_IMAGE) first. Layout (installed to
%LOCALAPPDATA%/Programs/LiveSpeechFilter, no administrator rights):
    LiveSpeechFilter.exe, Qt / sherpa-onnx / FFmpeg DLLs, platforms/ styles/ tls/   from the build folder
    msvcp140*.dll, vcruntime140*.dll   Visual C++ runtime, app-local (machines without the redistributable)
    data/       word list, pinyin dictionary, replacement sounds (from the build folder)
    models/     the recommended X-ASR model (same files as in the plugin installer 0.8.5)
    vbcable/    VB-Audio's original VBCABLE_Driver_Pack46.zip, unmodified (the installer and the program's
                安装 button open VB-Audio's own setup from it; distribution allowed with attribution:
                https://vb-audio.com/Services/licensing.htm)
    licenses/   program and engine sources, patches, third-party licenses, SOURCES.txt
Output: work/installer-payload-lite-<version>, work/installer-manifest-lite-<version>.json
"""
import hashlib
import json
import pathlib
import re
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
version = sys.argv[1]
build = ROOT / 'work/build-lite/Release'
plugin = ROOT / 'work/installer-payload-plugin-0.8.5'
deps = ROOT / 'upstream/obs-profanity-filter/.deps'
crt = sorted(pathlib.Path('C:/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/VC/Redist/MSVC').glob('*/x64/Microsoft.VC145.CRT'))[-1]
MODEL = 'sherpa-onnx-x-asr-160ms-streaming-zipformer-transducer-zh-en-int8-2026-06-05'
out = ROOT / f'work/installer-payload-lite-{version}'
if out.exists():
    shutil.rmtree(out)
out.mkdir(parents=True)


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest().upper()


# Program and the DLLs the build copied next to it (test tools lsf-lite-cli / lsf-play-test stay out).
exe = build / 'LiveSpeechFilter.exe'
assert 'sponsor-qr.png'.encode('utf-16-be') in exe.read_bytes(), 'build without -DLSF_SPONSOR_IMAGE'
shutil.copy2(exe, out)
dlls = sorted(build.glob('*.dll'))
for p in dlls:
    shutil.copy2(p, out)
for sub in ('platforms', 'styles', 'tls'):
    shutil.copytree(build / sub, out / sub)
# Visual C++ runtime used by the program, Qt, onnxruntime and FFmpeg (dumpbin /dependents).
runtime = ['msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'vcruntime140.dll', 'vcruntime140_1.dll']
for name in runtime:
    shutil.copy2(crt / name, out)
shutil.copytree(build / 'data', out / 'data')
shutil.copytree(plugin / 'data/models' / MODEL, out / 'models' / MODEL)
VBCABLE_ZIP = ROOT / 'downloads/vbcable-pack46/VBCABLE_Driver_Pack46.zip'
VBCABLE_SHA = 'BC44E8974191A5A2427D6C979E2DAB08DBB09925F3F435DFA2F0BA3E45B6EB90'  # as published by VB-Audio
assert sha(VBCABLE_ZIP) == VBCABLE_SHA, 'VB-CABLE pack differs from the recorded original'
(out / 'vbcable').mkdir()
shutil.copy2(VBCABLE_ZIP, out / 'vbcable')

# Sources and licenses
lic = out / 'licenses'
lic.mkdir()
shutil.copy2(plugin / 'licenses/plugin-GPL.txt', lic / 'GPL-2.0.txt')
shutil.copy2(plugin / 'licenses/plugin-upstream-source.zip', lic)
third = lic / 'third-party'
shutil.copytree(plugin / 'licenses/third-party', third,
                ignore=shutil.ignore_patterns('curl-*', 'zlib-*'))  # the plugin's model download only
shutil.copy2(deps / 'obs-deps-qt6-2025-07-11-x64/licenses/qt6/LICENSE.GPL2', third / 'Qt-LICENSE.GPL2.txt')
shutil.copy2(deps / 'obs-deps-2025-07-11-x64/licenses/FFmpeg/COPYING.GPLv2', third / 'FFmpeg-COPYING.GPLv2.txt')
(lic / 'patches').mkdir()
for p in sorted((ROOT / 'patches').glob('*')):
    shutil.copy2(p, lic / 'patches' / p.name)
eng = ROOT / 'upstream/obs-profanity-filter'
shutil.copytree(eng / 'src', lic / 'modified-plugin-src')
shutil.copy2(eng / 'CMakeLists.txt', lic / 'modified-plugin-src/CMakeLists.txt')
app = ROOT / 'standalone'
shutil.copytree(app, lic / 'program-src', ignore=shutil.ignore_patterns('*.wav', '__pycache__'))

last_patch = sorted(p.name[:4] for p in (ROOT / 'patches').glob('[0-9][0-9][0-9][0-9]-*.patch'))[-1]
qt_version = re.search(rb'Qt 6\.\d+\.\d+', (build / 'Qt6Core.dll').read_bytes()).group().decode()
sources = f'''直播消音 (LiveSpeechFilter) {version}, GPL-2.0 (GPL-2.0.txt).
Program sources: program-src (standalone folder of the project; build with CMake, see its README.md).
Recognition and muting engine: Cloud370/obs-profanity-filter (GPL-2.0)
  https://github.com/Cloud370/obs-profanity-filter/tree/7f0d445357df7e009321f5e6531d0543f55a6a8c (plugin-upstream-source.zip)
  Local changes: patches/0001-{last_patch} (apply in order on 7f0d445); resulting sources in modified-plugin-src.
  The program uses only the shared engine files (censor-engine, companion-audio); the OBS plugin itself is not included.
Bundled (licenses in third-party):
  {qt_version} (GPL-2.0 for this program; LGPL-3.0/GPL also offered by Qt): https://download.qt.io/archive/qt/
    Qt6Core/Gui/Widgets/Network and the qwindows, qmodernwindowsstyle, qschannelbackend plugins,
    as built by obs-deps 2025-07-11: https://github.com/obsproject/obs-deps/releases/tag/2025-07-11
  FFmpeg libswresample and libavutil (swresample-5.dll, avutil-59.dll), GPL-2.0 build from obs-deps 2025-07-11:
    https://github.com/obsproject/obs-deps/releases/tag/2025-07-11 (FFmpeg source: https://ffmpeg.org/download.html)
  Sherpa ONNX 1.12.10 (Apache-2.0): https://github.com/k2-fsa/sherpa-onnx/tree/v1.12.10
    sherpa-onnx-c-api.dll from sherpa-onnx-v1.12.10-win-x64-shared.tar.bz2
  ONNX Runtime 1.17 (MIT), onnxruntime.dll as shipped in the Sherpa ONNX 1.12.10 package: https://github.com/microsoft/onnxruntime
  cpp-pinyin (Apache-2.0): https://github.com/wolfgitpr/cpp-pinyin/tree/e4dd8310e3bb6b8ac8edc42f2cbd80be92586a8e
    (statically linked; dictionary files in data\\dict)
  Microsoft Visual C++ runtime {crt.parent.parent.name} (msvcp140*.dll, vcruntime140*.dll), redistributable files of
    Visual Studio, shipped unmodified next to the program.
Speech model X-ASR zh-en 160ms streaming zipformer int8 2026-06-05 (Apache-2.0): https://github.com/Gilgamesh-J/X-ASR
  models\\{MODEL}\\ (LICENSE and README.md inside)
Replacement sounds (CC0 1.0, Freesound): data\\sounds\\README.md lists each source and author.
VB-CABLE by VB-Audio Software (www.vb-cable.com): vbcable\\VBCABLE_Driver_Pack46.zip is VB-Audio's original
  driver pack, unmodified (SHA256 {VBCABLE_SHA}), from https://vb-audio.com/Cable/ . It is not part of this
  program and not covered by its license; it is distributed as is under https://vb-audio.com/Services/licensing.htm .
  The installer (when the user keeps "安装 VB-CABLE" ticked) and the program's 安装 button open VB-Audio's own setup.
  VB-CABLE is a donationware, all participations are welcome.
'''
(lic / 'SOURCES.txt').write_text(sources, encoding='utf-8')

files = sorted(p for p in out.rglob('*') if p.is_file())
manifest = {'version': version, 'patches': '0001-' + last_patch, 'qt': qt_version, 'vc_runtime': crt.parent.parent.name,
            'exe_sha256': sha(out / 'LiveSpeechFilter.exe'), 'vbcable_pack46_sha256': VBCABLE_SHA, 'file_count': len(files),
            'total_bytes': sum(p.stat().st_size for p in files),
            'dlls': {p.name: sha(p) for p in sorted(out.glob('*.dll'))}}
(ROOT / f'work/installer-manifest-lite-{version}.json').write_text(json.dumps(manifest, indent=2, ensure_ascii=False),
                                                                        encoding='utf-8')
print(json.dumps(manifest, indent=2, ensure_ascii=False))
