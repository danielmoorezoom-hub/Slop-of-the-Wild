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

# ---- the mods. BNP: (name, base romfs, logs, aoc romfs); PLAIN: (name, base romfs, aoc romfs, kind)
# kind: 'old' (made on an older game version: only what's new counts), 'own' (its own actors' changes count too),
# 'full' (current game version, an overhaul: everything it changes counts; lowest priority, so the rest win)
RAW3 = Path('raw3')
SKIP_BNP = {'royal_knight_armor_for_19_blue_', 'champion_s_cap', 'islands_expansion_dlc_v12_-_switch'}  # (blue: same armour as red, kept aside; the cap: installed already)
BNP = []
def add_bnp(d):
    info = d / 'info.json'
    name = json.loads(info.read_text())['name']
    BNP.append((name, d / '01007EF00011E000' / 'romfs', d / 'logs', d / '01007EF00011F001' / 'romfs'))
for d in sorted(RAW.iterdir()):
    if (d / 'info.json').exists(): add_bnp(d)
for d in sorted(RAW3.iterdir()):
    if d.name in SKIP_BNP or d.name == 'relics': continue
    for sub in sorted(d.iterdir()) if d.is_dir() else []:
        if (sub / 'info.json').exists(): add_bnp(sub)
PLAIN = [('OoT Zora Tunic', RAW / 'zora' / 'romfs', None, 'old'),
         ('OoT Goron Tunic', RAW / 'goronsand' / 'Goron' / 'romfs', None, 'old'),
         ('Ancient Weaponry MkII', RAW3 / 'ancientweaponrymkii_5acf0/01007EF00011E000/romfs', None, 'own'),
         ('Magic Armor (Twilight Princess)', RAW3 / 'magic_armor_from_twilight_princess/48b66c60-magic_armor_from_twilight_princess/atmosphere/contents/01007EF00011E000/romfs', None, 'own'),
         ('Link II Shield', RAW3 / 'linkiishield', None, 'own'),
         ('TP Master Sword and Shield', RAW3 / 'tpmasterswordandshield/romfs', None, 'own'),
         ('Epic Master Sword Enhanced', RAW3 / 'epic_mastersword_enhanced_-_dmg250/Epic MasterSword Enhanced - Dmg250/contents/01007EF00011E000/romfs', None, 'own'),
         ('Thunderstorm Rod', RAW3 / 'thunderstormrod_-_ganon_thunder_rod/ThunderstormRod - Ganon Thunder Rod/contents/01007EF00011E000/romfs', None, 'own'),
         ('0 Relics of the Past', RAW3 / 'relics/01007EF00011E000/romfs', RAW3 / 'relics/01007EF00011F001/romfs', 'full')]
ONLY_RELICS = os.environ.get('ONLY_RELICS') == '1'  # (just the overhaul: every other mod left out)
if ONLY_RELICS: BNP = []; PLAIN = [m for m in PLAIN if m[3] == 'full']
SKIP = {'Pack/Bootup.pack', 'Actor/ActorInfo.product.sbyml', 'System/Resource/ResourceSizeTable.product.srsizetable',
        'Pack/TitleBG.pack', 'Pack/Title.pack', 'readme.txt', 'Pack/debug.log'}
import re
UNIT = re.compile(r'^Map/(MainField|AocField)/([A-J]-\d|_DistanceView)/[^/]+_(Static|Dynamic)\.smubin$')
AOC_SKIP = {'Pack/AocMainField.pack', 'System/AocVersion.txt'}

def rkey(rel):  # the size table's name for a file: Yaz0 extensions lose their 's'
    r = rel.rsplit('.', 1)
    return r[0] + '.' + (r[1][1:] if len(r) > 1 and r[1].startswith('s') and r[1] != 'sarc' else r[-1])
def safe(n): return ''.join(c for c in n if c not in '<>:"/\\|?*').strip()
copied = {}; owner = {}
def copy_tree(name, root, sub, aoc):
    n = 0
    if not root or not root.exists(): return 0
    for p in root.rglob('*'):
        if not p.is_file(): continue
        rel = p.relative_to(root).as_posix()
        if rel.count('/') > 2 and rel.startswith('Actor/Pack/'): continue  # (a stray options folder)
        if aoc and (rel in AOC_SKIP or UNIT.match(rel)): continue
        if not aoc and (rel in SKIP or (rel.startswith('Pack/Bootup_') and rel.endswith('.pack'))): continue
        if aoc and (sub, rel) in owner: continue  # (two mods' copies of the same DLC file: the first one)
        dst = OUT / safe(name) / sub / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(p, dst); n += 1
        owner.setdefault((sub, rel), []).append(name)
    return n
