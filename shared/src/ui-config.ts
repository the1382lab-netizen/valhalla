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
      colors: { high: string; mid: string; low: string; bg: string; bgAlpha: number };
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

export const DEFAULT_UI_CONFIG: UIConfig = {
  version: '1.0.0',
  hud: {
    hpBar: {
      x: 16,
      width: 200,
      height: 12,
      yOffsetFromBottom: 36,
      colors: { high: '#44ff44', mid: '#ffaa00', low: '#ff4444', bg: '#000000', bgAlpha: 0.7 },
    },
    manaBar: {
      width: 200,
      height: 12,
      color: '#4488ff',
      bgColor: '#000000',
      bgAlpha: 0.7,
      gapAboveHp: 6,
    },
    energyBar: {
      width: 200,
      height: 12,
      color: '#ddaa00',
      bgColor: '#000000',
      bgAlpha: 0.7,
    },
    classText: {
      fontSize: '12px',
      fontFamily: 'monospace',
      color: '#ffffff',
      strokeColor: '#000000',
      strokeThickness: 2,
    },
  },
  actionBar: {
    slotSize: 44,
    slotGap: 4,
    padding: 6,
    bottomMargin: 8,
    colors: {
      bg: '#1a1a1a',
      bgAlpha: 0.95,
      border: '#666666',
      cooldownOverlay: '#880000',
      cooldownOverlayAlpha: 0.6,
      keyLabelColor: '#888888',
    },
  },
  chat: {
    maxWidth: 360,
    height: 170,
    bottomMargin: 8,
    lineHeight: 15,
    padding: 6,
    inputHeight: 18,
    maxMessages: 50,
    visibleLines: 9,
    fontSize: '11px',
    bgColor: '#0a0a1a',
    bgAlpha: 0.72,
    borderColor: '#333355',
    borderAlpha: 0.9,
    colors: {
      general: '#ffffff',
      world: '#ffdd00',
      whisper: '#ff88cc',
      system: '#ffaa44',
    },
  },
  inventory: {
    cols: 8,
    rows: 4,
    slotSize: 48,
    slotGap: 4,
    charPanelWidth: 220,
    panelGap: 8,
    panelHeight: 340,
    colors: {
      bg: '#1a1a2e',
      bgAlpha: 0.95,
      border: '#333355',
      slotBg: '#2a2a3e',
      highlight: '#ffaa00',
      titleColor: '#ffcc00',
      labelColor: '#aaaacc',
      valueColor: '#ffffff',
    },
  },
  castBar: {
    width: 260,
    height: 16,
    yAboveActionBar: 12,
    color: '#ffaa00',
    bgColor: '#000000',
    bgAlpha: 0.6,
    textColor: '#ffffff',
    fontSize: '11px',
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
  deathOverlay: {
    bgAlpha: 0.7,
    textColor: '#ff4444',
    fontSize: '32px',
  },
};
