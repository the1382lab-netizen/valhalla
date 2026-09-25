"""B-07: the game HUD's Widget Blueprints (step 2 groundwork, step 3 layouts).

Three Widget Blueprints in ``/Game/Valhalla/UI/HUD``, parented to the C++
classes in ``ValhallaGameHUDWidget.h``:

* ``WBP_HUDSlot``  -> ``UValhallaHUDSlotWidget`` (one action / inventory cell)
* ``WBP_HUDBar``   -> ``UValhallaHUDBarWidget``  (one resource bar)
* ``WBP_GameHUD``  -> ``UValhallaGameHUDWidget`` (the whole HUD)

An *empty* Blueprint (no designer tree) behaves exactly like its C++ class:
the cell, the bar and the HUD build themselves in code. ``layout_blueprint``
gives each one a designer tree built from ``ui-config.json`` whose widget
names are the C++ ``BindWidget`` / ``BindWidgetOptional`` members, so the
Blueprint then owns the layout while C++ still fills and runs it:

* ``layout_game_hud``: WBP_GameHUD, every panel where the code-built HUD puts
  it (same anchors, so it adapts to the viewport the same way). Its bars are
  WBP_HUDBar instances, each in a Size Box that gives it the code bar's size
  (a designer bar is not sized by C++; see below), with ``BarWidth`` set to
  the fill's full width.
* ``layout_bar``: WBP_HUDBar, the code bar (frame, dark background, fill,
  shield wash, centred label) out of the part names.
* ``layout_slot``: WBP_HUDSlot, the cell (frame, well, icon, skill tile,
  code, cooldown sweep, key label, quantity) out of the part names.

The B-15 Wave 4 frame art the code build draws (``T_UI_Panel``,
``T_UI_Button``, ``T_UI_Slot``, ``T_UI_BarFrame``, ``T_UI_BarFill`` in
``/Game/Valhalla/UI/Frames``, ``ValhallaHudArt::BoxBrush``) is baked into the
designer brushes with the same margins and paddings when those textures are
imported; without them the trees use the flat ui-config colours. C++ applies
no frame art to a designer tree, so the art has to be in the Blueprint -- with
one exception: every Border whose brush is panel frame art (``T_UI_Panel`` or
``T_UI_PanelFrame_NN``) is re-bordered at runtime for the panel border
thickness (``UValhallaGameHUDWidget::PanelBorderThickness``, 4 px, or the
player's Options -> Layout "Border thickness"). Slate draws a box brush's
margin at one texel per Slate unit (margin x the texture's pixel size; the
brush's Image Size does not enter into it), so each thickness N has its own
art, ``T_UI_PanelFrame_NN`` (``Tools/ui/make_panel_frames.py``: T_UI_Panel
scaled so its 24 px trim is N texels), drawn with a margin of N / size.
``PANEL_BORDER_PX`` bakes the default here so the designer preview matches;
``fix_panel_borders`` repairs trees saved with other margins (a box margin
past 0.5 squeezes the trim into thick, doubled sides).

B-07 step 4: WBP_GameHUD's ``SlotWidgetClass`` / ``BarWidgetClass`` are
WBP_HUDSlot / WBP_HUDBar (``wire_cell_classes(True, True)``; ``create_blueprints``
still leaves them alone unless asked). ``UValhallaHUDSlotWidget::Setup`` sizes a
designer cell's ``Sizer`` (action 44, inventory / loot 48, skills 36,
equipment 26: WBP_GameHUD's "Valhalla|HUD Style" Class Defaults) and
``UValhallaHUDBarWidget::SetBarSize`` a designer bar's ``Sizer`` and
``BarWidth`` (party 160x8, nameplates 60x4), so one designer tree serves every
size.

B-21: every panel the player may move (UValhallaGameHUDWidget::GetMovablePanels)
is its own child of the root canvas: the character sheet and the inventory are
two (they used to share an ``InventoryPair`` Horizontal Box), placed so the
default look is unchanged. C++ records each canvas slot as the designer's
layout and lays the player's over it.

B-21 step 4: the options menu. ``WBP_OptionsMenu`` (-> ``UValhallaOptionsMenuWidget``)
is laid out by ``layout_options_menu`` (every control named for the C++
``BindWidgetOptional`` members; C++ adds the colour rows, preset swatches and
log-filter check boxes and does the rest) and assigned as WBP_GameHUD's
``OptionsMenuClass``. WBP_GameHUD's ``OptionsButton`` is the cog bottom right
(``T_UI_Cog`` in ``/Game/Valhalla/UI/Frames``, a UValhallaHUDButton with
Action = Options).

B-07 step 4 also trimmed ``ui-config.json`` to chat / inventory (cols x rows) /
nameplates: the layout now lives in the Blueprints themselves. The layout
functions below still read the file, and every field they read that is gone
falls back to its old value (the ``_get`` defaults), so re-running them gives
the same trees -- but it replaces whatever was edited in the Widget Designer
since (``replace=True`` is required for a Blueprint that has widgets).

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
OPTIONS_MENU_BP = HUD_FOLDER + "/WBP_OptionsMenu"

SLOT_CLASS = "/Script/ValhallaGame.ValhallaHUDSlotWidget"
BAR_CLASS = "/Script/ValhallaGame.ValhallaHUDBarWidget"
GAME_HUD_CLASS = "/Script/ValhallaGame.ValhallaGameHUDWidget"
OPTIONS_MENU_CLASS = "/Script/ValhallaGame.ValhallaOptionsMenuWidget"

_PARENTS = (
    ("WBP_HUDSlot", SLOT_CLASS),
    ("WBP_HUDBar", BAR_CLASS),
    ("WBP_GameHUD", GAME_HUD_CLASS),
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
    "OptionsButton",
)

#: UValhallaOptionsMenuWidget::GetOptionalWidgetNames (all BindWidgetOptional).
OPTIONS_MENU_PARTS = (
    "Tabs", "CloseButton",
    "LayoutTabButton", "ColoursTabButton", "ChatLogTabButton", "NameplatesTabButton", "ControlsTabButton",
    "LockCheck", "UiScaleSlider", "UiScaleText", "OpacitySlider", "OpacityText",
    "BorderSlider", "BorderText",
    "ShowVitalsCheck", "ShowActionBarCheck", "ShowCastBarCheck", "ShowTargetFrameCheck",
    "ShowPartyCheck", "ShowCombatLogCheck", "ShowChatCheck", "ResetLayoutButton",
    "ColourList", "ColourEditor", "EditTitle", "PresetGrid",
    "HueSlider", "SaturationSlider", "ValueSlider", "EditSwatch",
    "ColourOkButton", "ColourCancelButton", "ResetColoursButton",
    "ChatFontSizeSlider", "ChatFontSizeText", "ChatLinesSlider", "ChatLinesText",
    "TimestampsCheck", "LogFilterList", "ResetChatButton",
    "NpcNameplatesCheck", "PlayerNameplatesCheck", "FloatingTextCheck",
    "NameplateFontSlider", "NameplateFontText", "ResetNameplatesButton",
    "ControlsText",
)

#: UValhallaHUDSlotWidget's BindWidgetOptional parts.
SLOT_PARTS = (
    "Sizer", "Frame", "Fill", "Stack", "Icon", "SkillTile", "Abbrev",
    "CooldownSizer", "CooldownFill", "CooldownBar", "CooldownText",
    "KeyLabel", "QuantityText", "SelectionRing",
)

#: UValhallaHUDBarWidget's BindWidgetOptional parts.
BAR_PARTS = (
    "Frame", "Background", "Sizer", "FillSizer", "Fill", "FillBar",
    "OverlaySizer", "OverlayFill", "OverlayBar", "Label",
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


def _is_child_of(asset_path, native_path):
    generated = _blueprint_class(asset_path)
    native = unreal.load_class(None, native_path)
    return bool(generated and native and unreal.MathLibrary.class_is_child_of(generated, native))


# ── create / describe ─────────────────────────────────────────────────────

def create_blueprints(overwrite=False, wire_cells=False):
    """Create WBP_HUDSlot, WBP_HUDBar and WBP_GameHUD (empty designer trees).

    Existing assets are left alone and reported as ``existed`` unless
    ``overwrite``, which deletes them first. ``wire_cells`` also points
    WBP_GameHUD's SlotWidgetClass / BarWidgetClass at the other two (off by
    default: see the module docstring).
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

    # An empty WBP_GameHUD does not compile (its BindWidget members are
    # required), so wiring waits for a layout unless asked for.
    wired = wire_cell_classes(slot=True, bar=True) if wire_cells else {}
    return {"assets": report, "wired": wired}


def wire_cell_classes(slot=False, bar=False):
    """WBP_GameHUD's SlotWidgetClass / BarWidgetClass -> WBP_HUDSlot / WBP_HUDBar, or back to C++.

    ``slot`` / ``bar`` false sets that one to the C++ class (the code-built
    cell / bar at the size C++ asks for). Compiles and saves WBP_GameHUD.
    """
    hud_class = _blueprint_class(GAME_HUD_BP)
    if hud_class is None:
        return {}
    cdo = unreal.get_default_object(hud_class)
    wired = {}
    for prop, want, asset, native in (("slot_widget_class", slot, SLOT_BP, SLOT_CLASS),
                                      ("bar_widget_class", bar, BAR_BP, BAR_CLASS)):
        cls = _blueprint_class(asset) if want else None
        cls = cls or _load_class(native)
        cdo.set_editor_property(prop, cls)
        wired[prop] = cls.get_path_name()
    blueprint = unreal.load_asset(GAME_HUD_BP)
    try:
        wired["compiled"] = bool(_umg_call("CompileWidgetBlueprint", blueprint))
    except Exception as exc:  # noqa: BLE001 - e.g. an empty tree lacks the BindWidget members
        wired["compiled"] = False
        wired["compileError"] = str(exc)[:400]
    unreal.EditorAssetLibrary.save_asset(GAME_HUD_BP, only_if_is_dirty=False)
    return wired