for name, romfs, logs, aoc in BNP:
    copied[name] = copy_tree(name, romfs, 'romfs', False) + copy_tree(name, aoc, 'aoc', True)
for name, romfs, aoc, kind in PLAIN:
    copied[name] = copy_tree(name, romfs, 'romfs', False) + copy_tree(name, aoc, 'aoc', True)
# BNP options picked: the Fierce Deity Sword, two-handed with wind slices
FD = RAW3 / 'ultimatefiercedeitysword/eeac194f-ultimatefiercedeitysword/options/Ultimate Fierce Deity Sword WindSlices/01007EF00011E000/romfs'
if not ONLY_RELICS: copied['Ultimate Fierce Deity Sword'] += copy_tree('Ultimate Fierce Deity Sword', FD, 'romfs', False)
print('files per mod:', copied)

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
ai_diffs = [] if ONLY_RELICS else [('Unobtainable items', actorinfo_diff_of(OLD_UNOB, False))]
for name, romfs, logs, aoc in BNP:
    f = logs / 'actorinfo.yml'
    if f.exists():
        y = oead.byml.from_text(f.read_text('utf-8'))
        ai_diffs.append((name, {int(k): v for k, v in y.items()}))
for name, romfs, aoc, kind in sorted(PLAIN, key=lambda m: m[3] != 'full'):  # (the overhaul first: the rest win)
    f = romfs / 'Actor/ActorInfo.product.sbyml'
    own = {p.name[:-len('.sbactorpack')] for p in (romfs / 'Actor/Pack').glob('*.sbactorpack')}
    if f.exists(): ai_diffs.insert(1 if kind == 'full' else len(ai_diffs), (name, actorinfo_diff_of(f, kind == 'old', own)))
    if f.exists() and kind == 'full':  # (and its heap sizes for every actor: it changes ones whose packs live in
        # TitleBG.pack too, Link's among them, and their bigger files need the room: out of memory loading a save)
        for a in oead.byml.from_binary(unyaz(f.read_bytes()))['Actors']:
            h = crc(str(a['name']))
            if h in actors and 'instSize' in a and int(a['instSize']) > int(actors[h].get('instSize', 0)):
                ai_diffs.insert(1, (name + ' heap', {h: oead.byml.Hash({'instSize': a['instSize']})}))
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
# actors whose packs a mod replaces: a quarter more heap than the most any table gives (their files are often bigger
# than the game's, and a mod's own table may not say so)
bumped = 0
for (sub, rel), who in owner.items():
    if sub != 'romfs' or not rel.startswith('Actor/Pack/') or not rel.endswith('.sbactorpack'): continue
    h = crc(rel[len('Actor/Pack/'):-len('.sbactorpack')])
    if h in actors and 'instSize' in actors[h]:
        actors[h]['instSize'] = oead.S32(int(int(actors[h]['instSize']) * 1.25)); bumped += 1
_vt = {f.name: bytes(f.data) for f in oead.Sarc(Path('/home/claude/slopmods/van/TitleBG.pack').read_bytes()).get_files() if f.name.startswith('Actor/Pack/')}
for f in oead.Sarc((RAW3 / 'relics/01007EF00011E000/romfs/Pack/TitleBG.pack').read_bytes()).get_files():
    if f.name in _vt and _vt[f.name] != bytes(f.data):
        h = crc(f.name[len('Actor/Pack/'):-len('.sbactorpack')])
        if h in actors and 'instSize' in actors[h]:
            actors[h]['instSize'] = oead.S32(int(int(actors[h]['instSize']) * 1.25)); bumped += 1
