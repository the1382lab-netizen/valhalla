"""HUD Widget Blueprint tools for the Valhalla 2.0 port (B-07), exposed to Unreal MCP.

B-07 moves the code-built game HUD (``UValhallaGameHUDWidget``) to Widget
Blueprints. Step 2 made the C++ classes inheritable; these tools make and
inspect the Blueprints step 3 lays out. The work is in
``valhalla_tools/hud_blueprints.py``; see its docstring for how the designer
tree is built from Python (the engine's ``UMGToolSet``, no C++ editor module).

Every tool returns a JSON *string*, matching data_tools.py.
"""

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
        cannot). Then points WBP_GameHUD's SlotWidgetClass and BarWidgetClass
        at the other two, compiles and saves. Does not change the project
        setting.

        Args:
            overwrite: Delete and recreate Blueprints that already exist.
                Their designer layout is lost.

        Returns:
            A JSON object as text: ``ok``, ``assets`` (per asset: ``existed`` /
            ``created`` / ``deleted``, ``widgets``) and ``wired``.
        """
        from valhalla_tools import hud_blueprints

        try:
            return _ok(**hud_blueprints.create_blueprints(bool(overwrite)))
        except Exception as exc:  # noqa: BLE001 - reported, never raised at MCP
            return _fail(str(exc), traceback=traceback.format_exc().splitlines()[-12:])

    @toolset_registry.tool_call
    @staticmethod
    def layout_hud_from_config(asset_path: str = "/Game/Valhalla/UI/HUD/WBP_GameHUD", replace: bool = False) -> str:
        """Build WBP_GameHUD's designer tree from ui-config.json, named for the C++ bindings.

        A Canvas Panel root with every panel the code-built HUD has, placed
        where ``ui-config.json`` places it, each widget named after its
        ``UValhallaGameHUDWidget`` member (VitalsPanel, HpBar, ManaBar,
        ActionBarRow, ChatPanel, ChatScroll, ChatInput required; the rest
        optional) and marked as a variable. Containers C++ fills (ActionBarRow,
        InventoryGrid, EquipmentPanel, PartyList, LootGrid, TargetBuffs,
        SkillsList, the scroll boxes) are left empty. Buttons are
        UValhallaHUDButton with their Action set. Bars are WBP_HUDBar when it
        exists. Compiles and saves.

        Refuses a Blueprint that already has widgets unless ``replace``.

        Args:
            asset_path: The HUD Widget Blueprint.
            replace: Remove the existing designer tree first (its edits are lost).

        Returns:
            A JSON object as text: ``ok``, ``widgets`` (count added),
            ``panelsMissing`` (should be empty), ``buttonsWithoutAction``,
            ``renamed``, ``compiled``; or ``refused``.
        """
        from valhalla_tools import hud_blueprints

        try:
            result = hud_blueprints.layout_game_hud(asset_path, bool(replace))
        except Exception as exc:  # noqa: BLE001
            return _fail(str(exc), traceback=traceback.format_exc().splitlines()[-12:])
        if "refused" in result:
            return _fail(result["refused"], refused=True)
        return _ok(**result)

    @toolset_registry.tool_call
    @staticmethod
    def describe_hud_blueprint(asset_path: str) -> str:
        """List a Widget Blueprint's designer widgets and, for a HUD, the panels it binds.

        Args:
            asset_path: e.g. ``/Game/Valhalla/UI/HUD/WBP_GameHUD``.

        Returns:
            A JSON object as text: ``ok``, ``parentClass``, ``widgetCount``,
            ``widgets`` (name, class, parent, isVariable) and, for a child of
            UValhallaGameHUDWidget, ``requiredMissing``, ``optionalPresent``,
            ``optionalMissing`` and ``laysOutFromBlueprint`` (the tree is
            non-empty and every required panel is there; the root must also
            be a Canvas Panel).
        """
        from valhalla_tools import hud_blueprints

        try:
            return _ok(**hud_blueprints.describe(asset_path))
        except Exception as exc:  # noqa: BLE001
            return _fail(str(exc), traceback=traceback.format_exc().splitlines()[-12:])
