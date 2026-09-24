/**
 * UI Configuration schema and defaults (shared/data/ui-config.json).
 *
 * B-07 step 4: the game HUD's layout — every panel's position and size, the
 * bars, the action bar, the cast bar, the chat box, the inventory panels, the
 * death overlay and their colours — lives in the WBP_GameHUD Widget Blueprint
 * in Unreal (Content/Valhalla/UI/HUD), edited in the Widget Designer. This file
 * keeps only what the C++ HUD still builds or counts at runtime: the inventory
 * grid's size, the chat's line counts, and the nameplates (the nameplate and
 * floating-text layer is still built in C++). Edited on the editor's UI Layout
 * page; `valhalla.ReloadUI` (or saving the file) applies it to a running game.
 *
 * Mirrors FValhallaUIConfig (Valhalla2/Source/ValhallaCore/Public/ValhallaUIConfig.h).
 */

export interface UIConfig {
  version: string;
  chat: {
    /** Lines listed while the chat box is open (the client keeps at most 50). */
    maxMessages: number;
    /** Lines shown while the chat is idle; each fades 10 s after it arrived. */
    visibleLines: number;
  };
  inventory: {
    /** The inventory grid: cols x rows cells. */
    cols: number;
    rows: number;
  };
  nameplates: {
    fontSize: string;
    fontWeight: string;
    color: string;
    strokeColor: string;
    strokeThickness: number;
    bgColor: string;
    bgAlpha: number;
    yOffset: number;
    bgPaddingX: number;
    bgPaddingY: number;
    bgRadius: number;
  };
}

export const DEFAULT_UI_CONFIG: UIConfig = {
  version: '1.1.0',
  chat: {
    maxMessages: 50,
    visibleLines: 9,
  },
  inventory: {
    cols: 8,
    rows: 4,
  },
  nameplates: {
    fontSize: '12px',
    fontWeight: 'bold',
    color: '#ffffff',
    strokeColor: '#000000',
    strokeThickness: 3,
    bgColor: '#000000',
    bgAlpha: 0.5,
    yOffset: -44,
    bgPaddingX: 4,
    bgPaddingY: 2,
    bgRadius: 3,
  },
};