print('actor heaps grown for mods\' packs:', bumped)
# every armour piece dyeable (the ArmorDye tag the dye shop looks for)
DYE = zlib.crc32(b'ArmorDye'); dyed = 0
for h, a in actors.items():
    if not ONLY_RELICS and str(a.get('profile', '')).startswith('Armor'):
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
rsl = {} if ONLY_RELICS else gd_of_pack(CUR / 'Raised slot limits/romfs/Pack/Bootup.pack')
gd_apply({t: {k: e for k, e in v.items() if types.get(t, {}).get(k) != e} for t, v in rsl.items()})
for name, romfs, logs, aoc in BNP:
    f = logs / 'gamedata.yml'
    if not f.exists(): continue
    y = oead.byml.from_text(f.read_text('utf-8'))
    gd_apply({t: {str(k): e for k, e in v['add'].items()} for t, v in y.items()}, [(t, [str(x) for x in v['del']]) for t, v in y.items()])
GORON_FULL = 38146776
def padded(path, full=None):
    d = Path(path).read_bytes()
    return d + b'\0' * (full - len(d)) if full and len(d) < full else d
for name, romfs, aoc, kind in PLAIN:
    f = romfs / 'Pack/Bootup.pack'
    if not f.exists(): continue
    tmp = Path(tempfile.mktemp()); tmp.write_bytes(padded(f, GORON_FULL if 'Goron' in name else None))
    try:
        g = gd_of_pack(tmp)
        if kind == 'full':  # (current version: its changed flags too)
            gd_apply({t: {k: e for k, e in v.items() if k not in van_names.get(t, ()) or types[t].get(k) != e} for t, v in g.items()})
        else:
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
for name, romfs, logs, aoc in BNP:
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
for name, romfs, aoc, kind in PLAIN:
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
for name, romfs, logs, aoc in BNP:
    f = romfs / 'Pack/Bootup.pack'
    if not f.exists(): continue
    for x in oead.Sarc(f.read_bytes()).get_files():
        if not x.name.startswith('Sound/'): continue  # (other files differ only because a mod was made on an older game version)
        if van_boot.get(x.name) != bytes(x.data) and x.name not in taken:
            taken.add(x.name)
            bw.files[x.name] = bytes(x.data); print('  Bootup.pack:', x.name, 'from', name)
for name, romfs, aoc, kind in PLAIN:  # the overhaul's other changes, where no other mod made one
    f = romfs / 'Pack/Bootup.pack'
    if kind != 'full' or not f.exists(): continue
    k = 0
    for x in oead.Sarc(f.read_bytes()).get_files():
        if x.name.startswith(('GameData/', 'SLink2/', 'ELink2/')) or x.name in taken: continue  # (its XLink tables are in the 32-bit layout: the Switch game crashes reading them)
        if van_boot.get(x.name) != bytes(x.data): bw.files[x.name] = bytes(x.data); taken.add(x.name); k += 1
    print('  Bootup.pack:', k, 'files from', name)
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
for name, romfs, logs, aoc in BNP:
    f = logs / 'texts.json'
    if not f.exists(): continue
    t = json.loads(f.read_text('utf-8'))
    lang = next((l for l in ('EUen', 'USen') if l in t), next(iter(t), None))
    if lang: add_texts(t[lang])
tmpd = Path(tempfile.mkdtemp())
new_msg_files = {}  # message files the game hasn't got at all: a mod's own
def parse(data):
    p = tmpd / 'x.msbt'; p.write_bytes(bytes(data)); return pymsyt.parse_msbt(p)
for name, romfs, aoc, kind in sorted(PLAIN, key=lambda m: m[3] != 'full'):
    lang = 'EUen' if (romfs / 'Pack/Bootup_EUen.pack').exists() else 'USen'
    f = romfs / f'Pack/Bootup_{lang}.pack'
    if not f.exists(): continue
    mod = msbt_files(f, lang); van = msbt_files(VAN / f'Bootup_{lang}.pack', lang)
    vf = {x.name: bytes(x.data) for x in van.get_files()}
    for x in mod.get_files():
        if vf.get(x.name) == bytes(x.data): continue
        if x.name not in vf: new_msg_files.setdefault(x.name, bytes(x.data)); continue
        me = parse(x.data)['entries']; ve = parse(vf[x.name])['entries'] if x.name in vf else {}
        new = {k: v for k, v in me.items() if k not in ve or (kind == 'full' and ve[k] != v)}
        if new: add_texts({x.name.replace('.msbt', '.msyt'): new})
