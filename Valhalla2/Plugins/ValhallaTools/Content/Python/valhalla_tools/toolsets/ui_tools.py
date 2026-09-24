"""HUD Widget Blueprint tools for the Valhalla 2.0 port (B-07), exposed to Unreal MCP.

B-07 moves the code-built game HUD (``UValhallaGameHUDWidget``) to Widget
Blueprints. Step 2 made the C++ classes inheritable; these tools make and
inspect the Blueprints step 3 lays out. The work is in
``valhalla_tools/hud_blueprints.py``; see its docstring for how the designer
tree is built from Python (the engine's ``UMGToolSet``, no C++ editor module).

Every tool returns a JSON *string*, matching data_tools.py. Each call reloads
``hud_blueprints`` first, so an edit to it takes effect without an editor
restart (this file itself is only read at startup).
"""

import importlib
import json
import traceback

import unreal
import toolset_registry


def _ok(**kwargs) -> str:
    payload = {"ok": True}
    payload.update(kwargs)
    return json.dumps(payload, indent=2)


def _fail(message: str, **kwargs) -> str:
    payload = {"ok": False, "error": message}
    payload.update(kwargs)
    return json.dumps(payload, indent=2)


def _hud_blueprints():
    """valhalla_tools.hud_blueprints, freshly reloaded."""
    from valhalla_tools import hud_blueprints

    return importlib.reload(hud_blueprints)


@unreal.uclass()
class ValhallaUITools(unreal.ToolsetDefinition):
    """Create, lay out and inspect the game HUD's Widget Blueprints (B-07).

    ``WBP_HUDSlot`` / ``WBP_HUDBar`` / ``WBP_GameHUD`` in
    ``/Game/Valhalla/UI/HUD``, parented to ``UValhallaHUDSlotWidget``,
    ``UValhallaHUDBarWidget`` and ``UValhallaGameHUDWidget``. An empty one
    behaves exactly like its C++ class. The game uses WBP_GameHUD only once it
    is set as Project Settings > Valhalla > UI > Game HUD Class (or
    ``valhalla.HudClass <path>`` for one session).
    """

    @toolset_registry.tool_call
    @staticmethod
    def create_hud_blueprints(overwrite: bool = False) -> str:
        """Create WBP_HUDSlot, WBP_HUDBar and WBP_GameHUD with empty designer trees.

        Checks before creating (project rule: an "Overwrite?" modal freezes the
        editor): an existing asset is left alone and reported as ``existed``
        unless ``overwrite`` is true, which deletes it first (and fails if it
        cannot). Saves each. Does not change the project setting, and does
        not make WBP_HUDSlot / WBP_HUDBar the classes C++ builds its cells and
        bars from (``hud_blueprints.wire_cell_classes``; see that module for
        why). An empty WBP_GameHUD does not compile until it is laid out.

        Args:
            overwrite: Delete and recreate Blueprints that already exist.
                Their designer layout is lost.

        Returns:
            A JSON object as text: ``ok``, ``assets`` (per asset: ``existed`` /
            ``created`` / ``deleted``, ``widgets``) and ``wired``.
        """
        try:
            return _ok(**_hud_blueprints().create_blueprints(bool(overwrite)))
        except Exception as exc:  # noqa: BLE001 - reported, never raised at MCP
            return _fail(str(exc), traceback=traceback.format_exc().splitlines()[-12:])

    @toolset_registry.tool_call
    @staticmethod
    def layout_hud_from_config(asset_path: str = "/Game/Valhalla/UI/HUD/WBP_GameHUD", replace: bool = False) -> str:
        """Build a HUD Widget Blueprint's designer tree from ui-config.json, named for the C++ bindings.

        Which tree depends on the Blueprint's parent class:

        * WBP_GameHUD (UValhallaGameHUDWidget): a Canvas Panel root with every
          panel the code-built HUD has, at the code build's anchors and
          offsets, each widget named after its member (VitalsPanel, HpBar,
          ManaBar, ActionBarRow, ChatPanel, ChatScroll, ChatInput required;
          the rest optional) and marked as a variable. Containers C++ fills
          (ActionBarRow, InventoryGrid, EquipmentPanel, PartyList, LootGrid,
          TargetBuffs, SkillsList, the scroll boxes) are left empty. Buttons
          are UValhallaHUDButton with their Action set. Bars are WBP_HUDBar
          (lay it out first), each in a Size Box giving the code bar's size.
          The B-15 frame art (T_UI_Panel / _Button / _BarFrame) is used when
          imported, as the code build does.
        * WBP_HUDBar (UValhallaHUDBarWidget): Frame > Background > Sizer >
          FillSizer/Fill, OverlaySizer/OverlayFill, Label.
        * WBP_HUDSlot (UValhallaHUDSlotWidget): Sizer > Frame > Fill > Stack
          of Icon, SkillTile, Abbrev, CooldownSizer/CooldownFill,
          CooldownText, KeyLabel, QuantityText.

        Compiles and saves. Refuses a Blueprint that already has widgets
        unless ``replace``.

        Args:
            asset_path: The Widget Blueprint (WBP_GameHUD, WBP_HUDBar or WBP_HUDSlot).
            replace: Remove the existing designer tree first (its edits are lost).

        Returns:
            A JSON object as text: ``ok``, ``widgets`` (count added),
            ``compiled`` (and ``compileError``), ``saved``, ``renamed``,
            ``warnings``; for the HUD ``panelsMissing`` (should be empty) and
            ``buttonsWithoutAction``; for a bar or cell ``parts``; or ``refused``.
        """
        try:
            result = _hud_blueprints().layout_blueprint(asset_path, bool(replace))
        except Exception as exc:  # noqa: BLE001
            return _fail(str(exc), traceback=traceback.format_exc().splitlines()[-12:])
        if "refused" in result:
            return _fail(result["refused"], refused=True)
        return _ok(**result)

    @toolset_registry.tool_call
    @staticmethod
    def describe_hud_blueprint(asset_path: str) -> str:
        """List a Widget Blueprint's designer widgets and, for a HUD / bar / cell, the parts it binds.

        Args:
            asset_path: e.g. ``/Game/Valhalla/UI/HUD/WBP_GameHUD``.

        Returns:
            A JSON object as text: ``ok``, ``parentClass``, ``widgetCount``,
            ``widgets`` (name, class, parent, isVariable) and, for a child of
            UValhallaGameHUDWidget, ``requiredMissing``, ``optionalPresent``,
            ``optionalMissing`` and ``laysOutFromBlueprint`` (the tree is
            non-empty and every required panel is there; the root must also
            be a Canvas Panel) and the cell / bar classes; for a bar or cell
            ``partsPresent`` / ``partsMissing``.
        """
        try:
            return _ok(**_hud_blueprints().describe(asset_path))
        except Exception as exc:  # noqa: BLE001
            return _fail(str(exc), traceback=traceback.format_exc().splitlines()[-12:])
