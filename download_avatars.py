import urllib.request, os, sys

BASE = 'https://huggingface.co/datasets/Linzhan/Mixamo-Animations-Characters/resolve/main/character_refined/'
FILES = ['Uriel_A_Plotexia', 'Erika_Archer', 'Medea_By_M._Arrebola',
         'Pirate_By_P._Konstantinov', 'Dreyar_By_M.Aure', 'Nightshade_J_Friedrich',
         'Peasant_Girl', 'Sporty_Granny']
OUT = 'Unreal/Born2Flap/SourceAssets/Mixamo/Characters'

os.makedirs(OUT, exist_ok=True)
for f in FILES:
    p = os.path.join(OUT, f + '.fbx')
    url = BASE + f + '.fbx'
    try:
        req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
        with urllib.request.urlopen(req, timeout=300) as r, open(p, 'wb') as w:
            w.write(r.read())
        print('OK   %-28s %8.2f MB' % (f, os.path.getsize(p) / 1e6), flush=True)
    except Exception as e:
        print('FAIL %-28s %s' % (f, e), flush=True)
print('DONE', flush=True)