print('new message files:', len(new_msg_files)); print('text files changed:', len(text_diffs), sum(len(v) for v in text_diffs.values()), 'entries')
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
    for fn, data in new_msg_files.items(): mw.files[fn] = data
    msg_bytes = bytes(mw.write()[1])
    pw = oead.SarcWriter.from_sarc(pack)
    pw.files[f'Message/Msg_{lang}.product.ssarc'] = oead.yaz0.compress(msg_bytes)
    (M / f'Pack/Bootup_{lang}.pack').write_bytes(bytes(pw.write()[1]))
    globals()['msg_' + lang] = msg_bytes


# ---- maps: each unit the game's (the DLC's loose copy) with every mod's changes laid over it, by object HashId;
# BNPs say theirs in logs/map.yml, a plain mod's full units are compared with the game's. The DLC's
# AocMainField.pack is emptied so the game reads the loose units (as BCML does).
VAN2 = Path('van2')
MA = OUT / 'zz Merged data (keep last)' / 'aoc'
units = {}  # rel -> byml
def load_unit(rel):
    if rel not in units:
        p = VAN2 / 'aoc1' / rel
        if not p.exists(): return None
        units[rel] = oead.byml.from_binary(unyaz(p.read_bytes()))
    return units[rel]
def unit_rel(key):  # "E-7_Static" -> Map/MainField/E-7/E-7_Static.smubin
    sec = key.rsplit('_', 1)[0]
    return f'Map/MainField/{sec}/{key}.smubin'
def apply_unit(rel, diff, who):
    u = load_unit(rel)
    if u is None: print('  no game map unit', rel, 'for', who); return
    for part in ('Objs', 'Rails'):
        if part not in diff: continue
        d = diff[part]
        cur = {int(o['HashId']): o for o in (u[part] if part in u else [])}
        for h in d.get('del', []): cur.pop(int(h), None)
        for h, o in d.get('mod', {}).items(): cur[int(h)] = o
        for o in d.get('add', []): cur.setdefault(int(o['HashId']), o)
        u[part] = oead.byml.Array([cur[h] for h in sorted(cur)])
def diff_unit(van, mod):
    out = {}
    for part in ('Objs', 'Rails'):
        v = {int(o['HashId']): o for o in (van[part] if part in van else [])}
        m = {int(o['HashId']): o for o in (mod[part] if part in mod else [])}
        out[part] = {'add': [m[h] for h in m if h not in v], 'mod': {h: m[h] for h in m if h in v and v[h] != m[h]},
                     'del': [h for h in v if h not in m]}
    return out
for name, romfs, aoc, kind in PLAIN:  # (the overhaul first)
    if not aoc or not aoc.exists(): continue
    k = 0
    for p in sorted(aoc.rglob('*.smubin')):
        rel = p.relative_to(aoc).as_posix()
        if not UNIT.match(rel): continue
        van = load_unit(rel)
        mod = oead.byml.from_binary(unyaz(p.read_bytes()))
        if van is None: units[rel] = mod; k += 1; continue
        d = diff_unit(van, mod)
        if any(d[x]['add'] or d[x]['mod'] or d[x]['del'] for x in d): apply_unit(rel, d, name); k += 1
    print('  maps:', k, 'units from', name)
for name, romfs, logs, aoc in BNP:
    f = logs / 'map.yml'
    if not f.exists(): continue
    y = oead.byml.from_text(f.read_text('utf-8'))
    for key, d in y.items(): apply_unit(unit_rel(str(key)), d, name)
    print('  maps:', len(y), 'units from', name)
map_sizes = {}
for rel, u in units.items():
    data = oead.byml.to_binary(u, False, 2)
    dst = MA / rel; dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(oead.yaz0.compress(data))
    map_sizes['Aoc/0010/' + rkey(rel)] = bytes(data)
(MA / 'Pack').mkdir(parents=True, exist_ok=True)
(MA / 'Pack/AocMainField.pack').write_bytes(b'')
print('map units merged:', len(units))