def _widget_infos(blueprint):
    """The designer widgets (GetWidgets also lists unbound optional members, widget-less)."""
    tree = _umg_call("GetWidgets", blueprint)
    return [e for e in (_field(tree, "widgets", []) or []) if _field(e, "widget") is not None]


def describe(asset_path):
    """The designer tree of a Widget Blueprint, and (for a HUD, bar or cell) the parts it binds."""
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
        if widget is None:
            # GetWidgets also lists unbound BindWidgetOptional members, widget-less.
            continue
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
    if _is_child_of(asset_path, GAME_HUD_CLASS):
        result["rootIsCanvas"] = bool(widgets) and widgets[0]["class"] == "CanvasPanel"
        result["requiredMissing"] = [n for n in REQUIRED_PANELS if n not in names]
        result["optionalPresent"] = [n for n in OPTIONAL_PANELS if n in names]
        result["optionalMissing"] = [n for n in OPTIONAL_PANELS if n not in names]
        result["laysOutFromBlueprint"] = result["rootIsCanvas"] and not result["requiredMissing"]
        cdo = unreal.get_default_object(_blueprint_class(asset_path))
        for prop in ("slot_widget_class", "bar_widget_class"):
            try:
                cls = cdo.get_editor_property(prop)
                result[prop] = cls.get_path_name() if cls else ""
            except Exception:  # noqa: BLE001
                pass
        try:
            cls = cdo.get_editor_property("options_menu_class")
            result["options_menu_class"] = cls.get_path_name() if cls else ""
        except Exception:  # noqa: BLE001
            pass
    elif _is_child_of(asset_path, OPTIONS_MENU_CLASS):
        result["partsPresent"] = [n for n in OPTIONS_MENU_PARTS if n in names]
        result["partsMissing"] = [n for n in OPTIONS_MENU_PARTS if n not in names]
    elif _is_child_of(asset_path, SLOT_CLASS):
        result["partsPresent"] = [n for n in SLOT_PARTS if n in names]
        result["partsMissing"] = [n for n in SLOT_PARTS if n not in names]
    elif _is_child_of(asset_path, BAR_CLASS):
        result["partsPresent"] = [n for n in BAR_PARTS if n in names]
        result["partsMissing"] = [n for n in BAR_PARTS if n not in names]
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
        self.warnings = []

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

    def warn(self, text):
        self.warnings.append(text)


def _clear_tree(blueprint, replace, asset_path):
    """Refuse a Blueprint with a tree unless ``replace``; then remove its root (and so everything)."""
    existing = _widget_infos(blueprint)
    if not existing:
        return None
    if not replace:
        return {"refused": "{} already has {} widget(s); pass replace=True to rebuild it".format(asset_path, len(existing))}
    # The root is the entry without a parent. Not filtered on `inherited`:
    # UMGToolSet reports a widget whose name matches a C++ BindWidget member
    # as inherited, which is every part here (the C++ parents have no tree).
    for entry in existing:
        widget = _field(entry, "widget")
        if widget is not None and _field(entry, "parent") is None and _field(entry, "named_slot_host") is None:
            _umg_call("RemoveWidget", blueprint, widget)
    left = _widget_infos(blueprint)
    if left:
        raise RuntimeError("{} still has {} widget(s) after removing its root".format(asset_path, len(left)))
    return None


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


def _halign(slot, name):
    slot.set_horizontal_alignment(getattr(unreal.HorizontalAlignment, "H_ALIGN_" + name))


def _valign(slot, name):
    slot.set_vertical_alignment(getattr(unreal.VerticalAlignment, "V_ALIGN_" + name))


def _text(widget, content, size, colour, bold=False, wrap=False, outline=1, justify=None):
    """A text block in the HUD's font: HudFont(size, bold, outline) and MakeText's colour."""
    widget.set_text(unreal.Text(content))
    font = widget.get_editor_property("font")
    font.set_editor_property("size", int(size))
    font.set_editor_property("typeface_font_name", "Bold" if bold else "Regular")
    try:
        outline_settings = font.get_editor_property("outline_settings")
        outline_settings.set_editor_property("outline_size", int(round(outline)))
        outline_settings.set_editor_property("outline_color", unreal.LinearColor(0.0, 0.0, 0.0, 1.0))
        font.set_editor_property("outline_settings", outline_settings)
    except Exception:  # noqa: BLE001 - cosmetic
        pass
    widget.set_editor_property("font", font)
    widget.set_color_and_opacity(unreal.SlateColor(colour))
    if wrap:
        widget.set_auto_wrap_text(True)
    if justify:
        widget.set_editor_property("justification", getattr(unreal.TextJustify, justify))


def _panel(border, colour, padding):
    border.set_brush_color(colour)
    border.set_padding(unreal.Margin(float(padding), float(padding), float(padding), float(padding)))


# ── B-15 Wave 4 frame art (the code HUD's ValhallaHudArt, baked into the designer) ──

FRAMES_FOLDER = "/Game/Valhalla/UI/Frames"

#: The panel border's drawn width, Slate px (UValhallaGameHUDWidget::PanelBorderThickness).
PANEL_BORDER_PX = 4


def _panel_frame_art(px=PANEL_BORDER_PX):
    """UValhallaGameHUDWidget::PanelFrameTextureName: T_UI_PanelFrame_NN, or None when not imported."""
    return _ui_texture("T_UI_PanelFrame_{:02d}".format(max(1, min(12, int(round(px))))))


def _ui_texture(name):
    """A HUD frame texture (T_UI_Panel, T_UI_Slot, ...) when it is imported, else None (flat colours)."""
    path = "{}/{}".format(FRAMES_FOLDER, name)
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        return None
    return unreal.load_asset(path)


def _png_size(path):
    """(width, height) from a PNG's IHDR, or None."""
    try:
        with open(path, "rb") as f:
            head = f.read(24)
        if head[:8] == b"\x89PNG\r\n\x1a\n" and head[12:16] == b"IHDR":
            return int.from_bytes(head[16:20], "big"), int.from_bytes(head[20:24], "big")
    except OSError:
        pass
    return None


def _texture_size(texture):
    """The texture's own pixels: the imported PNG's size when it is on disk (the built size is a
    32 x 32 placeholder while the editor is still compiling textures after startup), else the built size."""
    try:
        source = texture.get_editor_property("asset_import_data").get_first_filename()
        size = _png_size(source) if source else None
        if size and size[0] > 0 and size[1] > 0:
            return size
    except Exception:  # noqa: BLE001 - no import data (a generated texture)
        pass
    try:
        return max(1, texture.blueprint_get_size_x()), max(1, texture.blueprint_get_size_y())
    except Exception:  # noqa: BLE001
        return 64, 64


def _set_image_size(brush, x, y):
    """FSlateBrush::ImageSize is a DeprecateSlateVector2D in 5.x, not a Vector2D."""
    try:
        size = unreal.DeprecateSlateVector2D(float(x), float(y))
    except Exception:  # noqa: BLE001 - older / newer struct shapes
        size = brush.get_editor_property("image_size")
        size.set_editor_property("x", float(x))
        size.set_editor_property("y", float(y))
    brush.set_editor_property("image_size", size)


def _box_brush(texture, margin_px, border_px, tint=(1.0, 1.0, 1.0)):
    """ValhallaHudArt::BoxBrush: nine-slice, `margin_px` of the texture drawn `border_px` wide."""
    sx, sy = _texture_size(texture)
    scale = float(border_px) / max(1.0, float(margin_px[0]))
    brush = unreal.SlateBrush()
    brush.set_editor_property("resource_object", texture)
    _set_image_size(brush, sx * scale, sy * scale)
    brush.set_editor_property("draw_as", unreal.SlateBrushDrawType.BOX)
    mx, my = float(margin_px[0]) / sx, float(margin_px[1]) / sy
    brush.set_editor_property("margin", unreal.Margin(mx, my, mx, my))
    brush.set_editor_property("tint_color", unreal.SlateColor(unreal.LinearColor(tint[0], tint[1], tint[2], 1.0)))
    return brush


def _image_brush(texture):
    """ValhallaHudArt::ImageBrush."""
    sx, sy = _texture_size(texture)
    brush = unreal.SlateBrush()
    brush.set_editor_property("resource_object", texture)
    _set_image_size(brush, sx, sy)
    brush.set_editor_property("draw_as", unreal.SlateBrushDrawType.IMAGE)
    return brush


