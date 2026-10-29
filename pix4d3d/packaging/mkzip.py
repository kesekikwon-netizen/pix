#!/usr/bin/env python3
"""휴대용 zip 만들기: 파일 이름을 UTF-8 로 쓰고 범용 비트 11(0x800, "이름은 UTF-8")을 켠다.
Info-ZIP zip 은 이 비트를 켜지 않아(유니코드 경로 추가 필드만) Windows 탐색기 '압축 풀기'가
'읽어보기.txt' 같은 한글 이름을 CP437/CP949 로 읽어 깨뜨리거나 풀지 못했다(1.2.0 신고).
사용: mkzip.py <부모 폴더> <최상위 폴더 이름> <출력.zip>   (검사만: mkzip.py --check <zip>)
"""
import os, sys, zipfile


def check(path):
    bad = []
    with zipfile.ZipFile(path) as z:
        for i in z.infolist():
            if any(ord(c) > 127 for c in i.filename) and not (i.flag_bits & 0x800):
                bad.append(i.filename)
        names = [i.filename for i in z.infolist()]
    if bad:
        print("UTF-8 플래그(0x800) 없는 한글 이름:", bad, file=sys.stderr)
        return 1
    print(f"zip-utf8 ok: {path} entries={len(names)} nonascii={sum(any(ord(c) > 127 for c in n) for n in names)}")
    return 0


def make(parent, top, out):
    if os.path.exists(out):
        os.remove(out)
    with zipfile.ZipFile(out, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        root = os.path.join(parent, top)
        for d, dirs, files in os.walk(root):
            dirs.sort()
            rel = os.path.relpath(d, parent).replace(os.sep, "/")
            zi = zipfile.ZipInfo(rel + "/", date_time=(2026, 1, 1, 0, 0, 0))
            zi.external_attr = (0o40755 << 16) | 0x10
            zi.flag_bits |= 0x800
            z.writestr(zi, b"")
            for f in sorted(files):
                full = os.path.join(d, f)
                zi = zipfile.ZipInfo.from_file(full, rel + "/" + f)
                zi.compress_type = zipfile.ZIP_DEFLATED
                zi.flag_bits |= 0x800   # 이름은 UTF-8 (ASCII 이름이면 zipfile 이 0 으로 둠 — 무해)
                with open(full, "rb") as fh:
                    z.writestr(zi, fh.read(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
    return check(out)


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--check":
        sys.exit(check(sys.argv[2]))
    if len(sys.argv) != 4:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    sys.exit(make(*sys.argv[1:]))
