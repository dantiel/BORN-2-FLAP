"""Rebase SK_Isis_ue.fbx: the UE FBX export displaced the whole body to
Y~5.735 (root model and hips model got T=(0.012,5.743,1.088), all vertices
Y~5.7). The GLB source (queen-rigged.glb) has the body centred on the root
(hips 1.088 m up, feet at 0). This edit restores exactly that frame:

1. vertices:   subtract (0.012, 5.735, 0)
2. root model  (Model::SK_Isis)          LclT (0.012,5.743,1.088) -> (0,0,0)
3. hips model  (Model::mixamorig:Hips)   LclT (0.012,5.743,1.088) -> (0,0,1.088)

Bones deeper in the hierarchy are already correct (GLB-relative values).
Run: python Tools/fix_isis_fbx.py
"""
import re
from pathlib import Path

_HERE = Path(__file__).resolve()
FBX = _HERE.parents[1] / 'SourceAssets/NewChars/SK_Isis_ue.fbx'

OFF_X, OFF_Y = 0.012, 5.735


def fix_vertices(s):
    i = s.find('Vertices:')
    if i < 0:
        raise SystemExit('no Vertices block')
    depth = 0
    j = i
    for k in range(i, len(s)):
        if s[k] == '{':
            depth += 1
        elif s[k] == '}':
            depth -= 1
            if depth == 0:
                j = k
                break
    block = s[i:j + 1]
    m = re.search(r'Vertices: \*(\d+) \{\s*a: ([^}]+)\}', block, flags=re.S)
    if not m:
        # values may contain internal braces already handled by brace tracking;
        # fall back to raw content between 'a:' and the final '}'
        m2 = re.search(r'a:\s*(.*)\}\s*$', block, flags=re.S)
        if not m2:
            raise SystemExit('cannot locate vertex array')
        body = m2.group(1)
    else:
        body = m.group(2)
    vals = [float(x) for x in body.split(',') if x.strip()]
    assert len(vals) % 3 == 0, len(vals)
    fixed = []
    for k in range(0, len(vals), 3):
        fixed.append(vals[k] - OFF_X)
        fixed.append(vals[k + 1] - OFF_Y)
        fixed.append(vals[k + 2])
    new_body = ','.join(('%.6f' % v) for v in fixed)
    return s[:i] + block.replace(body, new_body, 1) + s[j + 1:], len(vals) // 3


def fix_lclt(s):
    count = 0
    lclt_re = re.compile(
        r'P: "Lcl Translation", "Lcl Translation", "", "A",(-?[\d.e+]+),(-?[\d.e+]+),(-?[\d.e+]+)')

    def set_model_lclt(name, new_t):
        nonlocal s, count
        i = s.find('"Model::%s"' % name)
        if i < 0:
            print('  model not found:', name)
            return
        seg_start = s.rfind('Model: ', 0, i)
        seg = s[seg_start:seg_start + 4000]
        m = lclt_re.search(seg)
        if not m:
            print('  no LclT in model:', name)
            return
        old = m.group(0)
        new = 'P: "Lcl Translation", "Lcl Translation", "", "A",%s' % ','.join(
            ('%.4g' % v) for v in new_t)
        s = s[:seg_start + m.start()] + new + s[seg_start + m.end():]
        count += 1
        print('  %s: %s -> %s' % (name, old, new))

    # order matters: fix Hips first (its own block), then the root model
    set_model_lclt('mixamorig:Hips', (0, 0, 1.088))
    set_model_lclt('SK_Isis', (0, 0, 0))
    return s, count


def main():
    txt = FBX.read_text(encoding='utf-8', errors='replace')
    txt, nverts = fix_vertices(txt)
    print('vertices transformed:', nverts)
    txt, nmodels = fix_lclt(txt)
    print('model LclT fixed:', nmodels)
    FBX.write_text(txt, encoding='utf-8')
    print('FIX_ISIS_FBX_DONE wrote %s (%d bytes)' % (FBX, FBX.stat().st_size))


if __name__ == '__main__':
    main()