def _framed_panel(border, colour, padding, art):
    """MakePanel: the panel frame art for PANEL_BORDER_PX (T_UI_PanelFrame_NN, else `art`) when there is art
    and the panel shows, else flat."""
    if art is not None and colour.a > 0.0:
        border.set_brush(_panel_frame_brush(_box_brush(_panel_frame_art() or art, (1, 1), 1)))
        border.set_brush_color(unreal.LinearColor(1.0, 1.0, 1.0, max(float(colour.a), 0.94)))
        pad = float(padding) + 10.0
        border.set_padding(unreal.Margin(pad, pad, pad, pad))
    else:
        _panel(border, colour, padding)


def _button_art(button, art):
    """MakeButton's bronze plate: 10 px of the 64 x 32 texture is bevel, drawn 5 px."""
    if art is None:
        return
    style = button.get_editor_property("widget_style")
    style.set_editor_property("normal", _box_brush(art, (10, 10), 5))
    style.set_editor_property("hovered", _box_brush(art, (10, 10), 5, (1.25, 1.2, 1.1)))
    style.set_editor_property("pressed", _box_brush(art, (10, 10), 5, (0.75, 0.72, 0.68)))
    style.set_editor_property("normal_padding", unreal.Margin(8.0, 2.0, 8.0, 2.0))
    style.set_editor_property("pressed_padding", unreal.Margin(8.0, 3.0, 8.0, 1.0))
    button.set_editor_property("widget_style", style)


def _visibility(widget, name):
    widget.set_visibility(getattr(unreal.SlateVisibility, name))


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


def _enum_name(action):
    """``PartyLeave`` -> ``PARTY_LEAVE`` (how UE Python names enumerators)."""
    out = []
    for index, char in enumerate(action):
        if char.isupper() and index > 0:
            out.append("_")
        out.append(char.upper())
    return "".join(out)


def _finish(blueprint, asset_path, builder, extra):
    compile_error = ""
    try:
        compiled = bool(_umg_call("CompileWidgetBlueprint", blueprint))
    except Exception as exc:  # noqa: BLE001 - UMGToolSet raises with the compiler's messages
        compiled = False
        compile_error = str(exc)[:2000]
    saved = bool(unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False))
    result = {
        "asset": asset_path,
        "widgets": len(builder.added),
        "renamed": builder.renamed,
        "warnings": builder.warnings,
        "compiled": compiled,
        "saved": saved,
    }
    if compile_error:
        result["compileError"] = compile_error
    result.update(extra)
    return result


# ── Dispatch ──────────────────────────────────────────────────────────────

def layout_blueprint(asset_path=GAME_HUD_BP, replace=False):
    """Lay out WBP_GameHUD, WBP_HUDBar or WBP_HUDSlot, by the Blueprint's parent class."""
    if _is_child_of(asset_path, BAR_CLASS):
        return layout_bar(asset_path, replace)
    if _is_child_of(asset_path, SLOT_CLASS):
        return layout_slot(asset_path, replace)
    if _is_child_of(asset_path, OPTIONS_MENU_CLASS):
        return layout_options_menu(asset_path, replace)
    return _layout_game_hud(asset_path, replace)


def layout_game_hud(asset_path=GAME_HUD_BP, replace=False):
    """The step 2 entry point (``layout_hud_from_config``); dispatches like ``layout_blueprint``."""
    return layout_blueprint(asset_path, replace)


# ── WBP_HUDBar ────────────────────────────────────────────────────────────

def layout_bar(asset_path=BAR_BP, replace=False):
    """WBP_HUDBar's tree: 1.0's bar out of UValhallaHUDBarWidget's part names.

    Frame (T_UI_BarFrame nine-slice, 2 px padding; else a 1 px half-white
    stroke) > Background (dark, 1 px) > Sizer (the stand-alone size:
    hud.hpBar width x height) > Overlay of FillSizer/Fill (T_UI_BarFill
    tinted by the bar colour; left-aligned, C++ sets its width to fraction x
    BarWidth), OverlaySizer/OverlayFill (the shield wash) and a centred Label.
    A HUD that places one wraps it in a Size Box for its own size and sets
    BarWidth to the fill's full width (Size Box width - 6, or - 4 flat).
    """
    blueprint = unreal.load_asset(asset_path)
    if blueprint is None:
        raise RuntimeError("no asset at {} (run create_hud_blueprints first)".format(asset_path))
    refused = _clear_tree(blueprint, replace, asset_path)
    if refused:
        return refused
    config_path, cfg = _load_ui_config()
    W = unreal
    b = _TreeBuilder(blueprint)

    width = float(_get(cfg, "hud.hpBar.width", 200))
    height = float(_get(cfg, "hud.hpBar.height", 12))

    frame_art = _ui_texture("T_UI_BarFrame")
    fill_art = _ui_texture("T_UI_BarFill")

    frame, _ = b.add(W.Border.static_class(), "Frame", None, True)
    if frame_art is not None:
        # SetFrameArt: an iron frame (6 px of 128 x 24), drawn 3 px wide, 2 px padding.
        frame.set_brush(_box_brush(frame_art, (6, 6), 3))
        _panel(frame, W.LinearColor(1.0, 1.0, 1.0, 1.0), 2)
    else:
        _panel(frame, W.LinearColor(1.0, 1.0, 1.0, 0.5), 1)
    background, _ = b.add(W.Border.static_class(), "Background", frame, True)
    # Kevin (2026-09-25): the cast bar's sides looked too thick. The frame's
    # right (and bottom) bevel highlight sits 3-4 texels in from the edge,
    # under this well; a see-through well (0.7) let it show as a second grey
    # band beside the 2 px rim wherever the bar is empty (most of a cast),
    # while the left / top rim is the outer 2 texels only. An opaque well
    # leaves every side the same 2 px rim (the Frame padding). With the flat
    # fallback (no frame art) the old see-through well stays.
    well_alpha = 1.0 if frame_art is not None else _get(cfg, "hud.hpBar.colors.bgAlpha", 0.7)
    _panel(background, _colour(_get(cfg, "hud.hpBar.colors.bg", "#000000"), well_alpha), 1)
    sizer, _ = b.add(W.SizeBox.static_class(), "Sizer", background, True)
    sizer.set_width_override(width)
    sizer.set_height_override(height)
    layers, _ = b.add(W.Overlay.static_class(), "Layers", sizer)

    fill_sizer, s = b.add(W.SizeBox.static_class(), "FillSizer", layers, True)
    _halign(s, "LEFT")
    _valign(s, "FILL")
    fill_sizer.set_width_override(width)
    fill, _ = b.add(W.Border.static_class(), "Fill", fill_sizer, True)
    if fill_art is not None:
        # A glossy grey ramp the bar colour tints (SetFillColour sets the brush colour).
        fill.set_brush(_image_brush(fill_art))
    fill.set_brush_color(_colour(_get(cfg, "hud.hpBar.colors.high", "#44ff44")))

    wash_sizer, s = b.add(W.SizeBox.static_class(), "OverlaySizer", layers, True)
    _halign(s, "LEFT")
    _valign(s, "FILL")
    wash_sizer.set_width_override(0.0)
    _visibility(wash_sizer, "COLLAPSED")
    wash, _ = b.add(W.Border.static_class(), "OverlayFill", wash_sizer, True)
    wash.set_brush_color(_colour("#00ccff", 0.45))

    label, s = b.add(W.TextBlock.static_class(), "Label", layers, True)
    _halign(s, "CENTER")
    _valign(s, "CENTER")
    _text(label, "", 7, W.LinearColor(1, 1, 1, 1), bold=True, justify="CENTER")
    _visibility(label, "HIT_TEST_INVISIBLE")

    result = _finish(blueprint, asset_path, b, {"config": config_path})
    generated = _blueprint_class(asset_path)
    if generated is not None:
        cdo = unreal.get_default_object(generated)
        cdo.set_editor_property("bar_width", width)
        unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False)
    result["parts"] = [n for n in BAR_PARTS if n in b.added]
    return result


# ── WBP_HUDSlot ───────────────────────────────────────────────────────────

