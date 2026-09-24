/**
 * B-12: the Live Dashboard's Accounts dialog. Look an account up by account or
 * character name, see its characters and ban status, and reset its password,
 * ban or unban it, rename a character, or delete it.
 *
 * Talks to the editor server's /api/accounts/* routes, which go straight to
 * the account backend with the server secret, so it works whether or not the
 * game server is running. When the game server is up, a ban, reset, rename or
 * deletion also disconnects that account's live sessions.
 */
import React, { useCallback, useEffect, useState } from 'react';
import { ADMIN_RESULT_EVENT, AdminResult } from './adminApi';
import { BAN_DURATIONS, badgeStyle, overlayStyle, panelStyle, titleStyle } from './AdminPlayerTools';

interface BanStatus {
  banned: boolean;
  until: number | null;
  permanent: boolean;
  reason: string;
  bannedBy: string;
}

interface AccountSummary {
  userId: number;
  username: string;
  createdAt: number;
  lastLoginAt: number | null;
  characterCount: number;
  ban: BanStatus;
}

interface AccountCharacter {
  id: number;
  name: string;
  classId: string;
  level: number;
  zoneId: string;
  updatedAt: number;
}

interface AccountDetail extends AccountSummary {
  characters: AccountCharacter[];
}

async function accountsGet(path: string): Promise<any> {
  try {
    const res = await fetch(path);
    const json = await res.json().catch(() => ({}));
    return res.ok ? { ok: true, ...json } : { ...json, ok: false, error: json?.error || `HTTP ${res.status}` };
  } catch (err: any) {
    return { ok: false, error: err?.message || 'request failed' };
  }
}

/** POST an account action and announce the result on the dashboard, like postAdmin. */
async function accountsPost(action: string, body: Record<string, unknown>, label: string): Promise<any> {
  let result: any;
  try {
    const res = await fetch(`/api/accounts/${action}`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });
    const json = await res.json().catch(() => ({}));
    result = res.ok ? { ok: true, ...json } : { ...json, ok: false, error: json?.error || `HTTP ${res.status}` };
  } catch (err: any) {
    result = { ok: false, error: err?.message || 'request failed' };
  }
  const detail: AdminResult = {
    ok: !!result.ok,
    action: label,
    message: result.ok ? [result.message || 'done', result.kick].filter(Boolean).join(' · ') : result.error,
  };
  window.dispatchEvent(new CustomEvent<AdminResult>(ADMIN_RESULT_EVENT, { detail }));
  return result;
}

function when(ms: number | null | undefined): string {
  return ms ? new Date(ms).toLocaleString() : '—';
}

function banLabel(ban: BanStatus): string {
  if (!ban.banned) return '';
  return ban.permanent || !ban.until ? 'BANNED' : `SUSPENDED until ${new Date(ban.until).toLocaleString()}`;
}

const rowStyle: React.CSSProperties = { borderTop: '1px solid var(--border-color)' };
const cell: React.CSSProperties = { padding: 4 };
const small: React.CSSProperties = { padding: '2px 8px', fontSize: 11 };
const muted: React.CSSProperties = { fontSize: 12, color: 'var(--text-muted)' };

