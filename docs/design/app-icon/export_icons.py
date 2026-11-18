"""Export the approved A icon. Dependencies: Pillow, PySide6 (QtSvg)."""
from pathlib import Path
from io import BytesIO
import json
import struct
import zipfile
import xml.etree.ElementTree as ET

from PIL import Image, ImageDraw, ImageFont
from PySide6.QtCore import QByteArray, QRectF
from PySide6.QtGui import QImage, QPainter
from PySide6.QtSvg import QSvgRenderer

ROOT = Path(__file__).resolve().parent
BLUE, INK = '#3B63FB', '#1F2329'
SIZES = (1024, 512, 256, 128, 64, 48, 32, 24, 20, 16)
ICO_SIZES = (16, 20, 24, 32, 48, 64, 128, 256)


def geometry(small):
    # Coordinates refer to a 32px square. Only the plate may occupy the badge zone.
    if small:
        return [(2, 10, 4, 8, 0, BLUE), (8, 4, 4, 20, 0, BLUE),
                (14, 12, 4, 2, 0, BLUE), (20, 6, 4, 12, 0, BLUE),
                (26, 10, 4, 8, 0, BLUE)]
    return [(1, 1, 30, 30, 7, BLUE),
            (5, 10, 2, 8, 1, '#FFFFFF'), (9, 6, 2, 16, 1, '#FFFFFF'),
            (13, 9, 2, 10, 1, '#FFFFFF'), (23, 7, 2, 12, 1, '#FFFFFF'),
            (27, 11, 2, 7, 1, '#FFFFFF'), (16, 9, 6, 10, 2, INK),
            (17, 13, 4, 2, 1, '#FFFFFF')]


def svg(small=False, pixel_size=None):
    # Small PNG exports snap each edge to the target pixel grid before rendering.
    scale = pixel_size / 32 if pixel_size else 1
    edge = pixel_size or 32
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{edge}" height="{edge}" viewBox="0 0 {edge} {edge}">']
    for x, y, w, h, r, color in geometry(small):
        if pixel_size:
            x1, y1, x2, y2 = [round(v * scale) for v in (x, y, x+w, y+h)]
            x, y, w, h, r = x1, y1, x2-x1, y2-y1, r*scale
        parts.append(f'  <rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" fill="{color}"/>')
    return '\n'.join(parts + ['</svg>', ''])


def render(source, size):
    renderer = QSvgRenderer(QByteArray(source.encode()))
    assert renderer.isValid()
    image = QImage(size, size, QImage.Format.Format_ARGB32)
    image.fill(0)
    painter = QPainter(image)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    renderer.render(painter, QRectF(0, 0, size, size))
    painter.end()
    rgba = image.convertToFormat(QImage.Format.Format_RGBA8888)
    return Image.frombytes('RGBA', (size, size), bytes(rgba.bits()))


def badge(im):
    # Occupies x/y >= 60%; state is deliberately separate from the base asset.
    n = im.width
    layer = Image.new('RGBA', (n*8, n*8))
    d = ImageDraw.Draw(layer)
    def box(a, b):
        return tuple(round(v*n*8) for v in (a, a, b, b))
    d.ellipse(box(.60, .99), fill='white')
    d.ellipse(box(.655, .935), fill='#24B36B')
    out = im.copy()
    out.alpha_composite(layer.resize(im.size, Image.Resampling.LANCZOS))
    return out


def font(size, bold=False):
    return ImageFont.truetype('C:/Windows/Fonts/msyhbd.ttc' if bold else 'C:/Windows/Fonts/msyh.ttc', size)


