"""B-07: the game HUD's Widget Blueprints (step 2 groundwork, step 3 uses it).

Three Widget Blueprints in ``/Game/Valhalla/UI/HUD``, parented to the C++
classes in ``ValhallaGameHUDWidget.h``:

* ``WBP_HUDSlot``  -> ``UValhallaHUDSlotWidget`` (one action / inventory cell)
* ``WBP_HUDBar``   -> ``UValhallaHUDBarWidget``  (one resource bar)
* ``WBP_GameHUD``  -> ``UValhallaGameHUDWidget`` (the whole HUD)

An *empty* Blueprint (no designer tree) behaves exactly like its C++ class:
the cell, the bar and the HUD build themselves in code. ``layout_game_hud``
gives ``WBP_GameHUD`` a designer tree built from ``ui-config.json`` whose
widget names are the C++ ``BindWidget`` / ``BindWidgetOptional`` members, so
the Blueprint then owns the layout while C++ still fills and runs it.

How the tree is built (the B-07 step 2 investigation): UE 5.8's Python cannot
reach ``UWidgetBlueprint::WidgetTree`` (not an exposed property), but the
engine's ``UMGToolSet`` (the UMG toolset the MCP server lists) is a reflected
class whose functions -- ``CreateWidgetBlueprint``, ``AddWidget``,
``RenameWidget``, ``ToggleWidgetAsVariable``, ``RemoveWidget``, ``GetWidgets``,
``CompileWidgetBlueprint`` -- can be called from Python on its class default
object with ``call_method``. The widgets and slots they return are ordinary
UObjects, so their properties are set with the usual Blueprint-callable
setters. No C++ editor module is needed.

Project rule: never create an asset that may already exist without checking
first (an "Overwrite?" modal freezes the editor). ``create_blueprints``
checks, and deletes first only when asked to overwrite.
"""

import json
import os

import unreal

HUD_FOLDER = "/Game/Valhalla/UI/HUD"
SLOT_BP = HUD_FOLDER + "/WBP_HUDSlot"
BAR_BP = HUD_FOLDER + "/WBP_HUDBar"
GAME_HUD_BP = HUD_FOLDER + "/WBP_GameHUD"

_PARENTS = (
    ("WBP_HUDSlot", "/Script/ValhallaGame.ValhallaHUDSlotWidget"),
    ("WBP_HUDBar", "/Script/ValhallaGame.ValhallaHUDBarWidget"),
    ("WBP_GameHUD", "/Script/ValhallaGame.ValhallaGameHUDWidget"),
)

#: UValhallaGameHUDWidget::GetRequiredPanelNames (meta = (BindWidget)).
REQUIRED_PANELS = (
    "VitalsPanel", "HpBar", "ManaBar", "ActionBarRow",
    "ChatPanel", "ChatScroll", "ChatInput",
)

#: UValhallaGameHUDWidget::GetOptionalPanelNames (meta = (BindWidgetOptional)).
OPTIONAL_PANELS = (
    "ClassText", "CastBar",
    "TargetFramePanel", "TargetName", "TargetHpBar", "TargetBuffs",
    "PartyPanel", "PartyTitle", "PartyList", "InvitePanel", "InviteText",
    "CombatLogPanel", "CombatLogTitle", "CombatLogScroll",
    "ChatChannelText",
    "LootPanel", "LootTitle", "LootGrid",
    "SkillsPanel", "SkillsList", "SkillsHint",
    "CharacterPanel", "EquipmentPanel", "CharacterLevel", "XpBar", "CharacterStats",
    "InventoryPanel", "InventoryTitle", "InventoryGrid",
    "TooltipPanel", "TooltipName", "TooltipBody",
    "DropConfirmPanel", "DropText",
    "DeathOverlay", "DeathText",
)


# ── The engine's UMG toolset ──────────────────────────────────────────────

def _umg():
    return unreal.get_default_object(unreal.UMGToolSet)


def _umg_call(function_name, *args):
    """Call one UMGToolSet function (they are not exposed as Python methods)."""
    return _umg().call_method(function_name, tuple(args))


