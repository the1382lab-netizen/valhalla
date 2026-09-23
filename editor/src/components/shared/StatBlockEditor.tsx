import React from 'react';

export interface StatBlock {
  hp: number;
  mana: number;
  strength: number;
  stamina: number;
  dexterity: number;
  intelligence: number;
  wisdom: number;
  physicalResist: number;
  spellResist: number;
  critChance: number;
  critDamage: number;
  physicalDefense: number;
  blockRating: number;
  dodgeRating: number;
}

interface StatBlockEditorProps {
  value: StatBlock;
  onChange: (stats: StatBlock) => void;
}

const STAT_LABELS: Record<keyof StatBlock, string> = {
  hp: 'HP',
  mana: 'Mana',
  strength: 'Strength',
  stamina: 'Stamina',
  dexterity: 'Dexterity',
  intelligence: 'Intelligence',
  wisdom: 'Wisdom',
  physicalResist: 'Phys Resist',
  spellResist: 'Spell Resist',
  critChance: 'Crit Chance %',
  critDamage: 'Crit Damage %',
  physicalDefense: 'Phys Defense',
  blockRating: 'Block Rating',
  dodgeRating: 'Dodge Rating',
};

const STAT_ORDER: (keyof StatBlock)[] = [
  'hp',
  'mana',
  'strength',
  'stamina',
  'dexterity',
  'intelligence',
  'wisdom',
  'physicalDefense',
  'physicalResist',
  'spellResist',
  'critChance',
  'critDamage',
  'blockRating',
  'dodgeRating',
];

export const StatBlockEditor: React.FC<StatBlockEditorProps> = ({ value, onChange }) => {
  const handleStatChange = (key: keyof StatBlock, newValue: number) => {
    onChange({
      ...value,
      [key]: newValue,
    });
  };

  return (
    <div className="stat-grid">
      {STAT_ORDER.map((statKey) => (
        <div key={statKey} className="stat-row">
          <label className="stat-label">{STAT_LABELS[statKey]}</label>
          <div className="stat-value">
            <input
              type="number"
              value={value[statKey]}
              onChange={(e) => handleStatChange(statKey, parseInt(e.target.value, 10) || 0)}
            />
          </div>
        </div>
      ))}
    </div>
  );
};
