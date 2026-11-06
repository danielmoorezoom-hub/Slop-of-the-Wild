# Builds Slop of the Wild mod folders from BCML mods (.bnp) and plain romfs mods, merging what BCML would merge:
# ActorInfo, GameData (gamedata + savedataformat in Bootup.pack), texts (Bootup_EUen / USen), the size table.
# Output: out/mods/<mod name>/romfs/... (each mod's own files) and out/mods/zz Merged data (keep last)/romfs/...
import io, json, math, os, shutil, sys, tempfile, zlib
from pathlib import Path
import oead, rstb, yaml, pymsyt

RAW = Path('raw'); OUT = Path('out/mods'); VAN = Path('/mnt/user-data/uploads/Slop of the Wild/cache/vanilla')
CUR = Path('/mnt/user-data/uploads/Slop of the Wild/mods')
OLD_UNOB = Path('/home/claude/slopmods/out/mods/Unobtainable items/romfs/Actor/ActorInfo.product.sbyml')
shutil.rmtree('out', ignore_errors=True)

def unyaz(b): b = bytes(b); return bytes(oead.yaz0.decompress(b)) if b[:4] == b'Yaz0' else b
def crc(s): return zlib.crc32(s.encode())

# ---- the mods: (folder name, romfs dir, logs dir or None)
BNP = []
for d in sorted(RAW.iterdir()):
    info = d / 'info.json'
    if info.exists():
        name = json.loads(info.read_text())['name']
        BNP.append((name, d / '01007EF00011E000' / 'romfs', d / 'logs'))
# the mods enabled again (2026-11-06): BNPs and plain romfs mods from the later batch
RAW3 = Path('raw3')
for d in ['ancientbustersword', 'barbarianarmorredesignfor19', 'trirod_switch', 'weaponsofblight_98e28']:
    for sub in sorted((RAW3 / d).iterdir()):
        if (sub / 'info.json').exists():
            BNP.append((json.loads((sub / 'info.json').read_text())['name'], sub / '01007EF00011E000' / 'romfs', sub / 'logs'))
PLAIN = [('OoT Zora Tunic', RAW / 'zora' / 'romfs'), ('OoT Goron Tunic', RAW / 'goronsand' / 'Goron' / 'romfs')]
OWN = [('Ancient Weaponry MkII', RAW3 / 'ancientweaponrymkii_5acf0/01007EF00011E000/romfs'),
       ('Magic Armor (Twilight Princess)', RAW3 / 'magic_armor_from_twilight_princess/48b66c60-magic_armor_from_twilight_princess/atmosphere/contents/01007EF00011E000/romfs'),
       ('Link II Shield', RAW3 / 'linkiishield'),
       # ('TP Master Sword and Shield', removed 2026-11-06)
       ('Epic Master Sword Enhanced', RAW3 / 'epic_mastersword_enhanced_-_dmg250/Epic MasterSword Enhanced - Dmg250/contents/01007EF00011E000/romfs'),
       ('Thunderstorm Rod', RAW3 / 'thunderstormrod_-_ganon_thunder_rod/ThunderstormRod - Ganon Thunder Rod/contents/01007EF00011E000/romfs')]
NEWER = {n for n, *_ in BNP if 'raw3' in str(_[0])} | {n for n, _ in OWN}
SKIP = {'Pack/TitleBG.pack', 'Pack/Title.pack', 'readme.txt', 'Pack/debug.log', 'Pack/Bootup.pack', 'Actor/ActorInfo.product.sbyml', 'System/Resource/ResourceSizeTable.product.srsizetable'}

def safe(n): return ''.join(c for c in n if c not in '<>:"/\\|?*').strip()
copied = {}
for name, romfs, *_ in BNP + PLAIN + OWN:
    n = 0
    for p in romfs.rglob('*'):
        if not p.is_file(): continue
        rel = p.relative_to(romfs).as_posix()
        if rel.count('/') > 2 and rel.startswith('Actor/Pack/'): continue  # (a stray options folder)
        if rel in SKIP or (rel.startswith('Pack/Bootup_') and rel.endswith('.pack')): continue
        dst = OUT / safe(name) / 'romfs' / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(p, dst); n += 1
    copied[name] = n