# ---- packs two mods both change: the game's with each one's changed files (the overhaul first, so the rest win)
def merge_sarc(van_bytes, mods, removals=True):
    van = oead.Sarc(unyaz(van_bytes)); w = oead.SarcWriter.from_sarc(van)
    vf = {f.name: bytes(f.data) for f in van.get_files()}
    for who, data in mods:
        m = {f.name: bytes(f.data) for f in oead.Sarc(unyaz(data)).get_files()}
        for n, d in m.items():
            if vf.get(n) != d: w.files[n] = d
        if removals:
            for n in vf:
                if n not in m and n in w.files: del w.files[n]
    return bytes(w.write()[1])
# TitleBG: the overhaul's changes, then the TOTK hair's
tbg = merge_sarc(Path('/home/claude/slopmods/van/TitleBG.pack').read_bytes(),
                 [('relics', (RAW3 / 'relics/01007EF00011E000/romfs/Pack/TitleBG.pack').read_bytes())])
if not ONLY_RELICS: tbg = merge_sarc(tbg, [('hair', Path('/home/claude/slopmods/raw/totkhair/01007EF00011E000/romfs/Pack/TitleBG.pack').read_bytes())], removals=False)
(M / 'Pack/TitleBG.pack').write_bytes(tbg)
# Title: the islands' title screen (the overhaul's changes the same file)
# the title screen: the overhaul's (the Islands Expansion's is left out with it)
rt = RAW3 / 'relics/01007EF00011E000/romfs/Pack/Title.pack'
if rt.exists(): (M / 'Pack/Title.pack').write_bytes(merge_sarc((VAN2 / 'romfs/Pack/Title.pack').read_bytes(), [('relics', rt.read_bytes())]))
elif (M / 'Pack/Title.pack').exists(): (M / 'Pack/Title.pack').unlink()
# actor packs more than one of the user's own mods carry (the overhaul just loses those)
conf = {}
for (sub, rel), who in owner.items():
    real = [w for w in who if not w.startswith('0 ')]
    if sub == 'romfs' and rel.startswith('Actor/Pack/') and len(real) > 1: conf[rel] = real
for rel, who in conf.items():
    vp = VAN2 / 'romfs' / rel
    if not vp.exists(): print('  no game copy of', rel, '- the last of', who, 'wins'); continue
    data = merge_sarc(vp.read_bytes(), [(w, (OUT / safe(w) / 'romfs' / rel).read_bytes()) for w in who], removals=False)
    (M / rel).parent.mkdir(parents=True, exist_ok=True)
    (M / rel).write_bytes(oead.yaz0.compress(data))
    print('  merged', rel, 'from', who)

# ---- packs mods ship partly: BNPs (and BCML-made folders) keep only the files a mod changed inside a game pack (its
# Title.sblarc with just the new picture, an NPC's pack with one file of 68). Served like that the game finds the rest
# missing (no Title_00 layout: crash at the title screen). Each such pack gets the game's files it leaves out, nested
# archives too. The overhaul's packs are whole (their missing files are removals), and Bootup/TitleBG are merged above.
VAN3 = Path('van3')
def deep_fill(van, mod):
    zv, zm = van[:4] == b'Yaz0', mod[:4] == b'Yaz0'
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
filled = 0
for d in sorted(OUT.iterdir()):
    if d.name.startswith('0 '): continue
    for sub, vsub in (('romfs', 'romfs'), ('aoc', 'aoc1')):
        root = d / sub
        if not root.exists(): continue
        for p in root.rglob('*'):
            rel = p.relative_to(root).as_posix()
            if not p.is_file() or rel in ('Pack/Bootup.pack', 'Pack/TitleBG.pack') or rel.startswith('Pack/Bootup_'): continue
            vp = next((x for x in (VAN3 / sub / rel, VAN2 / vsub / rel) if x.exists()), None)
            if not vp: continue
            r = deep_fill(vp.read_bytes(), p.read_bytes())
            if r: p.write_bytes(r[0]); filled += 1; print('  filled', d.name, rel, '+%d game files' % r[1])
print('packs filled in with the game files they left out:', filled)

# ---- size table: what's installed now, each mod's entries, the merged files
tbl = rstb.ResourceSizeTable(unyaz((CUR / 'zz Size table (keep last)/romfs/System/Resource/ResourceSizeTable.product.srsizetable').read_bytes()), be=False)
vtbl = rstb.ResourceSizeTable(unyaz((VAN / 'ResourceSizeTable.product.srsizetable').read_bytes()), be=False)
def setmax(k, v):
    old = tbl.get_size(k) if tbl.is_in_table(k) else 0
    if v > old: tbl.set_size(k, v)