def _field(struct, name, default=None):
    try:
        return getattr(struct, name)
    except Exception:  # noqa: BLE001 - struct field names differ between versions
        try:
            return struct.get_editor_property(name)
        except Exception:  # noqa: BLE001
            return default


def _load_class(path):
    cls = unreal.load_class(None, path)
    if cls is None:
        raise RuntimeError("class not found: {} (is ValhallaGame built with B-07 step 2?)".format(path))
    return cls


def _blueprint_class(asset_path):
    """The generated class of a Blueprint asset, or None."""
    if not unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        return None
    return unreal.EditorAssetLibrary.load_blueprint_class(asset_path)


# ── create / describe ─────────────────────────────────────────────────────

def create_blueprints(overwrite=False):
    """Create WBP_HUDSlot, WBP_HUDBar and WBP_GameHUD (empty designer trees).

    Existing assets are left alone and reported as ``existed`` unless
    ``overwrite``, which deletes them first. WBP_GameHUD's SlotWidgetClass and
    BarWidgetClass are then pointed at the other two.
    """
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    report = []
    for name, parent_path in _PARENTS:
        asset_path = "{}/{}".format(HUD_FOLDER, name)
        entry = {"asset": asset_path, "parent": parent_path}
        if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
            if not overwrite:
                entry["existed"] = True
                report.append(entry)
                continue
            if not unreal.EditorAssetLibrary.delete_asset(asset_path):
                raise RuntimeError("could not delete {} (referenced or checked out?)".format(asset_path))
            entry["deleted"] = True
        factory = unreal.WidgetBlueprintFactory()
        factory.set_editor_property("parent_class", _load_class(parent_path))
        blueprint = tools.create_asset(name, HUD_FOLDER, unreal.WidgetBlueprint, factory)
        if blueprint is None:
            raise RuntimeError("create_asset returned None for {}".format(asset_path))
        unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False)
        entry["created"] = True
        entry["widgets"] = len(_widget_infos(blueprint))
        report.append(entry)

    wired = _wire_cell_classes()
    return {"assets": report, "wired": wired}


def _wire_cell_classes():
    """WBP_GameHUD's SlotWidgetClass / BarWidgetClass -> WBP_HUDSlot / WBP_HUDBar."""
    hud_class = _blueprint_class(GAME_HUD_BP)
    if hud_class is None:
        return {}
    cdo = unreal.get_default_object(hud_class)
    wired = {}
    for prop, asset in (("slot_widget_class", SLOT_BP), ("bar_widget_class", BAR_BP)):
        cls = _blueprint_class(asset)
        if cls is not None:
            cdo.set_editor_property(prop, cls)
            wired[prop] = cls.get_path_name()
    if wired:
        blueprint = unreal.load_asset(GAME_HUD_BP)
        _umg_call("CompileWidgetBlueprint", blueprint)
        unreal.EditorAssetLibrary.save_asset(GAME_HUD_BP, only_if_is_dirty=False)
    return wired


def _widget_infos(blueprint):
    tree = _umg_call("GetWidgets", blueprint)
    return list(_field(tree, "widgets", []) or [])


def describe(asset_path):
    """The designer tree of a Widget Blueprint, and (for a HUD) the panel names it binds."""
    blueprint = unreal.load_asset(asset_path)
    if blueprint is None:
        raise RuntimeError("no asset at {}".format(asset_path))
    tree = _umg_call("GetWidgets", blueprint)
    info = _field(tree, "info")
    parent = _field(info, "parent_class") if info is not None else None
    widgets = []
    names = set()
    for entry in _field(tree, "widgets", []) or []:
        widget = _field(entry, "widget")
        parent_widget = _field(entry, "parent")
        name = str(_field(entry, "widget_name", "") or (widget.get_name() if widget else ""))
        names.add(name)
        widgets.append({
            "name": name,
            "class": widget.get_class().get_name() if widget else "",
            "parent": parent_widget.get_name() if parent_widget else "",
            "isVariable": bool(_field(entry, "is_variable", False)),
        })
    result = {
        "asset": asset_path,
        "parentClass": parent.get_path_name() if parent else "",
        "widgetCount": len(widgets),
        "widgets": widgets,
    }
    hud_class = unreal.load_class(None, "/Script/ValhallaGame.ValhallaGameHUDWidget")
    generated = _blueprint_class(asset_path)
    if hud_class is not None and generated is not None and unreal.MathLibrary.class_is_child_of(generated, hud_class):
        result["requiredMissing"] = [n for n in REQUIRED_PANELS if n not in names]
        result["optionalPresent"] = [n for n in OPTIONAL_PANELS if n in names]
        result["optionalMissing"] = [n for n in OPTIONAL_PANELS if n not in names]
        result["laysOutFromBlueprint"] = bool(widgets) and not result["requiredMissing"]
    return result