# the Fierce Deity Sword's option: two-handed, wind slices
FD = RAW3 / 'ultimatefiercedeitysword/eeac194f-ultimatefiercedeitysword/options/Ultimate Fierce Deity Sword WindSlices/01007EF00011E000/romfs'
for p in []:  # (the Fierce Deity Sword: removed 2026-11-06)
    if p.is_file():
        dst = OUT / 'Ultimate Fierce Deity Sword' / 'romfs' / p.relative_to(FD).as_posix(); dst.parent.mkdir(parents=True, exist_ok=True); shutil.copy(p, dst)
print('files per mod:', copied)
# packs the re-enabled BNPs ship partly (only the files they change inside a game pack): the game's other files added
def deep_fill(van, mod):
    zm = mod[:4] == b'Yaz0'
    v, m = unyaz(van), unyaz(mod)
    if v[:4] != b'SARC' or m[:4] != b'SARC': return None
    vs, ms = oead.Sarc(v), oead.Sarc(m)
    w = oead.SarcWriter.from_sarc(ms); mf = {f.name: bytes(f.data) for f in ms.get_files()}; added = 0
    for f in vs.get_files():
        d = bytes(f.data)
        if f.name not in mf: w.files[f.name] = d; added += 1
        elif mf[f.name] != d:
            sub = deep_fill(d, mf[f.name])
            if sub: w.files[f.name] = sub[0]; added += sub[1]
    if not added: return None
    out = bytes(w.write()[1])
    return (oead.yaz0.compress(out) if zm else out), added
for name in sorted(NEWER):
    root = OUT / safe(name) / 'romfs'
    for p in root.rglob('*.sbactorpack') if root.exists() else []:
        rel = p.relative_to(root).as_posix()
        vp = next((x for x in (Path('van3/romfs') / rel, Path('van2/romfs') / rel) if x.exists()), None)
        r = deep_fill(vp.read_bytes(), p.read_bytes()) if vp else None
        if r: p.write_bytes(r[0]); print('  filled', name, rel, '+%d game files' % r[1])

# ---- dict merge as BCML (dicts recursive, everything else replaced)
def dict_merge(base, diff):
    for k, v in diff.items():
        if isinstance(v, (dict, oead.byml.Hash)) and k in base and isinstance(base[k], (dict, oead.byml.Hash)):
            dict_merge(base[k], v)
        else:
            base[k] = v
def diff_node(van, mod):  # what mod changes in van (dicts value by value)
    if isinstance(van, oead.byml.Hash) and isinstance(mod, oead.byml.Hash):
        out = oead.byml.Hash()
        for k, v in mod.items():
            if k not in van: out[k] = v
            elif van[k] != v:
                if isinstance(v, oead.byml.Hash) and isinstance(van[k], oead.byml.Hash): out[k] = diff_node(van[k], v)
                else: out[k] = v
        return out
    return mod

# ---- ActorInfo
van_ai = oead.byml.from_binary(unyaz((VAN / 'ActorInfo.product.sbyml').read_bytes()))
actors = {crc(str(a['name'])): a for a in van_ai['Actors']}
van_hashes = set(actors)
def actorinfo_diff_of(path, new_only, only=None):
    m = oead.byml.from_binary(unyaz(Path(path).read_bytes()))
    d = {}
    for a in m['Actors']:
        if only is not None and str(a['name']) not in only: continue
        h = crc(str(a['name']))
        if h not in actors: d[h] = a
        elif not new_only:
            x = diff_node(actors[h], a)
            if len(x): d[h] = x
    return d
