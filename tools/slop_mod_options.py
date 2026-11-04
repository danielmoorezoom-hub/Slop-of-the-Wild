# Option variants of the merged data (romfs_build.c picks one by the settings): fog = Lost Woods fog and its warp,
# dark = Typhlo Ruins' darkness, bounds = invisible walls (AirWall).
import itertools, shutil, oead
from pathlib import Path
PROBE = Path('/mnt/user-data/uploads/Slop of the Wild/cache/probe')
MERGED = Path('out/mods/zz Merged data (keep last)/romfs')
OUT = Path('out/options'); shutil.rmtree(OUT, ignore_errors=True)
def unyaz(b): b = bytes(b); return bytes(oead.yaz0.decompress(b)) if b[:4] == b'Yaz0' else b
def relink(name, **users):
    s = oead.Sarc(unyaz((PROBE / f'{name}.sbactorpack').read_bytes()))
    w = oead.SarcWriter.from_sarc(s)
    for f in s.get_files():
        if f.name.endswith('.bxml'):
            pio = oead.aamp.ParameterIO.from_binary(bytes(f.data))
            lt = pio.objects['LinkTarget']
            for k, v in users.items(): lt.params[k] = oead.aamp.Parameter(v)
            w.files[f.name] = pio.to_binary()
    return oead.yaz0.compress(w.write()[1])
PACKS = {
    'fog': {'WarpTag': dict(AIProgramUser='Dummy'), 'MayoiFogEftLocater': dict(ElinkUser='Dummy'), 'DenseFogEftLocater': dict(ElinkUser='Dummy')},
    'dark': {'DarknessAreaTag': dict(AIProgramUser='Dummy'), 'Darkness_Distance': dict(ElinkUser='Dummy')},
    'bounds': {'AirWall': dict(AIProgramUser='Dummy'), 'AirWallForE3': dict(AIProgramUser='Dummy')},
}
CLIMATE = {'fog': (8, 'LostWoodClimate'), 'dark': (88, 'DarkWoodsClimat')}
boot = oead.Sarc((MERGED / 'Pack/Bootup.pack').read_bytes())
ad_bytes = unyaz(boot.get_file('Ecosystem/AreaData.sbyml').data)
built = {}
for name, users in [(n, u) for k in PACKS.values() for n, u in k.items()]: built[name] = relink(name, **users)
keys = sorted(PACKS)
for r in range(1, len(keys) + 1):
    for combo in itertools.combinations(keys, r):
        d = OUT / '+'.join(combo) / 'romfs'
        (d / 'Actor/Pack').mkdir(parents=True, exist_ok=True)
        for k in combo:
            for n in PACKS[k]: (d / f'Actor/Pack/{n}.sbactorpack').write_bytes(built[n])
        cl = [CLIMATE[k] for k in combo if k in CLIMATE]
        if cl:
            ad = oead.byml.from_binary(ad_bytes)
            for e in ad:
                for num, old in cl:
                    if int(e['AreaNumber']) == num and str(e['Climate']) == old: e['Climate'] = 'NorthHyrulePlainClimate'
            w = oead.SarcWriter.from_sarc(boot)
            w.files['Ecosystem/AreaData.sbyml'] = oead.yaz0.compress(oead.byml.to_binary(ad, False, 2))
            (d / 'Pack').mkdir(exist_ok=True)
            (d / 'Pack/Bootup.pack').write_bytes(bytes(w.write()[1]))
        print('+'.join(combo), sorted(p.name for p in d.rglob('*') if p.is_file()))
