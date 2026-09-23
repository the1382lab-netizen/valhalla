#!/usr/bin/env node
// Reconnecting stdio <-> Streamable-HTTP bridge for Epic's Unreal MCP (UE 5.8).
//
// Why this exists: Claude Desktop only speaks stdio to local MCP servers, and the
// stock `mcp-remote` bridge exits the moment the Unreal Editor closes (the HTTP
// endpoint disappears during every C++ link cycle). Claude then drops the server
// for the rest of the session. This bridge keeps the stdio side alive forever and
// re-dials http://127.0.0.1:8000/mcp on demand, so an editor restart costs a few
// seconds instead of a Claude restart.
//
// Usage (claude_desktop_config.json):
//   "unreal-mcp": { "command": "node", "args": ["C:\\...\\Tools\\unreal-mcp-bridge\\bridge.mjs"] }
// Optional env: UNREAL_MCP_URL (default http://127.0.0.1:8000/mcp),
//               UNREAL_MCP_RETRY_SECONDS (default 240).

import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StreamableHTTPClientTransport } from "@modelcontextprotocol/sdk/client/streamableHttp.js";
import { ListToolsRequestSchema, CallToolRequestSchema } from "@modelcontextprotocol/sdk/types.js";

const UPSTREAM_URL = process.env.UNREAL_MCP_URL || "http://127.0.0.1:8000/mcp";
const RETRY_SECONDS = Number(process.env.UNREAL_MCP_RETRY_SECONDS || 240);

const log = (...a) => console.error("[unreal-mcp-bridge]", new Date().toISOString(), ...a);

let upstream = null;      // connected Client, or null
let connecting = null;    // in-flight connect promise

function dropUpstream(reason) {
  if (upstream) {
    log("upstream dropped:", reason);
    try { upstream.close().catch(() => {}); } catch {}
  }
  upstream = null;
}

async function connectOnce() {
  const transport = new StreamableHTTPClientTransport(new URL(UPSTREAM_URL));
  const client = new Client({ name: "unreal-mcp-bridge", version: "1.0.0" }, { capabilities: {} });
  transport.onclose = () => { if (upstream === client) dropUpstream("transport closed"); };
  transport.onerror = (e) => { if (upstream === client) dropUpstream("transport error: " + (e?.message || e)); };
  await client.connect(transport);
  log("connected to", UPSTREAM_URL);
  return client;
}

// Connect with retries so an editor restart (~30-90 s) is survivable.
async function getUpstream() {
  if (upstream) return upstream;
  if (connecting) return connecting;
  connecting = (async () => {
    const deadline = Date.now() + RETRY_SECONDS * 1000;
    let delay = 1000;
    for (;;) {
      try {
        upstream = await connectOnce();
        return upstream;
      } catch (e) {
        if (Date.now() > deadline) throw new Error(`Unreal MCP unreachable at ${UPSTREAM_URL} for ${RETRY_SECONDS}s: ${e?.message || e}`);
        log("connect failed, retrying in", delay, "ms:", e?.message || e);
        await new Promise((r) => setTimeout(r, delay));
        delay = Math.min(delay * 2, 10000);
      }
    }
  })().finally(() => { connecting = null; });
  return connecting;
}

// Run an upstream request; on a connection-level failure, reconnect once and retry.
async function withUpstream(fn) {
  try {
    return await fn(await getUpstream());
  } catch (e) {
    const msg = String(e?.message || e);
    const connErr = /ECONNREFUSED|ECONNRESET|fetch failed|Not connected|closed|socket hang up|network/i.test(msg);
    if (!connErr) throw e;
    dropUpstream("request failed: " + msg);
    return await fn(await getUpstream());
  }
}

const server = new Server({ name: "unreal-mcp", version: "1.0.0" }, { capabilities: { tools: { listChanged: true } } });

server.setRequestHandler(ListToolsRequestSchema, async () => {
  try {
    return await withUpstream((c) => c.listTools());
  } catch (e) {
    log("tools/list failed:", e?.message || e);
    // Advertise the three meta-tools so the session keeps them while the editor is down.
    return {
      tools: [
        { name: "list_toolsets", description: "List all available toolsets (Unreal Editor must be running with the MCP server started).", inputSchema: { type: "object", properties: {} } },
        { name: "describe_toolset", description: "Describe a toolset's tools and schemas.", inputSchema: { type: "object", properties: { toolset_name: { type: "string" } } } },
        { name: "call_tool", description: "Call a toolset tool by name.", inputSchema: { type: "object", properties: { toolset_name: { type: "string" }, tool_name: { type: "string" }, arguments: { type: "object" } }, required: ["tool_name"] } },
      ],
    };
  }
});

server.setRequestHandler(CallToolRequestSchema, async (req) => {
  try {
    return await withUpstream((c) => c.callTool({ name: req.params.name, arguments: req.params.arguments || {} }));
  } catch (e) {
    return { isError: true, content: [{ type: "text", text: `Unreal MCP unavailable: ${e?.message || e}. Is the Unreal Editor open with the MCP server started (Editor Preferences > Model Context Protocol > Auto Start Server, or console 'ModelContextProtocol.StartServer')?` }] };
  }
});

process.on("uncaughtException", (e) => log("uncaughtException", e?.stack || e));
process.on("unhandledRejection", (e) => log("unhandledRejection", e?.stack || e));

const transport = new StdioServerTransport();
await server.connect(transport);
log("stdio bridge up; upstream", UPSTREAM_URL);
// Warm the connection in the background; failures are retried lazily on first use.
getUpstream().catch((e) => log("initial connect failed:", e?.message || e));