ai_diffs = [('Unobtainable items', actorinfo_diff_of(OLD_UNOB, False))]
for name, romfs, logs in BNP:
    f = logs / 'actorinfo.yml'
    if f.exists():
        y = oead.byml.from_text(f.read_text('utf-8'))
        ai_diffs.append((name, {int(k): v for k, v in y.items()}))
for name, romfs in PLAIN:
    f = romfs / 'Actor/ActorInfo.product.sbyml'
    own = {p.name[:-len('.sbactorpack')] for p in (romfs / 'Actor/Pack').glob('*.sbactorpack')}
    if f.exists(): ai_diffs.append((name, actorinfo_diff_of(f, True, own)))
for name, romfs in OWN:  # (their own actors' changes count too)
    f = romfs / 'Actor/ActorInfo.product.sbyml'
    own = {p.name[:-len('.sbactorpack')] for p in (romfs / 'Actor/Pack').glob('*.sbactorpack')}
    if f.exists(): ai_diffs.append((name, actorinfo_diff_of(f, False, own)))
new_actors = []
inst = {}
for name, d in ai_diffs:
    for h, a in d.items():
        if 'instSize' in a: inst[h] = max(inst.get(h, 0), int(a['instSize']))
        if h in actors and name.startswith('OoT ') and h not in van_hashes: actors[h] = oead.byml.Hash(dict(a))  # (its own new actor)
        elif h in actors: dict_merge(actors[h], a)
        else:
            actors[h] = oead.byml.Hash(dict(a)); new_actors.append((name, str(a.get('name', h))))
for h, v in inst.items(): actors[h]['instSize'] = oead.S32(v)
# every armour piece dyeable (the ArmorDye tag the dye shop looks for)
DYE = zlib.crc32(b'ArmorDye'); dyed = 0
for h, a in actors.items():
    if str(a.get('profile', '')).startswith('Armor'):
        tg = a['tags'] if 'tags' in a else oead.byml.Hash()
        k = 'tag%08x' % DYE
        if k not in tg:
            tg[k] = oead.S32(DYE) if DYE < 2**31 else oead.U32(DYE); dyed += 1
        a['tags'] = tg
print('armour made dyeable:', dyed)
missing = [h for h, a in actors.items() if 'name' not in a]
assert not missing, missing
order = sorted(actors)
ai = oead.byml.Hash({'Actors': oead.byml.Array([actors[h] for h in order]),
                     'Hashes': oead.byml.Array([oead.S32(h) if h < 2**31 else oead.U32(h) for h in order])})
ai_bin = oead.byml.to_binary(ai, False, 2)
M = OUT / 'zz Merged data (keep last)' / 'romfs'
(M / 'Actor').mkdir(parents=True, exist_ok=True)
(M / 'Actor/ActorInfo.product.sbyml').write_bytes(oead.yaz0.compress(ai_bin))
print(f'ActorInfo: {len(van_ai["Actors"])} -> {len(order)} actors; new:', len(new_actors))

# ---- GameData
boot = oead.Sarc((VAN / 'Bootup.pack').read_bytes())
gd = oead.Sarc(unyaz(boot.get_file('GameData/gamedata.ssarc').data))
gd_files = sorted(gd.get_files(), key=lambda f: f.name)
types = {}
for f in gd_files:
    y = oead.byml.from_binary(f.data)
    for t, arr in y.items():
        types.setdefault(t, {})
        for e in arr: types[t][str(e['DataName'])] = e
van_names = {t: set(v) for t, v in types.items()}
def gd_of_pack(path):  # every flag of a Bootup.pack, by type
    b = oead.Sarc(Path(path).read_bytes() + b'\0' * 0)
    g = oead.Sarc(unyaz(b.get_file('GameData/gamedata.ssarc').data))
    out = {}
    for f in g.get_files():
        for t, arr in oead.byml.from_binary(f.data).items():
            for e in arr: out.setdefault(t, {})[str(e['DataName'])] = e
    return out
