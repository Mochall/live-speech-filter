"""Export the public part of this project into work/public-repo (its own git repository).

    .venv/Scripts/python.exe -X utf8 tools/export-public.py [--out DIR]

--out  the public repository folder (default work/public-repo of this checkout; from a git worktree
       pass the main checkout's work/public-repo).

The development repository keeps its full history and internal notes (PLAN.md, test records)
and has no remote. Only the files listed here are copied; the public repository is a separate
git repository under the ignored work/ folder, so pushing it never sends the development
history. Files in the export that are not in this list are removed, then the script scans the
result for local paths and private material and stops if it finds any.
"""
import pathlib
import re
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / 'work/public-repo'
if '--out' in sys.argv:
    OUT = pathlib.Path(sys.argv[sys.argv.index('--out') + 1]).resolve()
PUBLIC = ROOT / 'packaging/public'          # files that exist only for the public repository

FILES = [
    '.gitattributes',
    'packaging/installer-lite.iss',
    'packaging/installer-plugin.iss',
    'packaging/ChineseSimplified.isl',
    'packaging/使用说明-插件.txt',
    'packaging/使用说明-直播消音.txt',
    'tools/build-lite-payload.py',
    'tools/build-plugin-payload.py',
    'tools/make-obs28-sdk.sh',
    'tools/check-plugin-imports.py',
    'tools/export-public.py',
    'update/latest.json',  # read by the programs' update check (raw on Gitee/GitHub, branch main)
]
DIRS = ['standalone', 'docs/design/app-icon', 'docs/images', 'docs/dual-pc', 'tools/tests']
# public-only files: source in packaging/public -> path in the export
EXTRA = {'README.md': 'README.md', 'gitignore': '.gitignore'}

TEXT_BLOCK = [r'C:[/\\]Users', r'D:[/\\]code']    # local paths (a user folder path names the account)
SPONSOR = r'sponsor-qr\.png$'
SPONSOR_PUBLIC = 'docs/images/sponsor-qr.png'   # the one copy meant for the README
MEDIA = r'\.wav$|\.mkv$|\.mp4$|\.onnx$'
ALLOWED_BINARY = re.compile(r'^standalone/data/sounds/[a-z]+\.wav$')


def tracked(prefix):
    out = subprocess.run(['git', 'ls-files', '-z', prefix], cwd=ROOT, capture_output=True, check=True).stdout
    return [p for p in out.decode('utf-8').split('\0') if p]


def main():
    wanted = {}
    for f in FILES:
        wanted[f] = ROOT / f
    for d in DIRS:
        for f in tracked(d):
            wanted[f] = ROOT / f
    for f in sorted((ROOT / 'patches').glob('[0-9][0-9][0-9][0-9]-*.patch')):
        wanted['patches/' + f.name] = f
    for src, dst in EXTRA.items():
        wanted[dst] = PUBLIC / src
    wanted['LICENSE'] = ROOT / 'upstream/obs-profanity-filter/LICENSE'

    OUT.mkdir(parents=True, exist_ok=True)
    if not (OUT / '.git').exists():
        subprocess.run(['git', 'init', '-q', '-b', 'main'], cwd=OUT, check=True)
    for p in sorted(OUT.rglob('*'), reverse=True):
        rel = p.relative_to(OUT).as_posix()
        if rel == '.git' or rel.startswith('.git/'):
            continue
        if p.is_file() and rel not in wanted:
            p.unlink()
        elif p.is_dir() and not any(p.iterdir()):
            p.rmdir()
    for rel, src in wanted.items():
        dst = OUT / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, dst)

    write_patch_index(sorted(k for k in wanted if k.startswith('patches/')))

    problems = []
    for p in OUT.rglob('*'):
        rel = p.relative_to(OUT).as_posix()
        if not p.is_file() or rel.startswith('.git/'):
            continue
        if (re.search(SPONSOR, rel) and rel != SPONSOR_PUBLIC) or (re.search(MEDIA, rel) and not ALLOWED_BINARY.match(rel)):
            problems.append(f'{rel}: private or large media file')
            continue
        data = p.read_bytes()
        if b'\0' in data or p.suffix.lower() in ('.png', '.ico', '.zip', '.wav'):
            continue                     # binary: only the file-name checks above apply
        text = data.decode('utf-8', 'ignore')
        for pat in TEXT_BLOCK:
            for m in re.finditer(pat, text):
                problems.append(f'{rel}: {text[max(0, m.start() - 30):m.end() + 30]!r}')
        if rel == 'packaging/ChineseSimplified.isl':
            continue                     # Inno Setup's translation, with its translator's credit
        for m in re.finditer(r'[\w.+-]+@[\w-]+\.[\w.]+', text):
            mail = m.group()
            if not re.search(r'noreply|live-speech-filter\.invalid|example|anthropic\.com|users\.noreply|^y@7z\.ee$', mail):
                problems.append(f'{rel}: e-mail {mail}')
    if problems:
        print('\n'.join(sorted(set(problems))[:80]))
        sys.exit('export stopped: review the lines above')
    print(f'exported {len(wanted) + 1} files to {OUT}')


def write_patch_index(patches):
    lines = ['# 补丁', '',
             '上游：[Cloud370/obs-profanity-filter](https://github.com/Cloud370/obs-profanity-filter)'
             ' `7f0d445`（v0.3.0-beta.1，GPL-2.0）。在该提交上按编号依次 `git apply` 即得本项目的插件源码；'
             '直播消音（独立程序）编译时使用同一份打过补丁的上游源码。', '',
             '| 补丁 | 说明 |', '| --- | --- |']
    for rel in patches:
        text = (OUT / rel).read_text(encoding='utf-8', errors='ignore')
        m = re.search(r'^Subject: (.*(?:\n .*)*)', text, re.M)
        subject = decode_subject(m.group(1)) if m else ''
        subject = re.sub(r'^\[PATCH[^\]]*\]\s*', '', subject).replace('|', '\\|')
        name = rel.split('/')[-1]
        if not subject:              # 0001-0011 are plain diffs without a message
            subject = name[5:-6].replace('-', ' ')
        lines.append(f'| [{name[:4]}]({name}) | {subject} |')
    (OUT / 'patches/README.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')


def decode_subject(raw):
    from email.header import decode_header, make_header
    raw = re.sub(r'\n ', ' ', raw)
    try:
        return str(make_header(decode_header(raw)))
    except Exception:
        return raw


if __name__ == '__main__':
    main()
