"""Static contract check: every key the client builds must resolve to a real frame."""
import json, os, sys

ROOT = os.getcwd()
PD = os.path.join(ROOT, 'public/assets/sprites/paperdoll')
ICONS = os.path.join(ROOT, 'public/assets/sprites/icons')
man = json.load(open(os.path.join(PD, 'manifest.json')))
items_json = json.load(open(os.path.join(ROOT, 'shared/data/items.json')))['items']

fail = []
def check(cond, msg):
    if not cond: fail.append(msg)

DIRS = man['directions']
ANIMS = man['animations']
layers = {e['id']: e for e in man['bodies'] + man['items']}

# 1. every sheet + atlas on disk, and the atlas contains every frame the client asks for
for lid, e in layers.items():
    sheet = os.path.join(PD, e['sheet']); atlas = os.path.join(PD, e['atlas'])
    check(os.path.exists(sheet), f'missing sheet {e["sheet"]}')
    check(os.path.exists(atlas), f'missing atlas {e["atlas"]}')
    if not os.path.exists(atlas): continue
    frames = json.load(open(atlas))['frames']
    for a in ANIMS:
        for d in DIRS:
            for i in range(a['frames']):
                name = f"{a['name']}_{d}_{i:02d}"          # paperdollFrame()
                check(name in frames, f'{lid}: atlas has no frame {name}')

# 2. every item's spriteId resolves, slot matches, icon exists
SLOT_TO_PD = {'weapon':'mainhand','offhand':'offhand','helm':'helm','chest':'chest',
              'legs':'legs','boots':'boots','gloves':'gloves','back':'back'}
STYLES = {'sword','greatsword','mace','bow','staff'}
CATEGORIES = {'equipment','consumable','quest','misc'}
mapped = 0
for iid, it in items_json.items():
    check(it.get('category') in CATEGORIES, f'{iid}: category {it.get("category")!r} not in ItemCategory')
    sid = it.get('spriteId')
    if not sid: continue
    mapped += 1
    check(sid in layers, f'{iid}: spriteId {sid!r} not in manifest')
    if sid in layers:
        want = SLOT_TO_PD.get(it.get('equipSlot') or '')
        got = layers[sid].get('slot')
        check(want == got, f'{iid}: equipSlot {it.get("equipSlot")!r} -> {want!r} but layer is {got!r}')
    ws = it.get('weaponStyle')
    if ws: check(ws in STYLES, f'{iid}: weaponStyle {ws!r} unknown')
    if it.get('equipSlot') == 'weapon':
        check(bool(ws), f'{iid}: weapon has no weaponStyle, attack cycle will default to swing')
    icon = it.get('inventoryIcon')
    if icon: check(os.path.exists(os.path.join(ICONS, icon)), f'{iid}: icon {icon} missing on disk')

# 3. layerOrder covers every slot plus body, for every direction
for d in DIRS:
    order = man['layerOrder'][d]
    check(set(order) == set(man['slots']) | {'body'}, f'layerOrder[{d}] does not cover all slots')
    check(len(order) < 16, f'layerOrder[{d}] longer than the depth budget (16)')

print(f'layers={len(layers)}  items mapped={mapped}/{len(items_json)}  '
      f'frames checked={len(layers)*len(DIRS)*sum(a["frames"] for a in ANIMS)}')
if fail:
    print(f'\nFAILURES ({len(fail)}):')
    for f in fail[:25]: print('  -', f)
    sys.exit(1)
print('\nOK — every animation key the client builds resolves to a real frame.')