gd_adds = 0
def gd_apply(add, dele=()):
    global gd_adds
    for t, entries in add.items():
        types.setdefault(t, {})
        for k, e in entries.items():
            if k not in types[t]: gd_adds += 1
            types[t][k] = e
    for t, names in dele:
        for k in names: types.get(t, {}).pop(k, None)
# Raised slot limits: its changes
rsl = gd_of_pack(CUR / 'Raised slot limits/romfs/Pack/Bootup.pack')
gd_apply({t: {k: e for k, e in v.items() if types.get(t, {}).get(k) != e} for t, v in rsl.items()})
for name, romfs, logs in BNP:
    f = logs / 'gamedata.yml'
    if not f.exists(): continue
    y = oead.byml.from_text(f.read_text('utf-8'))
    gd_apply({t: {str(k): e for k, e in v['add'].items()} for t, v in y.items()}, [(t, [str(x) for x in v['del']]) for t, v in y.items()])
GORON_FULL = 38146776
def padded(path, full=None):
    d = Path(path).read_bytes()
    return d + b'\0' * (full - len(d)) if full and len(d) < full else d
for name, romfs in PLAIN + OWN:
    f = romfs / 'Pack/Bootup.pack'
    if not f.exists(): continue
    tmp = Path(tempfile.mktemp()); tmp.write_bytes(padded(f, GORON_FULL if 'Goron' in name else None))
    try:
        g = gd_of_pack(tmp)
        gd_apply({t: {k: e for k, e in v.items() if k not in van_names.get(t, ())} for t, v in g.items()})
    except Exception as ex: print('  gamedata of', name, 'unreadable:', ex)
w = oead.SarcWriter(oead.Endianness.Little)
for t, entries in types.items():
    arr = list(entries.values())
    for i in range(max(1, math.ceil(len(arr) / 4096))):
        w.files[f'/{t}_{i}.bgdata'] = oead.byml.to_binary(oead.byml.Hash({t: oead.byml.Array(arr[i*4096:(i+1)*4096])}), False, 2)
gamedata_bytes = bytes(w.write()[1])
print('gamedata flags added:', gd_adds)

# ---- savedataformat
sv = oead.Sarc(unyaz(boot.get_file('GameData/savedataformat.ssarc').data))
sv_files = sorted(sv.get_files(), key=lambda f: f.name)
first = oead.byml.from_binary(sv_files[0].data)
entries = {int(e['HashValue']): e for f in sv_files[:-2] for e in oead.byml.from_binary(f.data)['file_list'][1]}
van_sv = set(entries)
dels = set()
for name, romfs, logs in BNP:
    f = logs / 'savedata.yml'
    if not f.exists(): continue
    y = oead.byml.from_text(f.read_text('utf-8'))
    for e in y['add']: entries[int(e['HashValue'])] = e
    dels |= {int(x) for x in y['del']}
def sv_of_pack(path):
    b = oead.Sarc(Path(path).read_bytes())
    s = oead.Sarc(unyaz(b.get_file('GameData/savedataformat.ssarc').data))
    fs = sorted(s.get_files(), key=lambda f: f.name)
    return [e for f in fs[:-2] for e in oead.byml.from_binary(f.data)['file_list'][1]]
for name, romfs in PLAIN + OWN:
    f = romfs / 'Pack/Bootup.pack'
    if not f.exists(): continue
    tmp = Path(tempfile.mktemp()); tmp.write_bytes(padded(f, GORON_FULL if 'Goron' in name else None))
    try:
        for e in sv_of_pack(tmp):
            if int(e['HashValue']) not in van_sv: entries[int(e['HashValue'])] = e
    except Exception as ex: print('  savedata of', name, 'unreadable:', ex)