def layout_slot(asset_path=SLOT_BP, replace=False):
    """WBP_HUDSlot's tree: the code-built cell out of UValhallaHUDSlotWidget's part names.

    Sizer (actionBar.slotSize square) > Frame (T_UI_Slot nine-slice, 3 px
    padding, over a clear Fill; else the actionBar border 1 px over the
    inventory slotBg) > Fill > Stack: Icon, SkillTile, Abbrev, CooldownSizer/
    CooldownFill (bottom-aligned sweep), CooldownText, KeyLabel (bottom
    right), QuantityText (top right). No SelectionRing: a selected cell tints
    Frame in the highlight colour.
    """
    blueprint = unreal.load_asset(asset_path)
    if blueprint is None:
        raise RuntimeError("no asset at {} (run create_hud_blueprints first)".format(asset_path))
    refused = _clear_tree(blueprint, replace, asset_path)
    if refused:
        return refused
    config_path, cfg = _load_ui_config()
    W = unreal
    b = _TreeBuilder(blueprint)
    size = float(_get(cfg, "actionBar.slotSize", 44))

    sizer, _ = b.add(W.SizeBox.static_class(), "Sizer", None, True)
    sizer.set_width_override(size)
    sizer.set_height_override(size)
    slot_art = _ui_texture("T_UI_Slot")
    frame, _ = b.add(W.Border.static_class(), "Frame", sizer, True)
    fill, _ = b.add(W.Border.static_class(), "Fill", frame, True)
    if slot_art is not None:
        # SetFrameArt: 10 px of the 64 px texture is rim, drawn 4 px wide; the well shows through.
        frame.set_brush(_box_brush(slot_art, (10, 10), 4))
        _panel(frame, W.LinearColor(1.0, 1.0, 1.0, 1.0), 3)
        _panel(fill, W.LinearColor(0.0, 0.0, 0.0, 0.0), 0)
    else:
        _panel(frame, _colour(_get(cfg, "actionBar.colors.border", "#666666")), 1)
        _panel(fill, _colour(_get(cfg, "inventory.colors.slotBg", "#2a2a3e")), 0)
    stack, _ = b.add(W.Overlay.static_class(), "Stack", fill, True)

    def layer(widget_class, name, h, v, pad=(0, 0, 0, 0)):
        widget, s = b.add(widget_class, name, stack, True)
        _halign(s, h)
        _valign(s, v)
        _pad(s, *pad)
        return widget

    icon = layer(W.Image.static_class(), "Icon", "FILL", "FILL", (2, 2, 2, 2))
    _visibility(icon, "COLLAPSED")
    tile = layer(W.Border.static_class(), "SkillTile", "FILL", "FILL", (3, 3, 3, 3))
    _visibility(tile, "COLLAPSED")
    abbrev = layer(W.TextBlock.static_class(), "Abbrev", "CENTER", "CENTER")
    _text(abbrev, "", 11, W.LinearColor(1, 1, 1, 1), bold=True, justify="CENTER")
    sweep = layer(W.SizeBox.static_class(), "CooldownSizer", "FILL", "BOTTOM")
    sweep.set_height_override(0.0)
    _visibility(sweep, "COLLAPSED")
    sweep_fill, _ = b.add(W.Border.static_class(), "CooldownFill", sweep, True)
    sweep_fill.set_brush_color(_colour(_get(cfg, "actionBar.colors.cooldownOverlay", "#880000"),
                                       _get(cfg, "actionBar.colors.cooldownOverlayAlpha", 0.6)))
    cooldown_text = layer(W.TextBlock.static_class(), "CooldownText", "CENTER", "CENTER")
    _text(cooldown_text, "", 12, W.LinearColor(1, 1, 1, 1), bold=True, justify="CENTER")
    _visibility(cooldown_text, "COLLAPSED")
    key = layer(W.TextBlock.static_class(), "KeyLabel", "RIGHT", "BOTTOM", (0, 0, 2, 0))
    _text(key, "", 7, _colour(_get(cfg, "actionBar.colors.keyLabelColor", "#888888")))
    quantity = layer(W.TextBlock.static_class(), "QuantityText", "RIGHT", "TOP", (0, 0, 2, 0))
    _text(quantity, "", 8, W.LinearColor(1, 1, 1, 1), bold=True)

    result = _finish(blueprint, asset_path, b, {"config": config_path})
    result["parts"] = [n for n in SLOT_PARTS if n in b.added]
    return result


# ── WBP_GameHUD ───────────────────────────────────────────────────────────