export const AccountsDialog: React.FC<{ onClose: () => void }> = ({ onClose }) => {
  const [query, setQuery] = useState('');
  const [bannedOnly, setBannedOnly] = useState(false);
  const [results, setResults] = useState<AccountSummary[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [selected, setSelected] = useState<AccountDetail | null>(null);

  const search = useCallback(async (q: string, onlyBanned: boolean) => {
    const res = await accountsGet(`/api/accounts/search?q=${encodeURIComponent(q)}${onlyBanned ? '&banned=1' : ''}`);
    if (res.ok) { setResults(res.accounts ?? []); setError(null); } else { setError(res.error); }
  }, []);

  const open = useCallback(async (userId: number) => {
    const res = await accountsGet(`/api/accounts/detail?userId=${userId}`);
    if (res.ok) { setSelected(res.account); setError(null); } else { setError(res.error); }
  }, []);

  useEffect(() => { search('', false); }, [search]);

  const refreshAll = async () => {
    await search(query, bannedOnly);
    if (selected) await open(selected.userId);
  };

  return (
    <div style={overlayStyle} onClick={onClose}>
      <div style={{ ...panelStyle, width: 720, maxHeight: '85vh', overflow: 'auto' }} onClick={e => e.stopPropagation()}
        onKeyDown={e => { if (e.key === 'Escape') onClose(); }}>
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
          <h3 style={titleStyle}>Accounts</h3>
          <button className="btn btn-ghost" style={small} onClick={refreshAll} title="Refresh">⟳</button>
        </div>

        <div style={{ display: 'flex', gap: 6, alignItems: 'center' }}>
          <input className="form-input" autoFocus placeholder="account or character name (empty = newest accounts)"
            value={query} onChange={e => setQuery(e.target.value)} style={{ flex: 1 }}
            onKeyDown={e => { if (e.key === 'Enter') search(query, bannedOnly); }} />
          <button className="btn btn-primary" style={small} onClick={() => search(query, bannedOnly)}>Search</button>
          <label style={{ fontSize: 11, color: 'var(--text-secondary)', display: 'flex', gap: 4, alignItems: 'center' }}>
            <input type="checkbox" checked={bannedOnly} onChange={e => { setBannedOnly(e.target.checked); search(query, e.target.checked); }} />
            banned only
          </label>
        </div>

        {error && <div style={{ color: '#e66', fontSize: 12 }}>{error}</div>}
        {results === null && !error && <div style={muted}>Loading…</div>}
        {results !== null && results.length === 0 && <div style={muted}>No accounts found.</div>}
        {results !== null && results.length > 0 && (
          <table style={{ width: '100%', fontSize: 12, borderCollapse: 'collapse' }}>
            <thead>
              <tr style={{ color: 'var(--text-muted)', textAlign: 'left' }}>
                <th style={cell}>Account</th><th style={cell}>Characters</th><th style={cell}>Created</th><th style={cell}>Last login</th><th style={cell}>Status</th>
              </tr>
            </thead>
            <tbody>
              {results.map(a => (
                <tr key={a.userId} style={{ ...rowStyle, cursor: 'pointer', background: selected?.userId === a.userId ? 'var(--bg-tertiary)' : undefined }}
                  onClick={() => open(a.userId)}>
                  <td style={{ ...cell, color: 'var(--text-primary)' }}>{a.username}</td>
                  <td style={cell}>{a.characterCount}</td>
                  <td style={cell}>{when(a.createdAt)}</td>
                  <td style={cell}>{when(a.lastLoginAt)}</td>
                  <td style={cell}>{a.ban.banned && <span style={badgeStyle('#e66')}>{banLabel(a.ban)}</span>}</td>
                </tr>
              ))}
            </tbody>
          </table>
        )}

        {selected && (
          <AccountPanel key={selected.userId} account={selected}
            onChanged={async () => { await open(selected.userId); await search(query, bannedOnly); }}
            onDeleted={async () => { setSelected(null); await search(query, bannedOnly); }} />
        )}

        <div style={{ display: 'flex', justifyContent: 'flex-end' }}>
          <button className="btn btn-ghost" onClick={onClose}>Close</button>
        </div>
      </div>
    </div>
  );
};

const AccountPanel: React.FC<{ account: AccountDetail; onChanged: () => void; onDeleted: () => void }> = ({ account, onChanged, onDeleted }) => {
  const [tempPassword, setTempPassword] = useState<string | null>(null);
  const [minutes, setMinutes] = useState(BAN_DURATIONS[1].value);
  const [reason, setReason] = useState('');
  const [renaming, setRenaming] = useState<number | null>(null);
  const [newName, setNewName] = useState('');
  const [confirmDelete, setConfirmDelete] = useState('');
  const [busy, setBusy] = useState(false);

  const run = async (fn: () => Promise<any>) => {
    setBusy(true);
    try { return await fn(); } finally { setBusy(false); }
  };

  const resetPassword = () => run(async () => {
    if (!window.confirm(`Reset the password of "${account.username}"? They will be logged out everywhere.`)) return;
    const res = await accountsPost('reset-password', { userId: account.userId }, `reset password ${account.username}`);
    if (res.ok) setTempPassword(res.temporaryPassword);
  });

  const ban = () => run(async () => {
    const res = await accountsPost('ban', { userId: account.userId, minutes: Number(minutes), reason }, `ban ${account.username}`);
    if (res.ok) { setReason(''); onChanged(); }
  });

  const unban = () => run(async () => {
    const res = await accountsPost('unban', { userId: account.userId }, `unban ${account.username}`);
    if (res.ok) onChanged();
  });

  const rename = (c: AccountCharacter) => run(async () => {
    const res = await accountsPost('rename-character', { characterId: c.id, name: newName }, `rename ${c.name}`);
    if (res.ok) { setRenaming(null); setNewName(''); onChanged(); }
  });

  const remove = () => run(async () => {
    const res = await accountsPost('delete', { userId: account.userId, confirm: confirmDelete }, `delete account ${account.username}`);
    if (res.ok) onDeleted();
  });

  return (
    <div style={{ borderTop: '1px solid var(--border-color)', paddingTop: 10, display: 'flex', flexDirection: 'column', gap: 10 }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'baseline' }}>
        <div style={{ fontSize: 13, color: 'var(--text-primary)', fontWeight: 600 }}>
          {account.username} <span style={{ ...muted, fontWeight: 400 }}>#{account.userId}</span>
        </div>
        <div style={muted}>created {when(account.createdAt)} · last login {when(account.lastLoginAt)}</div>
      </div>
      {account.ban.banned && (
        <div style={{ fontSize: 12, color: '#e66' }}>
          {banLabel(account.ban)}{account.ban.reason ? ` — ${account.ban.reason}` : ''}{account.ban.bannedBy ? ` (by ${account.ban.bannedBy})` : ''}
        </div>
      )}

      {/* Characters */}
      {account.characters.length === 0 ? <div style={muted}>No characters.</div> : (
        <table style={{ width: '100%', fontSize: 12, borderCollapse: 'collapse' }}>
          <thead>
            <tr style={{ color: 'var(--text-muted)', textAlign: 'left' }}>
              <th style={cell}>Character</th><th style={cell}>Class</th><th style={cell}>Level</th><th style={cell}>Zone</th><th style={cell}>Last saved</th><th />
            </tr>
          </thead>
          <tbody>
            {account.characters.map(c => (
              <tr key={c.id} style={rowStyle}>
                <td style={{ ...cell, color: 'var(--text-primary)' }}>
                  {renaming === c.id ? (
                    <input className="form-input" autoFocus value={newName} maxLength={20} style={{ padding: '2px 6px', fontSize: 12 }}
                      onChange={e => setNewName(e.target.value)}
                      onKeyDown={e => { if (e.key === 'Enter') rename(c); if (e.key === 'Escape') setRenaming(null); }} />
                  ) : c.name}
                </td>
                <td style={cell}>{c.classId}</td>
                <td style={cell}>{c.level}</td>
                <td style={cell}>{c.zoneId}</td>
                <td style={cell}>{when(c.updatedAt)}</td>
                <td style={{ ...cell, textAlign: 'right', whiteSpace: 'nowrap' }}>
                  {renaming === c.id ? (
                    <>
                      <button className="btn btn-primary" style={small} disabled={busy || !newName.trim()} onClick={() => rename(c)}>Save</button>{' '}
                      <button className="btn btn-ghost" style={small} onClick={() => setRenaming(null)}>Cancel</button>
                    </>
                  ) : (
                    <button className="btn btn-ghost" style={small} onClick={() => { setRenaming(c.id); setNewName(c.name); }}>Rename</button>
                  )}
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      )}

      {/* Password */}
      <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
        <button className="btn btn-ghost" style={{ ...small, whiteSpace: 'nowrap' }} disabled={busy} onClick={resetPassword}>Reset password…</button>
        {tempPassword && (
          <span style={{ fontSize: 12 }}>
            Temporary password: <code style={{ userSelect: 'all', color: 'var(--text-primary)' }}>{tempPassword}</code>{' '}
            <button className="btn btn-ghost" style={small} onClick={() => navigator.clipboard?.writeText(tempPassword)}>Copy</button>
            <span style={muted}> — shown once; give it to the player, who should change it after logging in.</span>
          </span>
        )}
      </div>

      {/* Ban */}
      <div style={{ display: 'flex', gap: 6, alignItems: 'center' }}>
        {account.ban.banned ? (
          <button className="btn btn-ghost" style={small} disabled={busy} onClick={unban}>Unban</button>
        ) : (
          <>
            <select className="form-input" value={minutes} onChange={e => setMinutes(e.target.value)} style={{ fontSize: 12, width: 'auto', flex: '0 0 auto' }}>
              {BAN_DURATIONS.map(d => <option key={d.value} value={d.value}>{d.label}</option>)}
            </select>
            <input className="form-input" placeholder="reason (shown to the player)" value={reason} maxLength={200}
              onChange={e => setReason(e.target.value)} style={{ flex: 1, minWidth: 0, fontSize: 12 }} />
            <button className="btn btn-danger" style={{ ...small, whiteSpace: 'nowrap' }} disabled={busy} onClick={ban}>Ban</button>
          </>
        )}
      </div>

      {/* Delete */}
      <div style={{ display: 'flex', gap: 6, alignItems: 'center' }}>
        <input className="form-input" placeholder={`type "${account.username}" to delete this account`} value={confirmDelete}
          onChange={e => setConfirmDelete(e.target.value)} style={{ flex: 1, fontSize: 12 }} />
        <button className="btn btn-danger" style={{ ...small, whiteSpace: 'nowrap' }}
          disabled={busy || confirmDelete.trim().toLowerCase() !== account.username}
          onClick={remove}>
          Delete account
        </button>
      </div>
      <div style={muted}>Deleting removes the account and all its characters, items and action bars. It can't be undone.</div>
    </div>
  );
};
