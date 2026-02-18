/**
 * UI Configuration schema and defaults.
 * Used by the game editor to visually tweak HUD layout,
 * and by the game client to load configurable UI parameters.
 */
export interface UIConfig {
    version: string;
    hud: {
        hpBar: {
            x: number;
            width: number;
            height: number;
            yOffsetFromBottom: number;
            colors: {
                high: string;
                mid: string;
                low: string;
                bg: string;
                bgAlpha: number;
            };
        };
        manaBar: {
            width: number;
            height: number;
            color: string;
            bgColor: string;
            bgAlpha: number;
            gapAboveHp: number;
        };
        energyBar: {
            width: number;
            height: number;
            color: string;
            bgColor: string;
            bgAlpha: number;
        };
        classText: {
            fontSize: string;
            fontFamily: string;
            color: string;
            strokeColor: string;
            strokeThickness: number;
        };
    };
    actionBar: {
        slotSize: number;
        slotGap: number;
        padding: number;
        bottomMargin: number;
        colors: {
            bg: string;
            bgAlpha: number;
            border: string;
            cooldownOverlay: string;
            cooldownOverlayAlpha: number;
            keyLabelColor: string;
        };
    };
    chat: {
        maxWidth: number;
        height: number;
        bottomMargin: number;
        lineHeight: number;
        padding: number;
        inputHeight: number;
        maxMessages: number;
        visibleLines: number;
        fontSize: string;
        bgColor: string;
        bgAlpha: number;
        borderColor: string;
        borderAlpha: number;
        colors: {
            general: string;
            world: string;
            whisper: string;
            system: string;
        };
    };
    inventory: {
        cols: number;
        rows: number;
        slotSize: number;
        slotGap: number;
        charPanelWidth: number;
        panelGap: number;
        panelHeight: number;
        colors: {
            bg: string;
            bgAlpha: number;
            border: string;
            slotBg: string;
            highlight: string;
            titleColor: string;
            labelColor: string;
            valueColor: string;
        };
    };
    castBar: {
        width: number;
        height: number;
        yAboveActionBar: number;
        color: string;
        bgColor: string;
        bgAlpha: number;
        textColor: string;
        fontSize: string;
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
    deathOverlay: {
        bgAlpha: number;
        textColor: string;
        fontSize: string;
    };
}
export declare const DEFAULT_UI_CONFIG: UIConfig;
//# sourceMappingURL=ui-config.d.ts.map