def _layout_game_hud(asset_path=GAME_HUD_BP, replace=False):
    """Give WBP_GameHUD a designer tree from ui-config.json, named for the C++ bindings.

    Every panel sits where the code build (BuildVitals, BuildActionBar, ...)
    puts it, with the same canvas anchors and alignment, so it moves with the
    viewport exactly as the code HUD does. Refuses a Blueprint that already
    has widgets unless ``replace`` (which removes the old root and everything
    under it). Compiles and saves.
    """
    blueprint = unreal.load_asset(asset_path)
    if blueprint is None:
        raise RuntimeError("no asset at {} (run create_hud_blueprints first)".format(asset_path))
    refused = _clear_tree(blueprint, replace, asset_path)
    if refused:
        return refused

    config_path, cfg = _load_ui_config()
    W = unreal
    bar_class = _blueprint_class(BAR_BP) or _load_class(BAR_CLASS)
    designer_bars = bar_class.get_name() != "ValhallaHUDBarWidget"
    button_class = _load_class("/Script/ValhallaGame.ValhallaHUDButton")
    b = _TreeBuilder(blueprint)
    panel_art = _ui_texture("T_UI_Panel")
    button_art = _ui_texture("T_UI_Button")
    # A framed bar is its fill plus Frame + Background padding on each side:
    # 1 + 1 flat, 2 + 1 with T_UI_BarFrame (layout_bar / SetFrameArt).
    bar_extra = 6.0 if (designer_bars and _ui_texture("T_UI_BarFrame") is not None) else 4.0

    # Numbers the C++ code layout uses (FValhallaUIConfig defaults when missing).
    hp_x = float(_get(cfg, "hud.hpBar.x", 16))
    hp_w = float(_get(cfg, "hud.hpBar.width", 200))
    hp_h = float(_get(cfg, "hud.hpBar.height", 12))
    hp_off = float(_get(cfg, "hud.hpBar.yOffsetFromBottom", 36))
    mana_w = float(_get(cfg, "hud.manaBar.width", 200))
    mana_h = float(_get(cfg, "hud.manaBar.height", 12))
    ab_slot = float(_get(cfg, "actionBar.slotSize", 44))
    ab_pad = float(_get(cfg, "actionBar.padding", 6))
    ab_bottom = float(_get(cfg, "actionBar.bottomMargin", 8))
    cast_w = float(_get(cfg, "castBar.width", 260))
    cast_h = float(_get(cfg, "castBar.height", 16))
    cast_above = float(_get(cfg, "castBar.yAboveActionBar", 12))
    chat_w = float(_get(cfg, "chat.maxWidth", 360))
    chat_h = float(_get(cfg, "chat.height", 170))
    chat_bottom = float(_get(cfg, "chat.bottomMargin", 8))
    chat_pad = float(_get(cfg, "chat.padding", 6))
    chat_input_h = float(_get(cfg, "chat.inputHeight", 18))
    chat_font = _px(_get(cfg, "chat.fontSize", "11px"), 11)
    chat_bg_hex = _get(cfg, "chat.bgColor", "#0a0a1a")
    inv_bg_hex = _get(cfg, "inventory.colors.bg", "#1a1a2e")
    inv_bg = _colour(inv_bg_hex, _get(cfg, "inventory.colors.bgAlpha", 0.95))
    title_c = _colour(_get(cfg, "inventory.colors.titleColor", "#ffcc00"))
    label_c = _colour(_get(cfg, "inventory.colors.labelColor", "#aaaacc"))
    value_c = _colour(_get(cfg, "inventory.colors.valueColor", "#ffffff"))
    highlight_c = _colour(_get(cfg, "inventory.colors.highlight", "#ffaa00"))
    close_c = _colour("#8888aa")
    char_w = float(_get(cfg, "inventory.charPanelWidth", 220))
    panel_gap = float(_get(cfg, "inventory.panelGap", 8))
    panel_h = float(_get(cfg, "inventory.panelHeight", 340))
    slot_gap = float(_get(cfg, "inventory.slotGap", 4))
    plate_font = _px(_get(cfg, "nameplates.fontSize", "12px"), 12)
    plate_outline = float(_get(cfg, "nameplates.strokeThickness", 3)) * 0.5

    unset_actions = []

    def button(parent, caption, action, tint):
        """MakeButton: a UValhallaHUDButton, tinted, with a 9 pt bold black caption."""
        widget, slot = b.add(button_class, "Btn" + action, parent)
        widget.set_background_color(tint)
        _button_art(widget, button_art)
        if not _set_button_action(widget, _enum_name(action)):
            unset_actions.append("Btn" + action)
        label, _ = b.add(W.TextBlock.static_class(), "Btn" + action + "Label", widget)
        _text(label, caption, 9, W.LinearColor(0, 0, 0, 1), bold=True, outline=0)
        return widget, slot

    def bar(parent, name, width, height):
        """A bar at MakeBar(width, height)'s size: a Size Box of the framed size holding it."""
        box, box_slot = b.add(W.SizeBox.static_class(), name + "Size", parent)
        box.set_width_override(width + bar_extra)
        box.set_height_override(height + bar_extra)
        widget, _ = b.add(bar_class, name, box, True)
        if designer_bars:
            widget.set_editor_property("bar_width", float(width))
        return widget, box_slot

    root, _ = b.add(W.CanvasPanel.static_class(), "HUDRoot")

    # ── vitals: class line, HP, mana/energy; bottom left ──
    # Kevin (2026-09-25): HP on top, the resource bar under it (the code HUD
    # had mana above HP). The block keeps its footprint: its bottom (now the
    # resource bar's) sits where the HP bar's was, -(yOffsetFromBottom + 2)
    # + the framed height; the class line's bottom 4 px above the HP bar, 2 px
    # further right than the bars; between the bars the old gap
    # (gapAboveHp - frame padding, i.e. none with the framed bars).
    vitals, s = b.add(W.VerticalBox.static_class(), "VitalsPanel", root, True)
    hp_bottom = hp_off + 2.0 - (hp_h + bar_extra)
    _canvas(s, (0, 1), (0, 1), (hp_x - 2.0, -hp_bottom), 0)
    w, s = b.add(W.TextBlock.static_class(), "ClassText", vitals, True)
    _text(w, "", _px(_get(cfg, "hud.classText.fontSize", "12px"), 12), _colour(_get(cfg, "hud.classText.color", "#ffffff")),
          outline=float(_get(cfg, "hud.classText.strokeThickness", 2)) * 0.5)
    _pad(s, 2, 0, 0, 4)
    _halign(s, "LEFT")
    _, s = bar(vitals, "HpBar", hp_w, hp_h)
    _pad(s, 0, 0, 0, max(0.0, float(_get(cfg, "hud.manaBar.gapAboveHp", 6)) - bar_extra))
    _halign(s, "LEFT")
    _, s = bar(vitals, "ManaBar", mana_w, mana_h)
    _halign(s, "LEFT")

    # ── action bar (BuildActionBar): bottom centre ──
    ab_panel, s = b.add(W.Border.static_class(), "ActionBarFrame", root)
    _canvas(s, (0.5, 1), (0.5, 1), (0, -ab_bottom), 5)
    _framed_panel(ab_panel, _colour(_get(cfg, "actionBar.colors.bg", "#1a1a1a"), _get(cfg, "actionBar.colors.bgAlpha", 0.95)), ab_pad, panel_art)
    b.add(W.HorizontalBox.static_class(), "ActionBarRow", ab_panel, True)

    # ── cast bar (BuildCastBar): yAboveActionBar over the action bar ──
    _, s = bar(root, "CastBar", cast_w, cast_h)
    _canvas(s, (0.5, 1), (0.5, 1), (0, -(ab_bottom + ab_slot + ab_pad * 2 + cast_above)), 6)

    # ── target frame (BuildTargetFrame): top centre ──
    target, s = b.add(W.Border.static_class(), "TargetFramePanel", root, True)
    _canvas(s, (0.5, 0), (0.5, 0), (0, 12), 5)
    _framed_panel(target, _colour(inv_bg_hex, 0.85), 6, panel_art)
    # Eats clicks (C++ handles a left press on it): a click on the frame must
    # not reach the world, where clicking nothing clears the target.
    _visibility(target, "VISIBLE")
    column, _ = b.add(W.VerticalBox.static_class(), "TargetColumn", target)
    w, _ = b.add(W.TextBlock.static_class(), "TargetName", column, True)
    _text(w, "Target", plate_font + 2, _colour(_get(cfg, "nameplates.color", "#ffffff")), bold=True, outline=plate_outline)
    _, s = bar(column, "TargetHpBar", 240.0, 14.0)
    _pad(s, 0, 3, 0, 0)
    _halign(s, "LEFT")
    _, s = b.add(W.HorizontalBox.static_class(), "TargetBuffs", column, True)
    _pad(s, 0, 4, 0, 0)

    # ── party (BuildPartyFrame): top left; invite prompt top centre ──
    party, s = b.add(W.Border.static_class(), "PartyPanel", root, True)
    _canvas(s, (0, 0), (0, 0), (12, 12), 5)
    _framed_panel(party, _colour("#12122a", 0.92), 6, panel_art)
    # Eats clicks too; C++ makes each member row a button (click = target them).
    _visibility(party, "VISIBLE")
    column, _ = b.add(W.VerticalBox.static_class(), "PartyColumn", party)
    header, _ = b.add(W.HorizontalBox.static_class(), "PartyHeader", column)
    w, s = b.add(W.TextBlock.static_class(), "PartyTitle", header, True)
    _text(w, "Party", 9, title_c, bold=True)
    _fill(s)
    _valign(s, "CENTER")
    button(header, "Leave", "PartyLeave", close_c)
    b.add(W.VerticalBox.static_class(), "PartyList", column, True)

    invite, s = b.add(W.Border.static_class(), "InvitePanel", root, True)
    _canvas(s, (0.5, 0), (0.5, 0), (0, 110), 20)
    _framed_panel(invite, inv_bg, 10, panel_art)
    column, _ = b.add(W.VerticalBox.static_class(), "InviteColumn", invite)
    w, s = b.add(W.TextBlock.static_class(), "InviteText", column, True)
    _text(w, "", 10, title_c, bold=True)
    _halign(s, "CENTER")
    row, s = b.add(W.HorizontalBox.static_class(), "InviteButtons", column)
    _pad(s, 0, 8, 0, 0)
    _, s = button(row, "Accept", "PartyAccept", _colour("#44cc66"))
    _pad(s, 4, 0)
    _, s = button(row, "Decline", "PartyDecline", _colour("#cc5555"))
    _pad(s, 4, 0)

    # ── combat log (BuildCombatLog): top right, 320 x 200 inside a 1 px rim ──
    log, s = b.add(W.Border.static_class(), "CombatLogPanel", root, True)
    _canvas(s, (1, 0), (1, 0), (-12, 12), 5)
    log_fill, _ = b.add(W.Border.static_class(), "CombatLogFill", log)
    if panel_art is not None:
        # The code log is a cell with the panel art as its frame (9 px padding) over a clear well.
        log.set_brush(_panel_frame_brush(_box_brush(_panel_frame_art() or panel_art, (1, 1), 1)))
        _panel(log, W.LinearColor(1.0, 1.0, 1.0, 1.0), 9)
        _panel(log_fill, W.LinearColor(0.0, 0.0, 0.0, 0.0), 0)
    else:
        _panel(log, _colour(_get(cfg, "chat.borderColor", "#333355"), _get(cfg, "chat.borderAlpha", 0.9)), 1)
        _panel(log_fill, _colour(chat_bg_hex, _get(cfg, "chat.bgAlpha", 0.72)), 0)
    box, _ = b.add(W.SizeBox.static_class(), "CombatLogSize", log_fill)
    box.set_width_override(320.0)
    box.set_height_override(200.0)
    column, _ = b.add(W.VerticalBox.static_class(), "CombatLogColumn", box)
    w, s = b.add(W.TextBlock.static_class(), "CombatLogTitle", column, True)
    _text(w, "Combat Log", 8, _colour("#ccaa66"), bold=True)
    _pad(s, 6, 3)
    _, s = b.add(W.ScrollBox.static_class(), "CombatLogScroll", column, True)
    _fill(s)
    _pad(s, 6, 0, 4, 4)

    # ── chat (BuildChat): bottom left, right of the vitals ──
    chat, s = b.add(W.Border.static_class(), "ChatPanel", root, True)
    _canvas(s, (0, 1), (0, 1), (hp_x + hp_w + 16, -chat_bottom), 8)
    _panel(chat, _colour(chat_bg_hex, 0.0), chat_pad)
    _visibility(chat, "SELF_HIT_TEST_INVISIBLE")
    box, _ = b.add(W.SizeBox.static_class(), "ChatSize", chat)
    box.set_width_override(chat_w)
    # The code box is chat.height tall while typing; a designer's chat keeps
    # its own size, so it grows with its lines up to that.
    box.set_max_desired_height(chat_h)
    column, _ = b.add(W.VerticalBox.static_class(), "ChatColumn", box)
    w, _ = b.add(W.TextBlock.static_class(), "ChatChannelText", column, True)
    _text(w, "[General]", chat_font, _colour(_get(cfg, "chat.colors.system", "#ffaa44")), bold=True)
    _visibility(w, "COLLAPSED")
    scroll, s = b.add(W.ScrollBox.static_class(), "ChatScroll", column, True)
    _fill(s)
    _visibility(scroll, "SELF_HIT_TEST_INVISIBLE")
    w, s = b.add(W.EditableTextBox.static_class(), "ChatInput", column, True)
    w.set_hint_text(unreal.Text("Say something — /g /world /p /w <name> /invite /accept /decline /leave"))
    _pad(s, 0, 3, 0, 0)
    _visibility(w, "COLLAPSED")
    _chat_input_style(b, w, chat_font, chat_input_h, _colour(chat_bg_hex, 0.95), _colour(_get(cfg, "chat.colors.general", "#ffffff")))

    # ── loot (BuildLootPanel): right of centre, bottom edge 40 px above it ──
    loot, s = b.add(W.Border.static_class(), "LootPanel", root, True)
    _canvas(s, (0.5, 0.5), (0.5, 1), (220, -40), 15)
    _framed_panel(loot, inv_bg, 8, panel_art)
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
    _, s = button(row, "Loot All", "LootAll", highlight_c)
    _pad(s, 0, 0, 6, 0)
    button(row, "Close", "LootClose", close_c)

    # ── skills (BuildSkillsPane): left, middle ──
    skills, s = b.add(W.Border.static_class(), "SkillsPanel", root, True)
    _canvas(s, (0, 0.5), (0, 0.5), (16, -40), 12)
    _framed_panel(skills, inv_bg, 8, panel_art)
    column, _ = b.add(W.VerticalBox.static_class(), "SkillsColumn", skills)
    header, _ = b.add(W.HorizontalBox.static_class(), "SkillsHeader", column)
    w, s = b.add(W.TextBlock.static_class(), "SkillsTitle", header)
    _text(w, "Skills  (K)", 10, title_c, bold=True)
    _fill(s)
    button(header, "X", "SkillsClose", close_c)
    w, s = b.add(W.TextBlock.static_class(), "SkillsHint", column, True)
    _text(w, "Drag a skill to the action bar, or click it and then click a slot.", 7, label_c, wrap=True)
    _pad(s, 0, 2, 0, 6)
    box, _ = b.add(W.SizeBox.static_class(), "SkillsSize", column)
    box.set_width_override(300.0)
    box.set_max_desired_height(panel_h + 40.0)
    scroll, _ = b.add(W.ScrollBox.static_class(), "SkillsScroll", box)
    b.add(W.VerticalBox.static_class(), "SkillsList", scroll, True)

    # ── character + inventory (BuildInventoryPanel, I): centre ──
    # B-21: two root-canvas children (each is a movable panel the player can
    # place on its own), no longer one InventoryPair Horizontal Box. The
    # default look is the pair's: side by side, panel_gap apart, the two
    # together centred at (0, -30), one height. Their widths are fixed (the
    # character sheet's char_w, the inventory grid's cols x (slot + gap)) plus
    # the framed panel's padding on each side, so the split point is known: the
    # character panel's right edge sits at (cw - gap - iw) / 2 from the centre.
    frame_pad = 8.0 + (10.0 if panel_art is not None else 0.0)  # _framed_panel(.., 8, art)
    inv_slot = _inventory_slot_size()
    inv_cols = int(_get(cfg, "inventory.cols", 8))
    char_outer_w = char_w + 2.0 * frame_pad
    inv_outer_w = inv_cols * (inv_slot + slot_gap) + 2.0 * frame_pad
    char_right_x = (char_outer_w - panel_gap - inv_outer_w) * 0.5
    # One height for both, as the Horizontal Box made them: the character
    # column of the tallest class (503 px measured in PIE, 2026-09-24; the
    # stats text is class-dependent), rounded up.
    pair_min_h = max(panel_h, 504.0)

    # Character sheet (B-07 step 3): title, the nine equipment rows C++ adds,
    # "Level n   XP x / y (z%)", a 200 x 10 XP bar, then the resolved stats
    # (C++ writes them as one multi-line text), with room for 16 lines.
    stat_font = 7
    stat_line = int(round(stat_font * 96.0 / 72.0 * 1.25))
    char_panel, s = b.add(W.Border.static_class(), "CharacterPanel", root, True)
    _canvas(s, (0.5, 0.5), (1.0, 0.5), (char_right_x, -30), 10)
    _framed_panel(char_panel, inv_bg, 8, panel_art)
    box, _ = b.add(W.SizeBox.static_class(), "CharacterSize", char_panel)
    box.set_width_override(char_w)
    box.set_min_desired_height(pair_min_h)
    column, _ = b.add(W.VerticalBox.static_class(), "CharacterColumn", box)
    w, s = b.add(W.TextBlock.static_class(), "CharacterTitle", column)
    _text(w, "Character  (I)", 10, title_c, bold=True)
    _pad(s, 0, 0, 0, 4)
    b.add(W.VerticalBox.static_class(), "EquipmentPanel", column, True)
    w, s = b.add(W.TextBlock.static_class(), "CharacterLevel", column, True)
    _text(w, "Level 1   XP 0 / 100 (0%)", stat_font, value_c)
    _pad(s, 0, 6, 0, 2)
    _, s = bar(column, "XpBar", 200.0 - bar_extra, 10.0 - bar_extra)  # 200 x 10 framed
    _pad(s, 0, 0, 0, 2)
    _halign(s, "LEFT")
    box, s = b.add(W.SizeBox.static_class(), "CharacterStatsSize", column)
    box.set_min_desired_height(float(stat_line * 16))
    _pad(s, 0, 2, 0, 0)
    w, _ = b.add(W.TextBlock.static_class(), "CharacterStats", box, True)
    _text(w, "", stat_font, value_c, wrap=True, justify="LEFT")

    inv_panel, s = b.add(W.Border.static_class(), "InventoryPanel", root, True)
    _canvas(s, (0.5, 0.5), (0.0, 0.5), (char_right_x + panel_gap, -30), 10)
    _framed_panel(inv_panel, inv_bg, 8, panel_art)
    box, _ = b.add(W.SizeBox.static_class(), "InventorySize", inv_panel)
    box.set_min_desired_height(pair_min_h)
    column, _ = b.add(W.VerticalBox.static_class(), "InventoryColumn", box)
    header, s = b.add(W.HorizontalBox.static_class(), "InventoryHeader", column)
    _pad(s, 0, 0, 0, 4)
    w, s = b.add(W.TextBlock.static_class(), "InventoryTitle", header, True)
    _text(w, "Inventory  (I)", 10, title_c, bold=True)
    _fill(s)
    button(header, "X", "InventoryClose", close_c)
    grid, _ = b.add(W.UniformGridPanel.static_class(), "InventoryGrid", column, True)
    grid.set_slot_padding(unreal.Margin(slot_gap * 0.5, slot_gap * 0.5, slot_gap * 0.5, slot_gap * 0.5))
    w, s = b.add(W.TextBlock.static_class(), "InventoryHint", column)
    _text(w, "click: equip / unequip    right-click: drop    drag: move or swap", 7, label_c)
    _pad(s, 0, 6, 0, 0)

    # ── tooltip (on the root canvas: C++ moves it), drop confirm, death ──
    tip, s = b.add(W.Border.static_class(), "TooltipPanel", root, True)
    _canvas(s, (0, 0), (0, 0), (0, 0), 50)
    _framed_panel(tip, _colour("#0c0c18", 0.97), 8, panel_art)
    _visibility(tip, "HIT_TEST_INVISIBLE")
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
    _framed_panel(drop, _colour(inv_bg_hex, 0.98), 12, panel_art)
    column, _ = b.add(W.VerticalBox.static_class(), "DropColumn", drop)
    w, s = b.add(W.TextBlock.static_class(), "DropText", column, True)
    _text(w, "", 10, title_c, bold=True)
    _halign(s, "CENTER")
    row, s = b.add(W.HorizontalBox.static_class(), "DropButtons", column)
    _pad(s, 0, 8, 0, 0)
    _, s = button(row, "Drop", "DropConfirm", _colour("#cc5555"))
    _pad(s, 4, 0)
    _, s = button(row, "Cancel", "DropCancel", close_c)
    _pad(s, 4, 0)

    death, s = b.add(W.Border.static_class(), "DeathOverlay", root, True)
    _canvas(s, (0, 0, 1, 1), None, None, 100, stretch=True)
    death.set_brush_color(W.LinearColor(0, 0, 0, float(_get(cfg, "deathOverlay.bgAlpha", 0.7))))
    death.set_editor_property("horizontal_alignment", W.HorizontalAlignment.H_ALIGN_CENTER)
    death.set_editor_property("vertical_alignment", W.VerticalAlignment.V_ALIGN_CENTER)
    _visibility(death, "HIT_TEST_INVISIBLE")
    w, _ = b.add(W.TextBlock.static_class(), "DeathText", death, True)
    _text(w, "YOU DIED", _px(_get(cfg, "deathOverlay.fontSize", "32px"), 32), _colour(_get(cfg, "deathOverlay.textColor", "#ff4444")),
          bold=True, outline=2, justify="CENTER")

    # ── the options cog (B-21 step 4): bottom right, over the panels ──
    cog_art = _ui_texture("T_UI_Cog")
    cog, s = b.add(button_class, "OptionsButton", root, True)
    _canvas(s, (1, 1), (1, 1), (-12, -12), 9)
    s.set_auto_size(False)
    s.set_size(unreal.Vector2D(36.0, 36.0))
    if not _set_button_action(cog, "OPTIONS"):
        unset_actions.append("OptionsButton")
    _not_focusable(cog)
    try:
        cog.set_tool_tip_text(unreal.Text("Options (Esc)"))
    except Exception:  # noqa: BLE001 - cosmetic
        pass
    if cog_art is not None:
        style = cog.get_editor_property("widget_style")
        for state, tint in (("normal", 1.0), ("hovered", 1.25), ("pressed", 0.75)):
            brush = _image_brush(cog_art)
            brush.set_editor_property("tint_color", unreal.SlateColor(unreal.LinearColor(tint, tint, tint, 1.0)))
            style.set_editor_property(state, brush)
        style.set_editor_property("normal_padding", unreal.Margin(0.0, 0.0, 0.0, 0.0))
        style.set_editor_property("pressed_padding", unreal.Margin(0.0, 0.0, 0.0, 0.0))
        cog.set_editor_property("widget_style", style)
    else:
        b.warn("T_UI_Cog is not imported; the options button is a plain plate")
        _button_art(cog, button_art)
        label, _ = b.add(W.TextBlock.static_class(), "OptionsButtonLabel", cog)
        _text(label, "Opt", 8, W.LinearColor(0, 0, 0, 1), bold=True, outline=0)

    missing = [n for n in REQUIRED_PANELS + OPTIONAL_PANELS if n not in b.added]
    return _finish(blueprint, asset_path, b, {
        "config": config_path,
        "barClass": bar_class.get_path_name(),
        "panelsMissing": missing,
        "buttonsWithoutAction": unset_actions,
    })