# ── ui-config.json ────────────────────────────────────────────────────────

def _data_root():
    settings = unreal.get_default_object(unreal.load_class(None, "/Script/ValhallaCore.ValhallaDataSettings"))
    root = str(settings.get_editor_property("data_root") or "../shared/data")
    if not os.path.isabs(root):
        root = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), root)
    return os.path.normpath(root)


def _load_ui_config():
    path = os.path.join(_data_root(), "ui-config.json")
    with open(path, "r", encoding="utf-8") as handle:
        return path, json.load(handle)


def _get(config, dotted, default):
    node = config
    for key in dotted.split("."):
        if not isinstance(node, dict) or key not in node:
            return default
        node = node[key]
    return node


def _px(value, default):
    """``"12px"`` or 12 -> 12, then CSS px to Slate points (PxToSlate)."""
    try:
        number = float(str(value).strip().rstrip("px").strip())
    except ValueError:
        number = float(default)
    return max(6, int(round(number * 0.75)))


def _colour(hex_text, alpha=1.0, default="#ffffff"):
    """``#rrggbb`` / ``#rgb`` (sRGB) -> unreal.LinearColor, as FValhallaUIConfig::ParseHexColor."""
    text = str(hex_text or default).lstrip("#")
    if len(text) == 3:
        text = "".join(c * 2 for c in text)
    try:
        rgb = [int(text[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]
    except ValueError:
        return _colour(default, alpha)

    def linear(c):
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    return unreal.LinearColor(linear(rgb[0]), linear(rgb[1]), linear(rgb[2]), float(alpha))


# ── Tree building ─────────────────────────────────────────────────────────

class _TreeBuilder:
    """AddWidget with the name checked (BindWidget matches by object name)."""

    def __init__(self, blueprint):
        self.blueprint = blueprint
        self.added = []
        self.renamed = []

    def add(self, widget_class, name, parent=None, variable=False):
        info = _umg_call("AddWidget", self.blueprint, widget_class, name, parent, -1)
        widget = _field(info, "widget")
        if widget is None:
            raise RuntimeError("AddWidget returned no widget for {}".format(name))
        if widget.get_name() != name:
            renamed = _umg_call("RenameWidget", self.blueprint, widget, name)
            widget = _field(renamed, "widget") or widget
            self.renamed.append(name)
            if widget.get_name() != name:
                raise RuntimeError("widget {} came out named {}".format(name, widget.get_name()))
        if variable:
            _umg_call("ToggleWidgetAsVariable", self.blueprint, widget, True)
        self.added.append(name)
        return widget, _field(info, "slot")


def _canvas(slot, anchor, alignment, position, z_order, stretch=False):
    anchors = unreal.Anchors()
    anchors.set_editor_property("minimum", unreal.Vector2D(anchor[0], anchor[1]))
    anchors.set_editor_property("maximum", unreal.Vector2D(anchor[2], anchor[3]) if stretch else unreal.Vector2D(anchor[0], anchor[1]))
    slot.set_anchors(anchors)
    if stretch:
        slot.set_offsets(unreal.Margin(0.0, 0.0, 0.0, 0.0))
    else:
        slot.set_alignment(unreal.Vector2D(alignment[0], alignment[1]))
        slot.set_position(unreal.Vector2D(position[0], position[1]))
        slot.set_auto_size(True)
    slot.set_z_order(int(z_order))


def _pad(slot, left, top=None, right=None, bottom=None):
    top = left if top is None else top
    right = left if right is None else right
    bottom = top if bottom is None else bottom
    slot.set_padding(unreal.Margin(float(left), float(top), float(right), float(bottom)))


def _fill(slot):
    size = unreal.SlateChildSize()
    size.set_editor_property("value", 1.0)
    size.set_editor_property("size_rule", unreal.SlateSizeRule.FILL)
    slot.set_size(size)


def _text(widget, content, size, colour, bold=False, wrap=False):
    widget.set_text(unreal.Text(content))
    font = widget.get_editor_property("font")
    font.set_editor_property("size", int(size))
    if bold:
        font.set_editor_property("typeface_font_name", "Bold")
    widget.set_editor_property("font", font)
    widget.set_color_and_opacity(unreal.SlateColor(colour))
    if wrap:
        widget.set_auto_wrap_text(True)


def _panel(border, colour, padding):
    border.set_brush_color(colour)
    border.set_padding(unreal.Margin(float(padding), float(padding), float(padding), float(padding)))


def _set_button_action(button, action_name):
    """UValhallaHUDButton::Action by enumerator name. True when it took.

    The enum's Python type is read off the property rather than looked up on
    ``unreal``: EValhallaHUDButton and UValhallaHUDButton both strip to
    "ValhallaHUDButton", so the module attribute is the class.
    """
    try:
        current = button.get_editor_property("action")
        button.set_editor_property("action", getattr(type(current), action_name))
        button.set_editor_property("index", 0)
        return True
    except Exception:  # noqa: BLE001 - reported by the caller
        return False


def layout_game_hud(asset_path=GAME_HUD_BP, replace=False):
    """Give WBP_GameHUD a designer tree from ui-config.json, named for the C++ bindings.

    Refuses a Blueprint that already has widgets unless ``replace`` (which
    removes the old root and everything under it). Compiles and saves.
    """
    blueprint = unreal.load_asset(asset_path)
    if blueprint is None:
        raise RuntimeError("no asset at {} (run create_hud_blueprints first)".format(asset_path))
    existing = _widget_infos(blueprint)
    if existing:
        if not replace:
            return {"refused": "{} already has {} widget(s); pass replace=True to rebuild it".format(asset_path, len(existing))}
        for entry in existing:
            if _field(entry, "parent") is None and not _field(entry, "inherited", False):
                _umg_call("RemoveWidget", blueprint, _field(entry, "widget"))

    config_path, cfg = _load_ui_config()
    W = unreal
    bar_class = _blueprint_class(BAR_BP) or _load_class("/Script/ValhallaGame.ValhallaHUDBarWidget")
    button_class = _load_class("/Script/ValhallaGame.ValhallaHUDButton")
    b = _TreeBuilder(blueprint)

    # Numbers the C++ code layout uses (FValhallaUIConfig defaults when missing).
    hp_x = float(_get(cfg, "hud.hpBar.x", 16))
    hp_w = float(_get(cfg, "hud.hpBar.width", 200))
    hp_h = float(_get(cfg, "hud.hpBar.height", 12))
    hp_off = float(_get(cfg, "hud.hpBar.yOffsetFromBottom", 36))
    mana_gap = float(_get(cfg, "hud.manaBar.gapAboveHp", 6))
    ab_slot = float(_get(cfg, "actionBar.slotSize", 44))
    ab_pad = float(_get(cfg, "actionBar.padding", 6))
    ab_bottom = float(_get(cfg, "actionBar.bottomMargin", 8))
    cast_above = float(_get(cfg, "castBar.yAboveActionBar", 12))
    chat_w = float(_get(cfg, "chat.maxWidth", 360))
    chat_bottom = float(_get(cfg, "chat.bottomMargin", 8))
    chat_pad = float(_get(cfg, "chat.padding", 6))
    chat_font = _px(_get(cfg, "chat.fontSize", "11px"), 11)
    inv_bg = _colour(_get(cfg, "inventory.colors.bg", "#1a1a2e"), _get(cfg, "inventory.colors.bgAlpha", 0.95))
    title_c = _colour(_get(cfg, "inventory.colors.titleColor", "#ffcc00"))
    label_c = _colour(_get(cfg, "inventory.colors.labelColor", "#aaaacc"))
    value_c = _colour(_get(cfg, "inventory.colors.valueColor", "#ffffff"))
    char_w = float(_get(cfg, "inventory.charPanelWidth", 220))
    panel_gap = float(_get(cfg, "inventory.panelGap", 8))
    panel_h = float(_get(cfg, "inventory.panelHeight", 340))
    slot_gap = float(_get(cfg, "inventory.slotGap", 4))

    unset_actions = []

    def button(parent, caption, action, tint):
        widget, slot = b.add(button_class, "Btn" + action, parent)
        widget.set_background_color(tint)
        if not _set_button_action(widget, _enum_name(action)):
            unset_actions.append("Btn" + action)
        label, _ = b.add(W.TextBlock.static_class(), "Btn" + action + "Label", widget)
        _text(label, caption, 9, W.LinearColor(0, 0, 0, 1), bold=True)
        return widget, slot

    root, _ = b.add(W.CanvasPanel.static_class(), "HUDRoot")

    # vitals: class line, mana/energy, HP, bottom left
    vitals, s = b.add(W.VerticalBox.static_class(), "VitalsPanel", root, True)
    _canvas(s, (0, 1), (0, 1), (hp_x - 2, -(hp_off - hp_h - 2)), 0)
    w, s = b.add(W.TextBlock.static_class(), "ClassText", vitals, True)
    _text(w, "", _px(_get(cfg, "hud.classText.fontSize", "12px"), 12), _colour(_get(cfg, "hud.classText.color", "#ffffff")))
    _pad(s, 2, 0, 0, 4)
    _, s = b.add(bar_class, "ManaBar", vitals, True)
    _pad(s, 0, 0, 0, mana_gap)
    b.add(bar_class, "HpBar", vitals, True)

    # action bar, bottom centre
    ab_panel, s = b.add(W.Border.static_class(), "ActionBarFrame", root)
    _canvas(s, (0.5, 1), (0.5, 1), (0, -ab_bottom), 5)
    _panel(ab_panel, _colour(_get(cfg, "actionBar.colors.bg", "#1a1a1a"), _get(cfg, "actionBar.colors.bgAlpha", 0.95)), ab_pad)
    b.add(W.HorizontalBox.static_class(), "ActionBarRow", ab_panel, True)

    # cast bar, above the action bar
    _, s = b.add(bar_class, "CastBar", root, True)
    _canvas(s, (0.5, 1), (0.5, 1), (0, -(ab_bottom + ab_slot + ab_pad * 2 + cast_above)), 6)

    # target frame, top centre
    target, s = b.add(W.Border.static_class(), "TargetFramePanel", root, True)
    _canvas(s, (0.5, 0), (0.5, 0), (0, 12), 5)
    _panel(target, _colour(_get(cfg, "inventory.colors.bg", "#1a1a2e"), 0.85), 6)
    column, _ = b.add(W.VerticalBox.static_class(), "TargetColumn", target)
    w, _ = b.add(W.TextBlock.static_class(), "TargetName", column, True)
    _text(w, "Target", _px(_get(cfg, "nameplates.fontSize", "12px"), 12) + 2, _colour(_get(cfg, "nameplates.color", "#ffffff")), bold=True)
    _, s = b.add(bar_class, "TargetHpBar", column, True)
    _pad(s, 0, 3, 0, 0)
    _, s = b.add(W.HorizontalBox.static_class(), "TargetBuffs", column, True)
    _pad(s, 0, 4, 0, 0)

    # party, top left; invite prompt, top centre
    party, s = b.add(W.Border.static_class(), "PartyPanel", root, True)
    _canvas(s, (0, 0), (0, 0), (12, 12), 5)
    _panel(party, _colour("#12122a", 0.92), 6)
    column, _ = b.add(W.VerticalBox.static_class(), "PartyColumn", party)
    header, _ = b.add(W.HorizontalBox.static_class(), "PartyHeader", column)
    w, s = b.add(W.TextBlock.static_class(), "PartyTitle", header, True)
    _text(w, "Party", 9, title_c, bold=True)
    _fill(s)
    button(header, "Leave", "PartyLeave", _colour("#8888aa"))
    b.add(W.VerticalBox.static_class(), "PartyList", column, True)

    invite, s = b.add(W.Border.static_class(), "InvitePanel", root, True)
    _canvas(s, (0.5, 0), (0.5, 0), (0, 110), 20)
    _panel(invite, inv_bg, 10)
    column, _ = b.add(W.VerticalBox.static_class(), "InviteColumn", invite)
    w, _ = b.add(W.TextBlock.static_class(), "InviteText", column, True)
    _text(w, "", 10, title_c, bold=True)
    row, s = b.add(W.HorizontalBox.static_class(), "InviteButtons", column)
    _pad(s, 0, 8, 0, 0)
    button(row, "Accept", "PartyAccept", _colour("#44cc66"))
    button(row, "Decline", "PartyDecline", _colour("#cc5555"))

    # combat log, top right
    log, s = b.add(W.Border.static_class(), "CombatLogPanel", root, True)
    _canvas(s, (1, 0), (1, 0), (-12, 12), 5)
    _panel(log, _colour(_get(cfg, "chat.bgColor", "#0a0a1a"), _get(cfg, "chat.bgAlpha", 0.72)), 2)
    box, _ = b.add(W.SizeBox.static_class(), "CombatLogSize", log)
    box.set_width_override(320.0)
    box.set_height_override(200.0)
    column, _ = b.add(W.VerticalBox.static_class(), "CombatLogColumn", box)
    w, s = b.add(W.TextBlock.static_class(), "CombatLogTitle", column, True)
    _text(w, "Combat Log", 8, _colour("#ccaa66"), bold=True)
    _pad(s, 6, 3)
    _, s = b.add(W.ScrollBox.static_class(), "CombatLogScroll", column, True)
    _fill(s)
    _pad(s, 6, 0, 4, 4)

    # chat, bottom left beside the vitals
    chat, s = b.add(W.Border.static_class(), "ChatPanel", root, True)
    _canvas(s, (0, 1), (0, 1), (hp_x + hp_w + 16, -chat_bottom), 8)
    _panel(chat, _colour(_get(cfg, "chat.bgColor", "#0a0a1a"), 0.0), chat_pad)
    box, _ = b.add(W.SizeBox.static_class(), "ChatSize", chat)
    box.set_width_override(chat_w)
    column, _ = b.add(W.VerticalBox.static_class(), "ChatColumn", box)
    w, _ = b.add(W.TextBlock.static_class(), "ChatChannelText", column, True)
    _text(w, "[General]", chat_font, _colour(_get(cfg, "chat.colors.system", "#ffaa44")), bold=True)
    _, s = b.add(W.ScrollBox.static_class(), "ChatScroll", column, True)
    _fill(s)
    w, s = b.add(W.EditableTextBox.static_class(), "ChatInput", column, True)
    w.set_hint_text(unreal.Text("Say something — /g /world /p /w <name> /invite /accept /decline /leave"))
    _pad(s, 0, 3, 0, 0)

    # loot, right of centre
    loot, s = b.add(W.Border.static_class(), "LootPanel", root, True)
    _canvas(s, (0.5, 0.5), (0.5, 1), (220, -40), 15)
    _panel(loot, inv_bg, 8)
    column, _ = b.add(W.VerticalBox.static_class(), "LootColumn", loot)
    w, _ = b.add(W.TextBlock.static_class(), "LootTitle", column, True)
    _text(w, "Loot", 10, title_c, bold=True)
    w, s = b.add(W.TextBlock.static_class(), "LootHint", column)
    _text(w, "click an item to take it", 7, label_c)
    _pad(s, 0, 0, 0, 4)
    grid, _ = b.add(W.UniformGridPanel.static_class(), "LootGrid", column, True)
    grid.set_slot_padding(unreal.Margin(slot_gap * 0.5, slot_gap * 0.5, slot_gap * 0.5, slot_gap * 0.5))
    row, s = b.add(W.HorizontalBox.static_class(), "LootButtons", column)
    _pad(s, 0, 6, 0, 0)
    button(row, "Loot All", "LootAll", _colour(_get(cfg, "inventory.colors.highlight", "#ffaa00")))
    button(row, "Close", "LootClose", _colour("#8888aa"))

    # skills, left middle
    skills, s = b.add(W.Border.static_class(), "SkillsPanel", root, True)
    _canvas(s, (0, 0.5), (0, 0.5), (16, -40), 12)
    _panel(skills, inv_bg, 8)
    column, _ = b.add(W.VerticalBox.static_class(), "SkillsColumn", skills)
    header, _ = b.add(W.HorizontalBox.static_class(), "SkillsHeader", column)
    w, s = b.add(W.TextBlock.static_class(), "SkillsTitle", header)
    _text(w, "Skills  (K)", 10, title_c, bold=True)
    _fill(s)
    button(header, "X", "SkillsClose", _colour("#8888aa"))
    w, s = b.add(W.TextBlock.static_class(), "SkillsHint", column, True)
    _text(w, "Drag a skill to the action bar, or click it and then click a slot.", 7, label_c, wrap=True)
    _pad(s, 0, 2, 0, 6)
    box, _ = b.add(W.SizeBox.static_class(), "SkillsSize", column)
    box.set_width_override(300.0)
    box.set_max_desired_height(panel_h + 40.0)
    scroll, _ = b.add(W.ScrollBox.static_class(), "SkillsScroll", box)
    b.add(W.VerticalBox.static_class(), "SkillsList", scroll, True)

    # character + inventory (I), centre
    pair, s = b.add(W.HorizontalBox.static_class(), "InventoryPair", root)
    _canvas(s, (0.5, 0.5), (0.5, 0.5), (0, -30), 10)
    char_panel, s = b.add(W.Border.static_class(), "CharacterPanel", pair, True)
    _pad(s, 0, 0, panel_gap, 0)
    _panel(char_panel, inv_bg, 8)
    box, _ = b.add(W.SizeBox.static_class(), "CharacterSize", char_panel)
    box.set_width_override(char_w)
    box.set_min_desired_height(panel_h)
    column, _ = b.add(W.VerticalBox.static_class(), "CharacterColumn", box)
    w, s = b.add(W.TextBlock.static_class(), "CharacterTitle", column)
    _text(w, "Character  (I)", 10, title_c, bold=True)
    _pad(s, 0, 0, 0, 4)
    b.add(W.VerticalBox.static_class(), "EquipmentPanel", column, True)
    w, s = b.add(W.TextBlock.static_class(), "CharacterLevel", column, True)
    _text(w, "", 7, value_c)
    _pad(s, 0, 6, 0, 2)
    _, s = b.add(bar_class, "XpBar", column, True)
    _pad(s, 0, 0, 0, 2)
    w, s = b.add(W.TextBlock.static_class(), "CharacterStats", column, True)
    _text(w, "", 7, value_c, wrap=True)
    _pad(s, 0, 2, 0, 0)

    inv_panel, _ = b.add(W.Border.static_class(), "InventoryPanel", pair, True)
    _panel(inv_panel, inv_bg, 8)
    box, _ = b.add(W.SizeBox.static_class(), "InventorySize", inv_panel)
    box.set_min_desired_height(panel_h)
    column, _ = b.add(W.VerticalBox.static_class(), "InventoryColumn", box)
    header, s = b.add(W.HorizontalBox.static_class(), "InventoryHeader", column)
    _pad(s, 0, 0, 0, 4)
    w, s = b.add(W.TextBlock.static_class(), "InventoryTitle", header, True)
    _text(w, "Inventory  (I)", 10, title_c, bold=True)
    _fill(s)
    button(header, "X", "InventoryClose", _colour("#8888aa"))
    grid, _ = b.add(W.UniformGridPanel.static_class(), "InventoryGrid", column, True)
    grid.set_slot_padding(unreal.Margin(slot_gap * 0.5, slot_gap * 0.5, slot_gap * 0.5, slot_gap * 0.5))
    w, s = b.add(W.TextBlock.static_class(), "InventoryHint", column)
    _text(w, "click: equip / unequip    right-click: drop    drag: move or swap", 7, label_c)
    _pad(s, 0, 6, 0, 0)

    # tooltip (on the root canvas: C++ moves it), drop confirm, death
    tip, s = b.add(W.Border.static_class(), "TooltipPanel", root, True)
    _canvas(s, (0, 0), (0, 0), (0, 0), 50)
    _panel(tip, _colour("#0c0c18", 0.97), 8)
    box, _ = b.add(W.SizeBox.static_class(), "TooltipSize", tip)
    box.set_max_desired_width(240.0)
    column, _ = b.add(W.VerticalBox.static_class(), "TooltipColumn", box)
    w, _ = b.add(W.TextBlock.static_class(), "TooltipName", column, True)
    _text(w, "", 10, W.LinearColor(1, 1, 1, 1), bold=True)
    w, s = b.add(W.TextBlock.static_class(), "TooltipBody", column, True)
    _text(w, "", 8, value_c, wrap=True)
    _pad(s, 0, 4, 0, 0)

    drop, s = b.add(W.Border.static_class(), "DropConfirmPanel", root, True)
    _canvas(s, (0.5, 0.5), (0.5, 0.5), (0, 0), 40)
    _panel(drop, _colour(_get(cfg, "inventory.colors.bg", "#1a1a2e"), 0.98), 12)
    column, _ = b.add(W.VerticalBox.static_class(), "DropColumn", drop)
    w, _ = b.add(W.TextBlock.static_class(), "DropText", column, True)
    _text(w, "", 10, title_c, bold=True)
    row, s = b.add(W.HorizontalBox.static_class(), "DropButtons", column)
    _pad(s, 0, 8, 0, 0)
    button(row, "Drop", "DropConfirm", _colour("#cc5555"))
    button(row, "Cancel", "DropCancel", _colour("#8888aa"))

    death, s = b.add(W.Border.static_class(), "DeathOverlay", root, True)
    _canvas(s, (0, 0, 1, 1), None, None, 100, stretch=True)
    death.set_brush_color(W.LinearColor(0, 0, 0, float(_get(cfg, "deathOverlay.bgAlpha", 0.7))))
    death.set_editor_property("horizontal_alignment", W.HorizontalAlignment.H_ALIGN_CENTER)
    death.set_editor_property("vertical_alignment", W.VerticalAlignment.V_ALIGN_CENTER)
    w, _ = b.add(W.TextBlock.static_class(), "DeathText", death, True)
    _text(w, "YOU DIED", _px(_get(cfg, "deathOverlay.fontSize", "32px"), 32), _colour(_get(cfg, "deathOverlay.textColor", "#ff4444")), bold=True)

    compiled = bool(_umg_call("CompileWidgetBlueprint", blueprint))
    unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False)
    missing = [n for n in REQUIRED_PANELS + OPTIONAL_PANELS if n not in b.added]
    return {
        "asset": asset_path,
        "config": config_path,
        "widgets": len(b.added),
        "renamed": b.renamed,
        "panelsMissing": missing,
        "buttonsWithoutAction": unset_actions,
        "compiled": compiled,
    }


def _enum_name(action):
    """``PartyLeave`` -> ``PARTY_LEAVE`` (how UE Python names enumerators)."""
    out = []
    for index, char in enumerate(action):
        if char.isupper() and index > 0:
            out.append("_")
        out.append(char.upper())
    return "".join(out)
