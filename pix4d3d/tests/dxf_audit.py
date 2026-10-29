#!/usr/bin/env python3
"""DXF 감사: ezdxf 로 읽고 audit, 레이어·엔티티·IMAGE 확인. 사용: dxf_audit.py a.dxf [b.dxf ...]"""
import sys, ezdxf
from ezdxf import recover
ok = True
for path in sys.argv[1:]:
    doc, auditor = recover.readfile(path)
    a = doc.audit()
    msp = doc.modelspace()
    kinds = {}
    for e in msp:
        kinds[e.dxftype()] = kinds.get(e.dxftype(), 0) + 1
    imgs = [e for e in msp if e.dxftype() == 'IMAGE']
    dup = 0
    for e in msp.query('LWPOLYLINE'):
        pts = list(e.get_points('xy'))
        dup += sum(1 for i in range(1, len(pts)) if abs(pts[i][0]-pts[i-1][0]) < 1e-9 and abs(pts[i][1]-pts[i-1][1]) < 1e-9)
    for e in msp.query('POLYLINE'):
        pts = [v.dxf.location for v in e.vertices]
        dup += sum(1 for i in range(1, len(pts)) if (pts[i]-pts[i-1]).magnitude < 1e-9)
    print(f"{path}: version={doc.dxfversion} recover_errors={len(auditor.errors)} audit_errors={len(a.errors)} fixes={len(a.fixes)} "
          f"layers={sorted(l.dxf.name for l in doc.layers)} entities={kinds} dup_vertices={dup}")
    for im in imgs:
        d = im.image_def
        print(f"  IMAGE file={d.dxf.filename} size={tuple(im.dxf.image_size)} insert={tuple(round(v,4) for v in im.dxf.insert)} u={tuple(round(v,5) for v in im.dxf.u_pixel)} v={tuple(round(v,5) for v in im.dxf.v_pixel)}")
    if auditor.errors or a.errors or dup:
        ok = False
        for e in list(auditor.errors)+list(a.errors): print("  ERR", e.message)
sys.exit(0 if ok else 1)