def _inventory_slot_size():
    """WBP_GameHUD's InventorySlotSize Class Default (UValhallaGameHUDWidget "Valhalla|HUD Style"), else 48."""
    try:
        generated = _blueprint_class(GAME_HUD_BP)
        if generated is not None:
            return float(unreal.get_default_object(generated).get_editor_property("inventory_slot_size"))
    except Exception:  # noqa: BLE001 - the C++ default
        pass
    return 48.0


def _chat_input_style(builder, box, font_size, input_height, background, foreground):
    """BuildChat's input style: chat.fontSize text on the panel's colours, chat.inputHeight tall."""
    try:
        style = box.get_editor_property("widget_style")
        text_style = style.get_editor_property("text_style")
        font = text_style.get_editor_property("font")
        font.set_editor_property("size", int(font_size))
        text_style.set_editor_property("font", font)
        style.set_editor_property("text_style", text_style)
        pad_v = max(1.0, (input_height - font_size * 1.35) * 0.5)
        style.set_editor_property("padding", unreal.Margin(4.0, pad_v, 4.0, pad_v))
        style.set_editor_property("background_color", unreal.SlateColor(background))
        style.set_editor_property("foreground_color", unreal.SlateColor(foreground))
        style.set_editor_property("focused_foreground_color", unreal.SlateColor(foreground))
        box.set_editor_property("widget_style", style)
    except Exception as exc:  # noqa: BLE001 - the box still works with the engine style
        builder.warn("ChatInput style not applied: {}".format(exc))