for h in dels: entries.pop(h, None)
# every gamedata flag that's saved needs a savedata entry
for t, es in types.items():
    for k, e in es.items():
        if e.get('IsSave') and k not in van_names.get(t, ()):
            h = int(e['HashValue']) & 0xffffffff
            hs = h - 2**32 if h >= 2**31 else h
            if hs not in entries: entries[hs] = oead.byml.Hash({'DataName': k, 'HashValue': oead.S32(hs)})
merged = sorted(entries.values(), key=lambda e: int(e['HashValue']))
w = oead.SarcWriter(oead.Endianness.Little)
n = math.ceil(len(merged) / 8192)
for i in range(n):
    w.files[f'/saveformat_{i}.bgsvdata'] = oead.byml.to_binary(oead.byml.Hash({
        'file_list': oead.byml.Array([first['file_list'][0], oead.byml.Array(merged[i*8192:(i+1)*8192])]),
        'save_info': first['save_info']}), False, 2)
w.files[f'/saveformat_{n}.bgsvdata'] = bytes(sv_files[-2].data)
w.files[f'/saveformat_{n+1}.bgsvdata'] = bytes(sv_files[-1].data)
savedata_bytes = bytes(w.write()[1])
print('savedata entries:', len(van_sv), '->', len(merged))

bw = oead.SarcWriter.from_sarc(boot)
# other files mods change inside Bootup.pack (music lists and the like): theirs
van_boot = {f.name: bytes(f.data) for f in boot.get_files()}
taken = set()  # (first mod wins: the music mod sorts before the ones that only carry an old copy)
for name, romfs, logs in BNP:
    f = romfs / 'Pack/Bootup.pack'
    if not f.exists(): continue
    for x in oead.Sarc(f.read_bytes()).get_files():
        if not x.name.startswith('Sound/'): continue  # (other files differ only because a mod was made on an older game version)
        if van_boot.get(x.name) != bytes(x.data) and x.name not in taken:
            taken.add(x.name)
            bw.files[x.name] = bytes(x.data); print('  Bootup.pack:', x.name, 'from', name)
bw.files['GameData/gamedata.ssarc'] = oead.yaz0.compress(gamedata_bytes)
bw.files['GameData/savedataformat.ssarc'] = oead.yaz0.compress(savedata_bytes)
(M / 'Pack').mkdir(parents=True, exist_ok=True)
(M / 'Pack/Bootup.pack').write_bytes(bytes(bw.write()[1]))

# ---- texts
def msbt_files(pack_path, lang):
    p = oead.Sarc(Path(pack_path).read_bytes())
    return oead.Sarc(unyaz(p.get_file(f'Message/Msg_{lang}.product.ssarc').data))
text_diffs = {}  # file -> entries
def add_texts(d):
    for file, ents in d.items():
        text_diffs.setdefault(file, {}).update(ents)
for name, romfs, logs in BNP:
    f = logs / 'texts.json'
    if not f.exists(): continue
    t = json.loads(f.read_text('utf-8'))
    lang = next((l for l in ('EUen', 'USen') if l in t), next(iter(t), None))
    if lang: add_texts(t[lang])
tmpd = Path(tempfile.mkdtemp())
def parse(data):
    p = tmpd / 'x.msbt'; p.write_bytes(bytes(data)); return pymsyt.parse_msbt(p)
for name, romfs in PLAIN + OWN:
    f = romfs / 'Pack/Bootup_USen.pack'
    if not f.exists(): continue
    mod = msbt_files(f, 'USen'); van = msbt_files(VAN / 'Bootup_USen.pack', 'USen')
    vf = {x.name: bytes(x.data) for x in van.get_files()}
    for x in mod.get_files():
        if vf.get(x.name) == bytes(x.data): continue
        me = parse(x.data)['entries']; ve = parse(vf[x.name])['entries'] if x.name in vf else {}
        new = {k: v for k, v in me.items() if k not in ve}
        if new: add_texts({x.name.replace('.msbt', '.msyt'): new})
