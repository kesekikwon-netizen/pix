# 발굴 단면뷰어 아이콘: 아이보리 스퀴클 + 격자 바탕에 단면선(먹색)·흙(테라코타)·레벨선
import io, struct
from PIL import Image, ImageDraw, ImageFilter
import os
SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'src') + '/'  # 원본 바탕·마스크 사본(이 저장소 안에 포함)
S = 1024
bg = Image.open(SRC + 'icon_B_bg_1024.png').convert('RGBA').resize((S, S))
mask = Image.open(SRC + 'icon_B_mask_squircle_512.png').convert('RGBA').split()[3].resize((S, S), Image.LANCZOS)
im = Image.new('RGBA', (S, S), (0, 0, 0, 0)); im.paste(bg, (0, 0), mask)
d = ImageDraw.Draw(im)
INK, TC, TCS = (17, 17, 17, 255), (198, 97, 63, 255), (232, 160, 132, 255)
# 레벨선(가는 회색)
for y in (300, 420, 540, 660):
    d.line([(150, y), (874, y)], fill=(150, 150, 146, 255), width=6)
# 단면선(수혈 + 주혈)
prof = [(150, 470), (330, 450), (372, 455), (410, 640), (450, 668), (560, 668), (585, 668), (600, 735), (640, 735), (655, 668), (640, 668), (655, 668), (700, 666), (735, 640), (770, 445), (874, 425)]
prof = [p for i, p in enumerate(prof) if i not in (10, 11)]
soil = prof + [(874, 860), (150, 860)]
d.polygon(soil, fill=TCS)
# 흙 속 층 줄무늬
for k, y in enumerate((760, 820)):
    d.line([(150, y), (874, y - 12)], fill=TC, width=10)
d.line(prof, fill=(211, 47, 47, 255), width=30, joint='curve')
for p in (prof[0], prof[-1]):
    d.ellipse([p[0] - 15, p[1] - 15, p[0] + 15, p[1] + 15], fill=(211, 47, 47, 255))
# 단면 표시 A–A' (테라코타 화살 머리)
for x in (150, 874):
    d.line([(x, 190), (x, 250)], fill=TC, width=22)
im.putalpha(Image.composite(im.split()[3], Image.new('L', (S, S), 0), mask))
im.save('app_1024.png')

def bmp_entry(im):
    im = im.convert('RGBA'); w, h = im.size
    px = im.tobytes('raw', 'BGRA')
    rows = [px[y*w*4:(y+1)*w*4] for y in range(h)][::-1]
    xor = b''.join(rows); mask_row = ((w + 31)//32)*4; andm = b''
    for y in range(h-1, -1, -1):
        bits = bytearray(mask_row)
        for x in range(w):
            if im.getpixel((x, y))[3] == 0: bits[x//8] |= 0x80 >> (x % 8)
        andm += bytes(bits)
    return struct.pack('<IiiHHIIiiII', 40, w, h*2, 1, 32, 0, len(xor)+len(andm), 0, 0, 0, 0) + xor + andm
def write_ico(path, imgs):
    datas = []
    for i in imgs:
        if i.size[0] >= 256:
            b = io.BytesIO(); i.save(b, 'PNG'); datas.append(b.getvalue())
        else: datas.append(bmp_entry(i))
    out = struct.pack('<HHH', 0, 1, len(imgs)); off = 6 + 16*len(imgs)
    for i, dd in zip(imgs, datas):
        w = i.size[0]; out += struct.pack('<BBBBHHII', w % 256, w % 256, 0, 0, 1, 32, len(dd), off); off += len(dd)
    open(path, 'wb').write(out + b''.join(datas))
rs = lambda s: im.resize((s, s), Image.LANCZOS)
write_ico('app.ico', [rs(s) for s in (16, 20, 24, 32, 40, 48, 64, 128, 256)])
rs(256).save('app_256.png'); rs(64).save('app_64.png')
# NSIS 마법사 그림(164x314) · 머리 그림(150x57): 아이보리 바탕 + 아이콘 + 테라코타 띠
wiz = Image.new('RGB', (164, 314), (247, 246, 242)); wiz.paste(rs(120), (22, 60), rs(120))
dw = ImageDraw.Draw(wiz); dw.rectangle([0, 300, 164, 314], fill=(198, 97, 63))
wiz.save('wizard.bmp')
hd = Image.new('RGB', (150, 57), (255, 255, 255)); hd.paste(rs(44), (98, 6), rs(44)); hd.save('header.bmp')
print('ok')
