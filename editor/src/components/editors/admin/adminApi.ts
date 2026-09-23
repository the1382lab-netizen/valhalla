/**
 * The dashboard's client for the Valhalla 2.0 admin API (proxied by the editor
 * server to the UE server's /api/admin/*).
 */

/** Fired on window after every admin POST so the dashboard can show the result. */
export const ADMIN_RESULT_EVENT = 'valhalla-admin-result';

export interface AdminResult {
  ok: boolean;
  action: string;
  message: string;
}

/**
 * POST an admin action. Never throws: a failure (HTTP error, server offline,
 * bad JSON) comes back as `{ ok: false, error }` and is announced through
 * ADMIN_RESULT_EVENT, so a rejected spawn or drop is visible instead of
 * looking exactly like a success.
 */
export async function postAdmin(action: string, body: any, opts: { quiet?: boolean; label?: string } = {}): Promise<any> {
  let result: any;
  try {
    const res = await fetch(`/api/admin/${action}`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });
    const json = await res.json().catch(() => ({}));
    result = res.ok
      ? { ok: true, ...json }
      : { ...json, ok: false, error: json?.error || `HTTP ${res.status}` };
  } catch (err: any) {
    result = { ok: false, error: err?.message || 'request failed' };
  }
  // A quiet call only announces failures (used for polling reads like inspect).
  if (!opts.quiet || !result.ok) {
    const detail: AdminResult = {
      ok: !!result.ok,
      action: opts.label || (body?.action ? `${action} ${body.action}` : action),
      message: result.ok ? (result.message || 'done') : result.error,
    };
    window.dispatchEvent(new CustomEvent<AdminResult>(ADMIN_RESULT_EVENT, { detail }));
  }
  return result;
}

/** Shorthand for POST /player-action. */
export function playerAction(sessionId: string, action: string, extra: Record<string, unknown> = {}, label?: string) {
  return postAdmin('player-action', { sessionId, action, ...extra }, { label });
}