print('text files changed:', len(text_diffs), sum(len(v) for v in text_diffs.values()), 'entries')
for lang in ('EUen', 'USen'):
    pack = oead.Sarc((VAN / f'Bootup_{lang}.pack').read_bytes())
    msg = oead.Sarc(unyaz(pack.get_file(f'Message/Msg_{lang}.product.ssarc').data))
    mw = oead.SarcWriter.from_sarc(msg)
    names = {x.name for x in msg.get_files()}
    for file, ents in text_diffs.items():
        fn = file.replace('.msyt', '.msbt')
        if fn not in names: print('  no', fn, 'in', lang, '- skipped'); continue
        y = parse(msg.get_file(fn).data)
        for k, v in ents.items(): y['entries'][k] = v
        out = tmpd / 'o.msbt'
        pymsyt.write_msbt(y, out, platform='switch')
        mw.files[fn] = out.read_bytes()
    msg_bytes = bytes(mw.write()[1])
    pw = oead.SarcWriter.from_sarc(pack)
    pw.files[f'Message/Msg_{lang}.product.ssarc'] = oead.yaz0.compress(msg_bytes)
    (M / f'Pack/Bootup_{lang}.pack').write_bytes(bytes(pw.write()[1]))
    globals()['msg_' + lang] = msg_bytes

# ---- size table: what's installed now, each mod's entries, the merged files
tbl = rstb.ResourceSizeTable(unyaz((CUR / 'zz Size table (keep last)/romfs/System/Resource/ResourceSizeTable.product.srsizetable').read_bytes()), be=False)
vtbl = rstb.ResourceSizeTable(unyaz((VAN / 'ResourceSizeTable.product.srsizetable').read_bytes()), be=False)
def setmax(k, v):
    old = tbl.get_size(k) if tbl.is_in_table(k) else 0
    if v > old: tbl.set_size(k, v)
for name, romfs, logs in BNP:
    f = logs / 'rstb.json'
    if not f.exists(): continue
    for k, v in json.loads(f.read_text()).items():
        if k.startswith('System/Resource/') or k.startswith('GameData/'): continue
        if v: setmax(k, int(v))
        elif tbl.is_in_table(k): tbl.delete_entry(k)  # (0: the game sizes it from the file, as BCML does)
for name, romfs in PLAIN + OWN:
    f = romfs / 'System/Resource/ResourceSizeTable.product.srsizetable'
    if not f.exists(): continue
    mt = rstb.ResourceSizeTable(unyaz(f.read_bytes()), be=False)
    # entries for this mod's own files
    for p in romfs.rglob('*'):
        if not p.is_file(): continue
        rel = p.relative_to(romfs).as_posix()
        r = rel.rsplit('.', 1); key = r[0] + '.' + (r[1][1:] if r[1].startswith('s') and r[1] != 'sarc' else r[1])
        if rel in SKIP or rel.startswith('Pack/'): continue
        if mt.is_in_table(key): setmax(key, mt.get_size(key))
calc = rstb.SizeCalculator()
for k, data, ext in [('GameData/gamedata.sarc', gamedata_bytes, '.sarc'), ('GameData/savedataformat.sarc', savedata_bytes, '.sarc'),
                     ('Message/Msg_EUen.product.sarc', msg_EUen, '.sarc'), ('Message/Msg_USen.product.sarc', msg_USen, '.sarc')]:
    v = calc.calculate_file_size_with_ext(data, False, ext)
    print('RSTB', k, tbl.get_size(k) if tbl.is_in_table(k) else None, '->', v)
    tbl.set_size(k, max(v, vtbl.get_size(k) if vtbl.is_in_table(k) else 0))
b = io.BytesIO(); tbl.write(b, be=False)
(M / 'System/Resource').mkdir(parents=True, exist_ok=True)
(M / 'System/Resource/ResourceSizeTable.product.srsizetable').write_bytes(oead.yaz0.compress(b.getvalue()))
json.dump(new_actors, open('out/new_actors.json', 'w'), indent=0)
print('done')