# ── B-21 step 4: WBP_OptionsMenu ──────────────────────────────────────────

def _not_focusable(widget):
    """Buttons, check boxes and sliders keep keyboard focus with the game (WASD keeps walking)."""
    try:
        widget.set_editor_property("is_focusable", False)
    except Exception:  # noqa: BLE001 - not every widget has it
        pass


def create_options_menu():
    """WBP_OptionsMenu (parent UValhallaOptionsMenuWidget), created only when it does not exist."""
    if unreal.EditorAssetLibrary.does_asset_exist(OPTIONS_MENU_BP):
        return {"asset": OPTIONS_MENU_BP, "existed": True}
    factory = unreal.WidgetBlueprintFactory()
    factory.set_editor_property("parent_class", _load_class(OPTIONS_MENU_CLASS))
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    blueprint = tools.create_asset("WBP_OptionsMenu", HUD_FOLDER, unreal.WidgetBlueprint, factory)
    if blueprint is None:
        raise RuntimeError("create_asset returned None for {}".format(OPTIONS_MENU_BP))
    unreal.EditorAssetLibrary.save_asset(OPTIONS_MENU_BP, only_if_is_dirty=False)
    return {"asset": OPTIONS_MENU_BP, "created": True}


def wire_options_menu():
    """WBP_GameHUD's OptionsMenuClass -> WBP_OptionsMenu (compiled, saved)."""
    hud_class = _blueprint_class(GAME_HUD_BP)
    menu_class = _blueprint_class(OPTIONS_MENU_BP)
    if hud_class is None or menu_class is None:
        return {"wired": False, "why": "WBP_GameHUD or WBP_OptionsMenu missing"}
    cdo = unreal.get_default_object(hud_class)
    cdo.set_editor_property("options_menu_class", menu_class)
    blueprint = unreal.load_asset(GAME_HUD_BP)
    out = {"options_menu_class": menu_class.get_path_name()}
    try:
        out["compiled"] = bool(_umg_call("CompileWidgetBlueprint", blueprint))
    except Exception as exc:  # noqa: BLE001
        out["compiled"] = False
        out["compileError"] = str(exc)[:400]
    out["saved"] = bool(unreal.EditorAssetLibrary.save_asset(GAME_HUD_BP, only_if_is_dirty=False))
    return out


def layout_options_menu(asset_path=OPTIONS_MENU_BP, replace=False, wire=True):
    """WBP_OptionsMenu's tree: the options menu's look, named for UValhallaOptionsMenuWidget.

    MenuFrame (T_UI_Panel) > MenuSize (480 x 470) > MenuColumn: a header
    (title, CloseButton), the tab bar (LayoutTabButton ... ControlsTabButton),
    and a dark well holding the ``Tabs`` widget switcher with five pages:

    0 Layout      LockCheck, UiScaleSlider / UiScaleText, OpacitySlider /
                  OpacityText, BorderSlider / BorderText, Show<Panel>Check x 7,
                  ResetLayoutButton
    1 Colours     ColourEditor (EditTitle, PresetGrid, Hue / Saturation /
                  ValueSlider, EditSwatch, ColourOkButton, ColourCancelButton),
                  ColourList (in a scroll box), ResetColoursButton
    2 Chat & log  ChatFontSizeSlider / Text, ChatLinesSlider / Text,
                  TimestampsCheck, LogFilterList, ResetChatButton
    3 Nameplates  NpcNameplatesCheck, PlayerNameplatesCheck, FloatingTextCheck,
                  NameplateFontSlider / Text, ResetNameplatesButton
    4 Controls    ControlsText

    Slider ranges, the rows in ColourList / PresetGrid / LogFilterList and
    every value come from C++. Creates the asset when missing (checked
    first), refuses a tree unless ``replace``, compiles, saves, and (``wire``)
    sets WBP_GameHUD's OptionsMenuClass to it.
    """
    created = create_options_menu() if asset_path == OPTIONS_MENU_BP else {}
    blueprint = unreal.load_asset(asset_path)
    if blueprint is None:
        raise RuntimeError("no asset at {}".format(asset_path))
    refused = _clear_tree(blueprint, replace, asset_path)
    if refused:
        return refused

    W = unreal
    b = _TreeBuilder(blueprint)
    panel_art = _ui_texture("T_UI_Panel")
    button_art = _ui_texture("T_UI_Button")
    title_c = _colour("#ffcc00")
    label_c = _colour("#aaaacc")
    value_c = _colour("#ddd6c4")
    close_c = _colour("#8888aa")

    def text(parent, name, content, size=8, colour=None, bold=False, wrap=False, variable=False):
        widget, slot = b.add(W.TextBlock.static_class(), name, parent, variable)
        _text(widget, content, size, colour or value_c, bold=bold, wrap=wrap)
        return widget, slot

    def button(parent, name, caption, tint=None):
        widget, slot = b.add(W.Button.static_class(), name, parent, True)
        widget.set_background_color(tint or close_c)
        _button_art(widget, button_art)
        _not_focusable(widget)
        text(widget, name + "Label", caption, 8, W.LinearColor(0, 0, 0, 1), bold=True)
        return widget, slot

    def check_row(parent, name, label):
        row, row_slot = b.add(W.HorizontalBox.static_class(), name + "Row", parent)
        _pad(row_slot, 0, 2)
        check, s = b.add(W.CheckBox.static_class(), name, row, True)
        _not_focusable(check)
        _valign(s, "CENTER")
        _, s = text(row, name + "Label", label, wrap=True)
        _fill(s)
        _valign(s, "CENTER")
        _pad(s, 6, 0, 0, 0)
        return check

    def slider_row(parent, name, label, value_name=None):
        row, row_slot = b.add(W.HorizontalBox.static_class(), name + "Row", parent)
        _pad(row_slot, 0, 3)
        box, s = b.add(W.SizeBox.static_class(), name + "LabelSize", row)
        box.set_width_override(120.0)
        _valign(s, "CENTER")
        text(box, name + "Label", label)
        slider, s = b.add(W.Slider.static_class(), name, row, True)
        _not_focusable(slider)
        _fill(s)
        _valign(s, "CENTER")
        if value_name:
            box, s = b.add(W.SizeBox.static_class(), value_name + "Size", row)
            box.set_width_override(80.0)
            _valign(s, "CENTER")
            _pad(s, 8, 0, 0, 0)
            text(box, value_name, "", 8, variable=True)
        return slider

    def header(parent, name, content):
        _, s = text(parent, name, content, 9, title_c, bold=True)
        _pad(s, 0, 8, 0, 2)

    # ── the frame ──
    frame, _ = b.add(W.Border.static_class(), "MenuFrame")
    _framed_panel(frame, _colour("#1a1a2e", 0.97), 10, panel_art)
    _visibility(frame, "VISIBLE")
    size, _ = b.add(W.SizeBox.static_class(), "MenuSize", frame)
    size.set_width_override(480.0)
    size.set_height_override(470.0)
    column, _ = b.add(W.VerticalBox.static_class(), "MenuColumn", size)

    row, _ = b.add(W.HorizontalBox.static_class(), "MenuHeader", column)
    _, s = text(row, "MenuTitle", "Options", 11, title_c, bold=True)
    _fill(s)
    _valign(s, "CENTER")
    button(row, "CloseButton", "X")

    tabs_row, s = b.add(W.HorizontalBox.static_class(), "TabBar", column)
    _pad(s, 0, 6, 0, 4)
    for name, caption in (("LayoutTabButton", "Layout"), ("ColoursTabButton", "Colours"), ("ChatLogTabButton", "Chat & log"),
                          ("NameplatesTabButton", "Nameplates"), ("ControlsTabButton", "Controls")):
        _, s = button(tabs_row, name, caption, _colour("#c8b48a"))
        _pad(s, 0, 0, 4, 0)

    well, s = b.add(W.Border.static_class(), "TabsWell", column)
    _fill(s)
    _panel(well, _colour("#0c0c18", 0.85), 8)
    switcher, _ = b.add(W.WidgetSwitcher.static_class(), "Tabs", well, True)

    # ── 0 Layout ──
    page, _ = b.add(W.ScrollBox.static_class(), "LayoutTab", switcher)
    col, _ = b.add(W.VerticalBox.static_class(), "LayoutColumn", page)
    check_row(col, "LockCheck", "Lock the HUD (untick, then close this menu, to drag panels and their corner grips)")
    slider_row(col, "UiScaleSlider", "UI scale", "UiScaleText")
    slider_row(col, "OpacitySlider", "Panel opacity", "OpacityText")
    slider_row(col, "BorderSlider", "Border thickness", "BorderText")
    header(col, "ShowHeader", "Show")
    for key, label in (("Vitals", "Vitals (HP, mana)"), ("ActionBar", "Action bar"), ("CastBar", "Cast bar"),
                       ("TargetFrame", "Target frame"), ("Party", "Party"), ("CombatLog", "Combat log"), ("Chat", "Chat")):
        check_row(col, "Show{}Check".format(key), label)
    _, s = button(col, "ResetLayoutButton", "Reset layout")
    _pad(s, 0, 10, 0, 0)
    _halign(s, "LEFT")

    # ── 1 Colours ──
    page, _ = b.add(W.VerticalBox.static_class(), "ColoursTab", switcher)
    editor, s = b.add(W.Border.static_class(), "ColourEditor", page, True)
    _panel(editor, _colour("#22223a", 0.95), 6)
    _pad(s, 0, 0, 0, 6)
    _visibility(editor, "COLLAPSED")
    ecol, _ = b.add(W.VerticalBox.static_class(), "ColourEditorColumn", editor)
    text(ecol, "EditTitle", "Colour", 9, title_c, bold=True, variable=True)
    grid, s = b.add(W.UniformGridPanel.static_class(), "PresetGrid", ecol, True)
    grid.set_slot_padding(unreal.Margin(2.0, 2.0, 2.0, 2.0))
    _pad(s, 0, 4)
    _halign(s, "LEFT")
    slider_row(ecol, "HueSlider", "Hue")
    slider_row(ecol, "SaturationSlider", "Saturation")
    slider_row(ecol, "ValueSlider", "Brightness")
    brow, s = b.add(W.HorizontalBox.static_class(), "ColourEditorButtons", ecol)
    _pad(s, 0, 4, 0, 0)
    box, s = b.add(W.SizeBox.static_class(), "EditSwatchSize", brow)
    box.set_width_override(60.0)
    box.set_height_override(20.0)
    _valign(s, "CENTER")
    swatch, _ = b.add(W.Border.static_class(), "EditSwatch", box, True)
    swatch.set_brush_color(W.LinearColor(1, 1, 1, 1))
    _, s = button(brow, "ColourOkButton", "OK", _colour("#44cc66"))
    _pad(s, 10, 0, 4, 0)
    button(brow, "ColourCancelButton", "Cancel")
    scroll, s = b.add(W.ScrollBox.static_class(), "ColourScroll", page)
    _fill(s)
    b.add(W.VerticalBox.static_class(), "ColourList", scroll, True)
    _, s = button(page, "ResetColoursButton", "Reset colours")
    _pad(s, 0, 6, 0, 0)
    _halign(s, "LEFT")

    # ── 2 Chat & log ──
    page, _ = b.add(W.ScrollBox.static_class(), "ChatLogTab", switcher)
    col, _ = b.add(W.VerticalBox.static_class(), "ChatLogColumn", page)
    slider_row(col, "ChatFontSizeSlider", "Chat font size", "ChatFontSizeText")
    slider_row(col, "ChatLinesSlider", "Chat lines (idle)", "ChatLinesText")
    check_row(col, "TimestampsCheck", "Timestamps ([hh:mm] on new lines)")
    header(col, "LogFilterHeader", "The combat log shows")
    b.add(W.VerticalBox.static_class(), "LogFilterList", col, True)
    _, s = button(col, "ResetChatButton", "Reset chat & log")
    _pad(s, 0, 10, 0, 0)
    _halign(s, "LEFT")

    # ── 3 Nameplates ──
    page, _ = b.add(W.VerticalBox.static_class(), "NameplatesTab", switcher)
    check_row(page, "NpcNameplatesCheck", "NPC nameplates")
    check_row(page, "PlayerNameplatesCheck", "Player nameplates")
    check_row(page, "FloatingTextCheck", "Floating combat text")
    slider_row(page, "NameplateFontSlider", "Nameplate font", "NameplateFontText")
    _, s = button(page, "ResetNameplatesButton", "Reset nameplates")
    _pad(s, 0, 10, 0, 0)
    _halign(s, "LEFT")

    # ── 4 Controls ──
    page, _ = b.add(W.ScrollBox.static_class(), "ControlsTab", switcher)
    text(page, "ControlsText", "", 8, value_c, wrap=True, variable=True)

    _, s = text(column, "MenuHint", "Esc closes this menu. Changes are saved for this character.", 7, label_c)
    _pad(s, 0, 4, 0, 0)

    missing = [n for n in OPTIONS_MENU_PARTS if n not in b.added]
    result = _finish(blueprint, asset_path, b, {"partsMissing": missing, "asset_created": created})
    if wire and asset_path == OPTIONS_MENU_BP and result.get("compiled"):
        result["wired"] = wire_options_menu()
    return result