for name, romfs, logs, aoc in BNP:
    f = logs / 'rstb.json'
    if not f.exists(): continue
    for k, v in json.loads(f.read_text()).items():
        if k.startswith('System/Resource/') or k.startswith('GameData/'): continue
        if v: setmax(k, int(v))
        elif tbl.is_in_table(k): tbl.delete_entry(k)  # (0: the game sizes it from the file, as BCML does)
for name, romfs, aoc, kind in PLAIN:
    f = romfs / 'System/Resource/ResourceSizeTable.product.srsizetable'
    if not f.exists(): continue
    mt = rstb.ResourceSizeTable(unyaz(f.read_bytes()), be=False)
    # entries for this mod's own files
    for root, pre in ((romfs, ''), (aoc, 'Aoc/0010/')):
        if not root or not root.exists(): continue
        for p in root.rglob('*'):
            if not p.is_file(): continue
            rel = p.relative_to(root).as_posix()
            if rel in SKIP or rel.startswith('Pack/') or UNIT.match(rel): continue
            key = pre + rkey(rel)
            if mt.is_in_table(key): setmax(key, mt.get_size(key))
calc = rstb.SizeCalculator()
for k, data, ext in [('GameData/gamedata.sarc', gamedata_bytes, '.sarc'), ('GameData/savedataformat.sarc', savedata_bytes, '.sarc'),
                     ('Message/Msg_EUen.product.sarc', msg_EUen, '.sarc'), ('Message/Msg_USen.product.sarc', msg_USen, '.sarc')]:
    v = calc.calculate_file_size_with_ext(data, False, ext)
    print('RSTB', k, tbl.get_size(k) if tbl.is_in_table(k) else None, '->', v)
    tbl.set_size(k, max(v, vtbl.get_size(k) if vtbl.is_in_table(k) else 0))
for k, data in map_sizes.items():
    v = calc.calculate_file_size_with_ext(data, False, '.mubin')
    setmax(k, v)
# every file the installed mods serve (inside packs too): an entry smaller than the file it sizes makes the game's load
# fail (Relics' Event/EventInfo, null resource at the title screen). Each gets at least what the file needs.
def walk_sizes(rel, data, pre, depth=0):
    if data[:4] == b'Yaz0': data = bytes(oead.yaz0.decompress(data))
    if data[:4] == b'SARC' and depth < 3:
        try: s = oead.Sarc(data)
        except Exception: s = None
        if s:
            for f in s.get_files(): walk_sizes(f.name, bytes(f.data), pre, depth + 1)
    if rel.startswith('System/Resource/'): return
    key = pre + rkey(rel)
    if not tbl.is_in_table(key): return
    ext = '.' + key.rsplit('.', 1)[-1]
    old = tbl.get_size(key)
    # the game needs the file's size (32-aligned) plus its parser's room (~0x1c0 for a BYML: the game's own entries
    # are that much over); the size calculator where it knows the type, and a margin either way
    need = max(calc.calculate_file_size_with_ext(data, False, ext) or 0, ((len(data) + 31) & ~31) + 0x600)
    if old >= need - 0x400: return  # (the game's own entries already have room enough)
    tbl.set_size(key, need); fixed.append((key, old, need))
fixed = []
for d in sorted(OUT.iterdir()):
    for sub, pre in (('romfs', ''), ('aoc', 'Aoc/0010/')):
        root = d / sub
        if not root.exists(): continue
        for p in root.rglob('*'):
            if p.is_file() and p.suffix not in ('.json', '.txt'):
                walk_sizes(p.relative_to(root).as_posix(), p.read_bytes(), pre)
print('RSTB: %d entries too small for their files fixed' % len(fixed))
for x in fixed[:60]: print('  ', *x)
b = io.BytesIO(); tbl.write(b, be=False)
(M / 'System/Resource').mkdir(parents=True, exist_ok=True)
(M / 'System/Resource/ResourceSizeTable.product.srsizetable').write_bytes(oead.yaz0.compress(b.getvalue()))
json.dump(new_actors, open('out/new_actors.json', 'w'), indent=0)
print('done')