def preview(icons):
    board = Image.new('RGB', (1440, 1080), '#F3F5F9')
    d = ImageDraw.Draw(board)
    d.text((48, 28), '直播消音 / A · 局部抹平', font=font(30, True), fill=INK)
    d.text((48, 77), '白色声波正常通过，深色片段被抹平。右下角留给状态提示。', font=font(17), fill='#69717F')
    for x, sz, label in [(64,256,'完整版'),(410,128,'简化版 · 放大查看')]:
        im = icons[256] if sz==256 else icons[32].resize((128,128),Image.Resampling.NEAREST)
        board.paste(im, (x,135),im)
        d.text((x,408), label, font=font(18, True), fill=INK)
    d.text((700,145),'蓝色  #3B63FB',font=font(19,True),fill=BLUE)
    d.text((700,188),'消音块  #1F2329',font=font(19),fill=INK)
    d.text((700,245),'48px 及以上：完整版',font=font(17),fill=INK)
    d.text((700,282),'32 / 24 / 20 / 16px：简化版，逐尺寸对齐像素',font=font(17),fill=INK)
    d.text((700,331),'状态点单独叠加；主图标不固化运行状态',font=font(17),fill='#69717F')
    for row,(bg,fg,muted,name) in enumerate([('#E9EDF3',INK,'#626C79','浅色环境'),('#202328','#FFFFFF','#B4BDC9','深色环境')]):
        y=464+row*205
        d.rounded_rectangle((48,y,1392,y+185),radius=16,fill=bg)
        d.text((68,y+14),name,font=font(17,True),fill=fg)
        for j,(size,label) in enumerate([(16,'任务栏托盘 · 16px'),(32,'开始菜单 · 32px'),(48,'桌面 · 48px'),(16,'正在过滤 · 16px'),(48,'正在过滤 · 48px')]):
            cx=194+j*258
            im=badge(icons[size]) if j>=3 else icons[size]
            board.paste(im,(cx-size//2,y+86-size//2),im)
            d.text((cx-92,y+141),label,font=font(15),fill=muted)
    d.text((48,900),'像素检查 / 16px、20px、24px、32px（均以最近邻放大 4 倍）',font=font(17,True),fill=INK)
    for j,size in enumerate([16,20,24,32]):
        im=icons[size].resize((size*4,size*4),Image.Resampling.NEAREST)
        board.paste(im,(60+j*220,940),im)
        d.text((200+j*220,960),str(size),font=font(15),fill='#69717F')
    board.save(ROOT/'preview.png')


def main():
    png_dir=ROOT/'png'
    png_dir.mkdir(exist_ok=True)
    (ROOT/'live-speech-filter-full.svg').write_text(svg(),encoding='utf-8')
    (ROOT/'live-speech-filter-small.svg').write_text(svg(True),encoding='utf-8')
    icons={}
    for size in SIZES:
        source=svg(True,size) if size<=32 else svg()
        icons[size]=render(source,size)
        icons[size].save(png_dir/f'live-speech-filter-{size}.png')
    # Build the ICO explicitly to preserve each custom small image, without resizing.
    payloads=[]
    for size in ICO_SIZES:
        stream=BytesIO()
        icons[size].save(stream,format='PNG')
        payloads.append(stream.getvalue())
    offset=6+16*len(ICO_SIZES)
    entries=[]
    for size,payload in zip(ICO_SIZES,payloads):
        entries.append(struct.pack('<BBBBHHII',size%256,size%256,0,0,1,32,len(payload),offset))
        offset+=len(payload)
    ico_path=ROOT/'live-speech-filter.ico'
    ico_path.write_bytes(struct.pack('<HHH',0,1,len(entries))+b''.join(entries)+b''.join(payloads))
    preview(icons)
    for name in ['full','small']:
        ET.parse(ROOT/f'live-speech-filter-{name}.svg')
    for size in SIZES:
        with Image.open(png_dir/f'live-speech-filter-{size}.png') as im:
            assert im.size==(size,size) and im.mode=='RGBA'
            assert im.getpixel((0,0))[3]==0
    with Image.open(ico_path) as ico:
        assert ico.ico.sizes()=={(s,s) for s in ICO_SIZES}
        for size in ICO_SIZES:
            assert ico.ico.getimage((size,size)).convert('RGBA').tobytes()==icons[size].tobytes()
    # Ensure opaque foreground never enters the reserved bottom-right 40% square.
    for size in (16,20,24,32):
        boundary=int(size*.6)
        assert icons[size].getchannel('A').crop((boundary,boundary,size,size)).getbbox() is None
    report={'png_sizes':list(SIZES),'ico_sizes':list(ICO_SIZES),'ico_frames_match_png':True,
            'png_rgba_transparent_corners':True,'svg_parse':True,'small_badge_zone_clear':True,
            'renderer':'PySide6.QtSvg.QSvgRenderer','preview':'Static mockup, not an OS screenshot'}
    (ROOT/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    package=ROOT/'live-speech-filter-icons.zip'
    with zipfile.ZipFile(package,'w',zipfile.ZIP_DEFLATED) as archive:
        for path in [ROOT/'live-speech-filter-full.svg',ROOT/'live-speech-filter-small.svg',ico_path,ROOT/'preview.png',ROOT/'README.md',ROOT/'validation.json',ROOT/'export_icons.py',*sorted(png_dir.glob('*.png'))]:
            archive.write(path,path.relative_to(ROOT))
    print(json.dumps(report,indent=2))
    print(package)


if __name__=='__main__':
    main()