# ── Panel border thickness (in place; nothing else in the trees changes) ──

def _panel_frame_brush(brush, thickness=PANEL_BORDER_PX):
    """UValhallaGameHUDWidget::SetPanelFrameThickness on a designer brush: the frame art for `thickness`
    (when imported) and a margin of exactly `thickness` of its texels."""
    art = _panel_frame_art(thickness)
    if art is not None:
        brush.set_editor_property("resource_object", art)
    texture = brush.get_editor_property("resource_object")
    sx, sy = _texture_size(texture)
    px = min(float(thickness), min(sx, sy) * 0.45)
    mx, my = px / sx, px / sy
    brush.set_editor_property("draw_as", unreal.SlateBrushDrawType.BOX)
    brush.set_editor_property("margin", unreal.Margin(mx, my, mx, my))
    _set_image_size(brush, sx, sy)
    return brush


def _is_panel_frame(widget):
    if not isinstance(widget, unreal.Border):
        return False
    resource = widget.get_editor_property("background").get_editor_property("resource_object")
    name = resource.get_name() if resource is not None else ""
    return name == "T_UI_Panel" or name.startswith("T_UI_PanelFrame_")


def _add_border_slider(blueprint):
    """WBP_OptionsMenu made before the border setting: BorderSlider / BorderText under the opacity row."""
    widgets = {str(_field(e, "widget_name", "") or _field(e, "widget").get_name()): _field(e, "widget")
               for e in _widget_infos(blueprint)}
    if "BorderSlider" in widgets:
        return []
    opacity_row = widgets.get("OpacitySliderRow")
    column = widgets.get("LayoutColumn")
    if opacity_row is None or column is None:
        raise RuntimeError("WBP_OptionsMenu has no OpacitySliderRow in LayoutColumn; re-run layout_options_menu(replace=True)")
    W = unreal
    b = _TreeBuilder(blueprint)
    index = column.get_child_index(opacity_row) + 1
    info = _umg_call("AddWidget", blueprint, W.HorizontalBox.static_class(), "BorderSliderRow", column, index)
    row = _field(info, "widget")
    if row is None or row.get_name() != "BorderSliderRow":
        raise RuntimeError("could not add BorderSliderRow")
    _pad(_field(info, "slot"), 0, 3)
    box, s = b.add(W.SizeBox.static_class(), "BorderSliderLabelSize", row)
    box.set_width_override(120.0)
    _valign(s, "CENTER")
    label, _ = b.add(W.TextBlock.static_class(), "BorderSliderLabel", box)
    _text(label, "Border thickness", 8, _colour("#ddd6c4"))
    slider, s = b.add(W.Slider.static_class(), "BorderSlider", row, True)
    _not_focusable(slider)
    _fill(s)
    _valign(s, "CENTER")
    box, s = b.add(W.SizeBox.static_class(), "BorderTextSize", row)
    box.set_width_override(80.0)
    _valign(s, "CENTER")
    _pad(s, 8, 0, 0, 0)
    value, _ = b.add(W.TextBlock.static_class(), "BorderText", box, True)
    _text(value, "", 8, _colour("#ddd6c4"))
    return ["BorderSliderRow"] + b.added


def fix_panel_borders(asset_paths=(GAME_HUD_BP, OPTIONS_MENU_BP), thickness=PANEL_BORDER_PX):
    """Give every T_UI_Panel Border in these trees the runtime's brush (margin 24/256, drawn `thickness` px),
    add the options menu's border slider when it is missing, compile and save. Nothing else changes."""
    results = []
    for asset_path in asset_paths:
        blueprint = unreal.load_asset(asset_path)
        if blueprint is None:
            results.append({"asset": asset_path, "missing": True})
            continue
        fixed = []
        for entry in _widget_infos(blueprint):
            widget = _field(entry, "widget")
            if not _is_panel_frame(widget):
                continue
            brush = widget.get_editor_property("background")
            before = "{} margin {:.3f}".format(brush.get_editor_property("resource_object").get_name(), float(brush.get_editor_property("margin").left))
            brush = _panel_frame_brush(brush, thickness)
            widget.set_brush(brush)
            after = "{} margin {:.4f}".format(brush.get_editor_property("resource_object").get_name(), float(brush.get_editor_property("margin").left))
            fixed.append("{}: {} -> {}".format(widget.get_name(), before, after))
        added = _add_border_slider(blueprint) if asset_path == OPTIONS_MENU_BP else []
        builder = _TreeBuilder(blueprint)
        builder.added = added
        results.append(_finish(blueprint, asset_path, builder, {"framesFixed": fixed}))
    return results
