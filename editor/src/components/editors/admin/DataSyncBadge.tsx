/**
 * B-13: "did my reload actually apply?" Compares the SHA-1 of each data file
 * the running game server loaded (its admin state) with the file on disk now,
 * via the editor server's /api/data-sync. Shown next to "Reload data".
 */
import React, { useCallback, useEffect, useState } from 'react';

interface SyncFile { name: string; disk: string | null; live: string | null; match: boolean }
interface SyncState {
  online: boolean;
  supported: boolean;
  loadedAt?: string | null;
  allMatch?: boolean;
  files?: SyncFile[];
}

export const DataSyncBadge: React.FC<{ refreshKey?: unknown }> = ({ refreshKey }) => {
  const [sync, setSync] = useState<SyncState | null>(null);

  const refresh = useCallback(async () => {
    try {
      const res = await fetch('/api/data-sync');
      setSync(await res.json());
    } catch {
      setSync(null);
    }
  }, []);

  useEffect(() => {
    refresh();
    const timer = setInterval(refresh, 5000);
    return () => clearInterval(timer);
  }, [refresh]);

  // A reload just happened (or failed): look again shortly after.
  useEffect(() => {
    const timer = setTimeout(refresh, 1500);
    return () => clearTimeout(timer);
  }, [refreshKey, refresh]);

  if (!sync || !sync.online) return null;

  const style: React.CSSProperties = { fontSize: 11, padding: '2px 6px', borderRadius: 3, cursor: 'default', whiteSpace: 'nowrap' };
  if (!sync.supported) {
    return <span style={{ ...style, color: 'var(--text-muted)' }} title="This game server build does not report its loaded data yet (rebuild with B-13).">data sync: unknown</span>;
  }
  const loaded = sync.loadedAt ? new Date(sync.loadedAt).toLocaleTimeString() : '?';
  if (sync.allMatch) {
    return (
      <span style={{ ...style, color: '#8fd18f' }} title={`The game server's data matches the files on disk (loaded ${loaded}).`}>
        ✓ live data = files
      </span>
    );
  }
  const stale = (sync.files ?? []).filter(f => !f.match);
  return (
    <span style={{ ...style, color: '#000', background: '#ffdd88' }}
      title={`Loaded ${loaded}. Different from disk: ${stale.map(f => f.name + (f.live ? '' : ' (not loaded)')).join(', ')}. The server re-reads changed files within a few seconds in dev builds; otherwise press Reload data.`}>
      ≠ {stale.length} file{stale.length === 1 ? '' : 's'} not live
    </span>
  );
};
