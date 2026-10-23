# Plan: agentic design (chat agent in the app, MCP server for external agents)

Status: planned, no code (2026-10-23). Execution order: `ROADMAP.md` step 5. Owner: a new `agent` area
(`src/agent`, `src/app/agent`) with requests to core through `docs/integration-notes/`.

**Goal.** The user connects a model provider (Anthropic, an OpenAI-compatible API, OpenRouter, a local Ollama or LM
Studio server), chats with an agent in a sidebar panel, and the agent works in the app: parts, sketches, assemblies,
drawings, exports. The user keeps working in every other document and in the camera; while the agent's commands are
being applied to a document, user input on that document is suspended with a clear visual state and a Stop button,
then handed back. A running FabriCAD can also be driven by an external agent (Claude Code, any MCP client) through an
MCP server.

**No feature losses.** Everything is additive: no existing tool, verb, setting, objectName, script syntax or `.fcad`
key changes. The agent is one more client of the command line, exactly like `ScriptRunner`. With no provider
configured, the app behaves as today plus a collapsed panel (or none: `agent/enabled`).

---

## 1. What exists and is reused

| Piece | File | Reused for |
|---|---|---|
| Tool registry: `ToolInfo` (id, name, tooltip, aliases, workspace, tab, group), `ITool`, `ToolRegistry::tools/search/byAlias`, `ToolManager::run/runCommandLine/accept/cancel`, `keepsActiveTool` | `src/core/Tool.h` | tool catalogue generation, command execution, "is a command active" |
| Property model: `Property` (Bool/Int/Double/Length/Angle/String/Enum/Vec3/Ref/RefList/Json, label, tooltip, options, refFilter, flags), `FeatureFactory::instance().registerType` | `src/core/Property.h`, `Feature.h` | per-feature input schema (instantiate each type once, read its properties) |
| Shell verbs `OK CANCEL SELECT PICK INPUT OPTIONS INFO LIST CHECK RENAME GROUP ... SCREENSHOT TAB CLOSE`, `evaluateCheck`, `listText`, `CheckContext` | `src/app/ScriptTools.{h,cpp}` (997 lines) | the agent's query and selection verbs; `CHECK` as the agent's self-verification |
| Selectors (`body:Seat/face:top`, `profile:@x,y,z`, `feature:Extrude1`) | `src/app/Selectors.{h,cpp}` | how the model names geometry in text; `INFO` output is the geometry summary |
| `ScriptRunner::runLine` (submit -> settle -> collect messages -> ok/error), `AppShell::submitCommandLine/setScriptMessageSink` | `src/app/ScriptRunner.*`, `AppShell.h` | one agent tool call = one or more lines through the same path, same result shape |
| `HeadlessHost` (no widgets), `core::isBatchMode()` (preview deferred until OK) | `src/app/HeadlessHost.*`, `Tool.h` | MCP stdio mode without a window; fast agent runs |
| Per-tab `Document` + `ToolManager` + `UndoStack` + `SelectionModel` | `src/app/DocumentTab.h`, `core/Document.h`, `UndoStack.h`, `Selection.h` | per-document action lock, one undo group per turn, selection snapshots |
| `MainWindow::setInteractive(false)`, `messageShown(level, text)`, `runCommand`, `handleKeyPress` | `src/app/MainWindow.h` | input suspension hooks, message capture |
| Sidebar: `SidebarHost` splitter of `SidebarPanel`s (`PanelHeader` with glyph, count chip, tool buttons, collapse), `ui/panels`, `ui/sidebarSplit` | `src/app/SidebarHost.h`, `SidebarPanel.h` | AGENT becomes a fourth panel |
| `CommandCard::setLive`, `FeatureListWidget::setPendingLive/setLiveFeature`, breadcrumb EDITING chip, status bar command cell | `src/app/CommandCard.h`, `FeatureListWidget.h` | agent activity shown where the user's own commands show |
| `UiSettings` (validated `ui/*` keys, `changed(key)`), `PreferencesDialog` pages, `theme::Tokens`, caps font rule with `textCase="as-is"` | `src/app/UiSettings.h`, `ui-redesign.md` 3, 5 | `agent/*` settings, Preferences > Agent page, panel styling |
| `tools/uitest/ui.sh`, offscreen grabs, `test_scripting.cpp` pattern (real `MainWindow`, offscreen) | `tools/uitest`, `src/app/tests` | agent GUI and headless tests |
| Tutorial plan's read-only `TestProbe` / `AnchorResolver` / `app::StateQuery` idea (`tutorial.md` 3, 5) | not built yet | `StateQuery` is shared by `CHECK`, the tutorial and the MCP resources |

Registered today (`LIST tools`, 2026-10-23): 211 tool ids (sketch 45, app 45, drawing 42, part 32, view 19,
assembly 18, construct 7, inspect 3). The catalogue is generated, never hand-written.

Available on the build box: Qt 6.11 `Qt6::Network` (HTTP, SSE via `QNetworkReply::readyRead`, `QLocalServer`),
`qtkeychain-qt6` (`Qt6Keychain`, libsecret/gnome-keyring, KWallet), `Qt6::DBus`. Not installed: QtWebSockets,
QtHttpServer (so the MCP transport is stdio and a local socket, not Streamable HTTP).

---

## 2. Architecture

```
            +---------------------- src/app/agent (GUI, depends on app) ---------------------+
            | AgentPanel (sidebar)  ApprovalRow  TurnDiffView  AgentPreferencesPage  InputSuspender |
            +---------------------------------------------+----------------------------------+
                                                          |
   +--------------------- src/agent (fabricad_agent, depends on core + Qt Network/Keychain) ------------------+
   | AgentSession (turn loop)  IProvider {Anthropic, OpenAiCompat, Mock}  SseParser  Transcript  Pricing     |
   | ToolCatalogue (schemas from the registry)   ICommandApi (abstract)   McpServer (JSON-RPC)   KeyStore    |
   +--------------------------------------------------+----------------------------------------------------+
                                                      | ICommandApi implemented by
                                   src/app/agent/ShellCommandApi  (AppShell + ScriptTools + Selectors + StateQuery)
                                                      |
                             ToolManager::runCommandLine  ->  tools, verbs, Document, UndoStack  (unchanged)
```

Dependency rule: `fabricad_agent` knows core and the abstract `ICommandApi`, never widgets or `MainWindow`, so every
provider, the catalogue, the turn loop and the MCP server are unit-tested headless. `src/app/agent/` binds it to the
shell (GUI and `HeadlessHost`). Network code lives only in `src/agent/providers/`.

### 2.1 `ICommandApi`: the stable typed command surface over the script layer

One interface, used by the in-app agent and the MCP server, implemented once (`ShellCommandApi`). Every call runs on
the GUI thread (Document is GUI-thread only), synchronously, and returns a `CommandResult { ok, messages[], error,
millis, stateDelta }` where `stateDelta` is a short text of what changed (features added/changed/failed, body count,
active command, open sketch) built from Document signals recorded during the call.

| Call | Does (via) |
|---|---|
| `run(toolId or alias, args[])` | `ToolManager::run` (starts a command or runs an instant one) |
| `input(text)` / `set(name, value)` | typed input for the active command (`commandLineInput`; `name=value`) |
| `select(selectors[], mode)` / `pick(x,y[,z], opts)` / `inputRefs(property, selectors[])` | `SELECT` / `PICK` / `INPUT` verbs |
| `accept()` / `cancel()` | `OK` / `CANCEL` |
| `options()` / `info(selectors[])` / `list(category)` / `check(expr)` | `OPTIONS` / `INFO` / `LIST` / `CHECK`, text results |
| `script(lines[])` | several command lines in order, stop at the first error (the `ScriptRunner::runLine` loop, same log rows) |
| `document(op, args)` | `NEW OPEN SAVE SAVEAS CLOSE TAB`, exports (`EXPORTSTL/STEP/PDF/SVG/DXF`), `DRAWING`, `SET param=`, `UNITS` |
| `screenshot(target)` | viewport or window PNG (GUI only; error headless, as `SCREENSHOT` today) |
| `undoSteps(n)` / `revertTurn(turnId)` | `UndoStack` (section 4.4) |
| `catalogue(query)` | the tool catalogue (section 2.2), filtered |
| `state()` | the model-context snapshot (section 2.4), also the MCP resources |

Nothing in the table is new behaviour: each row is a verb or alias that exists. New code is the typed wrapper, the
result shape and the delta. `docs/SCRIPTING.md` stays the reference for the text forms; the API doc gains one page
(`docs/AGENT.md`).

### 2.2 `ToolCatalogue`: tool-use schemas generated from the registry

Giving the model 211 tool definitions would cost ~25K tokens per request and most providers handle long tool lists
badly. The catalogue therefore has two layers:

1. **Fixed meta-tools** (12, hand-defined JSON schemas, `strict`-compatible: `additionalProperties:false`, all
   `required`): `fabricad_run {command, args[]}`, `fabricad_input {text | name,value}`, `fabricad_select {selectors[],
   mode}`, `fabricad_pick`, `fabricad_ok`, `fabricad_cancel`, `fabricad_query {kind: options|info|list|check, args}`,
   `fabricad_script {lines[]}`, `fabricad_document {op, args}`, `fabricad_screenshot {target}`, `fabricad_search_tools
   {query}`, `fabricad_undo {steps}`. The in-app agent and the MCP server expose the same twelve.
2. **The command catalogue as data**, generated once per process from `ToolRegistry::tools()` and `FeatureFactory`:
   for every tool `id, name, aliases, tooltip, workspace/tab/group, instant, keepsActiveTool`, and for feature tools
   the property table of a fresh feature of that type (`name, type, label, enum options, refFilter kinds, maxRefs,
   default, hidden`), rendered compactly (`part.extrude EXTRUDE E  "Extrude a profile": distance Length, operation
   Enum[Join|Cut|Intersect|New Body], direction Enum[...], profiles RefList[Profile|Face], targets RefList[Body]...`).
   It goes into the stable part of the system prompt (cacheable, ~6-8K tokens) and is also served by
   `fabricad_search_tools` and `LIST tools` for narrowing. Property-to-tool mapping needs the feature type a tool
   creates: `ITool` gets an optional `createsFeatureType()` (core request; fallback: `FeatureTool::createFeature` is
   already virtual, call it on a scratch context in the catalogue builder).
3. **Optional per-tool typed tools** for providers that support deferred loading (Anthropic `defer_loading` + tool
   search): one strict schema per feature tool generated from the same property table. Phase 5, measured, off by
   default.

A ctest (`fabricad_agent_test_catalogue`) asserts every registered id is in the catalogue, every `PropertyType` maps
to a schema type, and every enum option string is the one `name=value` accepts.

### 2.3 Providers and streaming

```cpp
// src/agent/Provider.h
struct ProviderConfig { QString kind /*anthropic|openai|openrouter|ollama|lmstudio|mock*/, baseUrl, model, keyId;
                        int maxOutputTokens; QString effort; bool thinkingSummaries; double contextTokens; };
struct Capabilities { bool tools, streamingToolArgs, images, thinking, strictSchemas, promptCaching, systemInMessages; };
class IProvider : public QObject {
  virtual Capabilities capabilities() const = 0;
  virtual ProviderRequest* send(const Conversation&, const ToolSet&, const RequestOptions&) = 0;  // streaming
  virtual void listModels(std::function<void(QStringList, QString error)>) = 0;
};
class ProviderRequest : public QObject {  // one HTTP request
 signals: void textDelta(QString); void thinkingDelta(QString); void toolCallStart(QString id, QString name);
          void toolCallArgsDelta(QString id, QString json); void toolCallEnd(QString id);
          void usage(Usage); void finished(StopReason); void failed(ProviderError);
 public: void abort();
};
```

| Provider | Endpoint and wire shape | Notes |
|---|---|---|
| `AnthropicProvider` | `POST {base}/v1/messages`, headers `x-api-key`, `anthropic-version: 2026-10-21`, body `model` (default `claude-opus-5-5`; list from `GET /v1/models`), `max_tokens` (64K streaming), `stream:true`, `system` (stable blocks with `cache_control`), `messages`, `tools` with `strict:true` and `eager_input_streaming:true`, `thinking:{type:"adaptive"[, display:"summarized"]}`, `output_config.effort` from the setting; `tool_choice` never forced (400 on the 5.5 family); optional `fallbacks:"default"` beta when the user enables it | SSE events `message_start`, `content_block_start/delta/stop` (`text_delta`, `input_json_delta`, `thinking_delta`), `message_delta` (`stop_reason`, usage), `message_stop`. Stop reasons handled: `end_turn`, `tool_use`, `max_tokens` (do **not** run that turn's tools, retry once with a higher cap), `refusal` (show `stop_details`, never run tools), `pause_turn` (continue). Document-context deltas go as `{"role":"system"}` entries inside `messages` so the cached prefix survives. Server-side compaction (beta `compact-2026-01-12`) when the transcript nears the limit; the response `content` is appended whole. |
| `OpenAiCompatProvider` | `POST {base}/v1/chat/completions` with `stream:true`, `stream_options:{include_usage:true}`, `tools:[{type:"function", function:{name, description, parameters, strict}}]`, `tool_choice:"auto"`; `GET /v1/models` | Covers OpenAI, OpenRouter (`https://openrouter.ai/api`, adds `HTTP-Referer`/`X-Title` headers), Ollama (`http://localhost:11434`, no key), LM Studio (`http://localhost:1234`), vLLM, llama.cpp server. SSE `data: {choices[0].delta.{content, tool_calls[{index,id,function.{name,arguments}}]}}`, `finish_reason` `tool_calls|stop|length`. `strict` is sent only when the model list / a probe says it is accepted (OpenRouter and local servers vary). Capability probing per endpoint (one cheap request) is cached per base URL. |
| `MockProvider` | no network; replays a transcript file (`tests/agent/transcripts/*.jsonl`) and records the requests it saw | tests, the GUI tester runs (`FABRICAD_AGENT_MOCK=<file>`) |

Shared: `HttpClient` (one `QNetworkAccessManager`, TLS required unless the host is loopback, per-request timeout,
retry with backoff on 408/429/5xx and connection errors, `Retry-After` honoured, HTTP/2 disabled for SSE replies),
`SseParser` (incremental `event:`/`data:` framing, `[DONE]`), `JsonArgsAccumulator` (joins `input_json_delta` /
`arguments` fragments; strict parse and schema validation at `content_block_stop`, invalid -> `tool_result is_error`
with `INVALID_JSON`, never executed). Everything runs on the GUI thread (Qt async I/O); nothing blocks.

### 2.4 Model context

The system prompt has a **stable** part (rules, the tool catalogue, the selector grammar from `docs/SCRIPTING.md`,
unit conventions) and the conversation carries a **volatile** `DocumentContext` block, refreshed at every user turn
and summarised after every tool call (the `stateDelta`):

| Section | Source | Budget |
|---|---|---|
| Document: name, path, units, modified, tabs (design/drawing), active tab | `AppShell::tabInfo`, `Document` | 10 lines |
| Timeline: `LIST features` (status, marker, groups) | `listText` | 60 rows, then "N more, use fabricad_query list features" |
| Parameters, components tree, bodies with volume/bbox/centroid | `LIST params/components/bodies`, `INFO body:*` | 40 rows |
| Selection and preselection, active command with `OPTIONS` (prompt, active input, validity, every property) | `SelectionModel`, `OPTIONS` | 30 lines |
| Open sketch: plane, DOF, profiles with seed points | `INFO sketch:` | 30 lines |
| Recent user actions since the last turn (echo history) | `MainWindow::echoHistory` | 20 lines |
| Optional viewport screenshot (image block, downscaled to <= 1280 px) and the current drawing sheet | `ViewerWidget::grab`, drawing page grab | per setting `agent/screenshot: off|onRequest|everyTurn` |

All document-derived text is wrapped in one `<document>` block and declared data, not instructions (section 6).
Geometry summaries never include B-rep dumps; the model asks `INFO` for detail. Token estimate per turn without
screenshot: 2-5K volatile on top of the cached prefix.

---

## 3. MCP server mode (external agents drive a running FabriCAD)

`McpServer` (`src/agent/mcp/`) speaks MCP JSON-RPC 2.0 (`initialize`, `tools/list`, `tools/call`, `resources/list`,
`resources/read`, `notifications/*`, protocol version `2025-06-18`) over newline-delimited JSON. Transports:

| Mode | How | Use |
|---|---|---|
| Local socket | the running GUI listens on `QLocalServer` at `$XDG_RUNTIME_DIR/fabricad/<pid>.sock` (mode 0600) when `agent/mcpServer` is on or `--mcp-socket` is given; a 32-byte token in `$XDG_RUNTIME_DIR/fabricad/<pid>.token` must be sent in `initialize` | an external agent attaches to the window the user is looking at |
| stdio bridge | `fabricad-mcp [--socket path]` (tiny second executable, `src/agent/mcp/bridge_main.cpp`): stdio <-> the socket of the newest running instance; `claude mcp add fabricad -- fabricad-mcp` | Claude Code and every stdio MCP client |
| Headless stdio | `fabricad --mcp-stdio --offscreen [file.fcad]`: `HeadlessHost` + `McpServer` on stdin/stdout, no window | CI, batch agents; `SCREENSHOT` is an error there as in scripts |

Tools exposed: the same twelve meta-tools (same schemas, same `ShellCommandApi`), so a conformance test covers both
front ends. Resources: `fabricad://state` (section 2.4 text), `fabricad://features`, `fabricad://selection`,
`fabricad://catalogue`, `fabricad://screenshot/viewport` (PNG, GUI only), `fabricad://log` (the last turn's
command rows). Notifications: `notifications/resources/updated` on `recomputeFinished` / selection change (debounced).

An external agent is a **participant**: its calls go through the same `ActionLock`, approval mode and undo grouping
as the in-app agent (section 4), and the AGENT panel shows its activity with a `MCP · <client name>` chip so the user
sees who is acting. Several clients may connect; calls are serialised per document. No TCP, no remote access.

---

## 4. Concurrency, locking, undo

### 4.1 `ActionLock` (one per `Document`, owned by `DocumentTab`)

States: `Free`, `UserCommand` (the tab's `ToolManager::isActive()`), `AgentHeld{participant, turnId}`. An agent turn
takes the lock before its first mutating call (anything but `query`, `catalogue`, `state`, `screenshot` and camera
verbs) and releases it when the turn ends (model stops calling tools, Stop, error, limit). Only the document whose
lock is held is affected; other tabs stay fully interactive.

| Situation | Behaviour |
|---|---|
| Agent wants the lock while the user has a command active | the agent's call returns `busy: user command <id> active`; the session waits (default 20 s, `agent/conflictWait`), shows "WAITING FOR YOUR COMMAND" in the panel and status bar; then the model is told and may ask the user (approval row: FINISH MINE / CANCEL MINE / KEEP WAITING) |
| User tries to act on a locked document | input is suspended (4.2); the status bar says `AGENT WORKING · ESC ESC STOPS`; Stop ends the turn and hands back |
| User types in the panel while the agent runs | the message is queued (shown as PENDING) and sent after the turn; `/stop` is immediate |
| Agent turn ends with a command still active (e.g. in a sketch) | allowed: the model is asked to leave the document idle; if it does not, the card stays with an `AGENT` badge, the user may finish or cancel it, nothing is auto-committed |
| Several agents (chat + MCP clients) | one queue per document, FIFO; one participant holds the lock at a time |
| Agent changes the active tab | allowed only for `TAB`/`OPEN`/`DRAWING` of its own turn; the lock follows the document, not the tab |

### 4.2 Input suspension (`InputSuspender`, `src/app/agent/`)

While `AgentHeld`: an event filter on the tab's `ViewerWidget` (mouse press/release/double-click/key, **not** wheel or
middle-drag or right-drag orbit, so the user can look around), `FeatureListWidget`, `BrowserTree`, `SelectionPanel`,
the `CommandCard` body (shown read-only, `setLive(true)`, `AGENT` badge, Apply/Cancel disabled), the command line and
the marking menu; `MainWindow::runCommand` refuses non-`keepsActiveTool` tools for a locked document with a toast;
shortcuts via `handleKeyPress` likewise. View commands (`FIT`, `TOP`, display modes) stay allowed. The viewport gets a
1 px `live.base` inner frame plus the breadcrumb chip `AGENT WORKING` (same chip style as `EDITING`); the dark
dim-out is not used (the user must see what the agent does). Esc Esc or the Stop button calls
`AgentSession::stop()`.

### 4.3 Execution of a tool call

`AgentSession` receives `toolCallEnd` -> validates args -> approval (section 5.3) -> `QMetaObject::invokeMethod(...,
Qt::QueuedConnection)` -> `ShellCommandApi` runs the lines through `submitCommandLine` + the `ScriptRunner` settle ->
`CommandResult` -> `tool_result` appended -> next request. Calls are strictly sequential; a parallel `tool_use` batch
from the model is executed in order and all results go back in one message. `core::setBatchMode(true)` is set for the
duration of a call only when `agent/livePreview` is off (default on: the user watches). Recompute is synchronous and
blocks the GUI thread as it does for scripts; `Document::cancelRequested_` (the `ctx.isCancelled()` hook) is set by
Stop so long features abort at their next poll.

### 4.4 One undo group per turn, revert turn

`UndoStack::beginMacro` would make `canUndo()` false and forbid the agent's own `UNDO` inside a turn, so the macro is
built **after** the turn: `AgentSession` records `UndoStack::index()` when it takes the lock; at release it calls a
new `UndoStack::collapse(fromIndex, text)` (core request: merges `commands_[from, index_)` into one `MacroCommand`
named `AGENT: <first user sentence>`, no re-execution) so the user's Ctrl+Z undoes the whole turn. Inside the turn the
agent may `fabricad_undo {steps}` its own steps only (never below `fromIndex`). **Revert turn** (panel button and
`/revert`) undoes to `fromIndex` when the stack above it is untouched; otherwise it offers the best-effort
`DocumentSnapshotCommand` restore from the turn's starting JSON (kept per turn, in memory, `agent/keepSnapshots` turns).
Autosave keeps running (a turn is ordinary undoable edits).

---

## 5. UI

### 5.1 AGENT panel (sidebar section, redesign tokens)

`SidebarHost` gets a fourth `SidebarPanel` (`key "agent"`, title `AGENT`, glyph: the v6 hexagon F, count chip = queued
or running calls) below SELECTION by default (`ui/panels` gains `agent:1`, `ui/sidebarSplit` a fourth size,
`ui/agentPanel` `on|off` on the Layout page). Header tools: provider chip (`OPUS 5.5` / `OLLAMA · QWEN3` /
`NO PROVIDER`), mode chip (`ASK` / `AUTO` / `PLAN`), STOP (visible only while running), settings (opens Preferences >
Agent). Body = `AgentTranscriptView` (a `QAbstractScrollArea` painted from `theme::Tokens` like `FeatureListWidget`:
user rows on `accent.selFill`, assistant text in `text.primary` with `textCase="as-is"`, a collapsed `THINKING` row
when summaries are on, tool-call rows as `> EXTRUDE 25` command text in the `value` font with the result's first
message and a status glyph (octagon for errors, as everywhere), approval rows, the turn footer `3 CALLS · 12.4K IN ·
0.9K OUT · $0.07 · REVERT TURN`) and `AgentComposer` (a `QPlainTextEdit` in the `value` font, Enter sends,
Shift+Enter newline, Ctrl+Enter sends with a screenshot; chips SCREENSHOT, SELECTION; slash commands `/stop`,
`/revert`, `/mode ask|auto|plan`, `/model <id>`, `/new`). Width budget: the panel works at 260 px (text wraps,
command rows elide in the middle). Both themes from existing tokens; two new aliases only: `agent.user.bg ->
accent.selFill`, `agent.tool.bg -> surface.header`. Everything in caps except transcript text, commands and values.

### 5.2 Agent activity in the existing surfaces

| Surface | While the agent works |
|---|---|
| FEATURES | the feature being created shows the gold live row (`setPendingLive`) exactly as for a user command, with the label prefix `AGENT ·`; features added this turn get a thin `accent.indicator` left tick until the turn ends (then the usual look) |
| Command card | the agent's command is presented through the normal `CommandCard`; header context reads `AGENT · PART · Bracket`; body read-only; gold hairline on |
| Breadcrumb / status bar | chip `AGENT WORKING` and `STOP` hint; the command cell shows the agent's command; the prompt cell shows the model's current step text (first 60 chars of its last text delta) |
| Viewport | live previews as today; 1 px `live.base` inner frame while locked |
| Document tab | a small gold dot on the tab of a locked document (so a user on another tab sees it) |

### 5.3 Approvals: ask / auto / plan, preview and diff before apply

| Mode (`agent/approval`) | Behaviour |
|---|---|
| `ask` (default) | every call that mutates shows an approval row: the exact command line(s), for feature commands the `OPTIONS` value table, and the live viewer preview (the tool is **started** so the preview and card appear, `OK` is held until APPLY); buttons `APPLY · SKIP · APPLY ALL THIS TURN · STOP` (Enter = Apply). Queries never ask. |
| `auto` | runs without asking, except the always-ask list: `DELETE`/`DELETECOMPONENT`, `CLOSE`/`NEW`/`OPEN` of another file with unsaved changes, `SAVE`/`SAVEAS`/exports to a path outside the document's folder, `UNITS`, anything after `agent/maxToolCallsPerTurn` (default 40) |
| `plan` | read-only: mutating calls are refused with `plan mode: describe the steps instead`; good for "what would you do" |

**Diff before apply.** For `fabricad_script` (several lines) in `ask` mode the approval row shows the whole script.
At turn end a `TurnDiff` (before/after of `LIST features`, bodies with volumes, params, components; unified-diff
style, generated by `StateQuery`) is folded under the footer. Deletions show the removed feature names in
`status.error` with the octagon.

### 5.4 Preferences > Agent

Provider (combo: Anthropic, OpenAI-compatible, OpenRouter, Ollama, LM Studio, custom URL), base URL, model (combo
filled by `listModels`, free text allowed), API key (password field; stored to the keyring on OK and shown as
`STORED IN KEYRING · REPLACE · FORGET`), effort (Anthropic: low/medium/high/xhigh/max), thinking summaries, max
output tokens, approval mode, screenshot policy, live preview during agent calls, cost limits (per turn, per session),
tool-call limit, conflict wait, MCP server on/off with the socket path shown, `fabricad-mcp` command to copy. A TEST
button sends a 1-token request and shows latency and the model's reply.

---

## 6. Security

| Topic | Rule |
|---|---|
| API keys | `KeyStore` over QtKeychain (`QKeychain::WritePasswordJob`, service `FabriCAD`, key `provider/<id>`). Never in QSettings, never in `.fcad`, never logged. Fallback when no keyring is available: environment variables (`FABRICAD_ANTHROPIC_API_KEY`, `FABRICAD_OPENAI_API_KEY`, or the provider's own `ANTHROPIC_API_KEY`/`OPENAI_API_KEY`) or a session-only key asked at first use; the panel says which source is in use. Local providers need no key. |
| `.fcad` | unchanged. Transcripts are not saved into the document. Optional transcript export is a separate `.jsonl` next to the file (`agent/exportTranscript`, off), keys and headers redacted. |
| Network | only the configured base URL (plus its `/v1/models`), TLS required except loopback, no redirects off-host, no telemetry, no update checks. The MCP socket is local-only with a per-instance token. A proxy follows `QNetworkProxy::applicationProxy` from the environment. |
| Prompt injection from imported files | names from STEP/`.fcad` files, sketch text, drawing notes, parameters comments and paths reach the context. They are placed only inside the `<document>` data block; the stable system prompt says that block is data; control characters are stripped and each string capped (120 chars). Tool results are data too. In `auto` mode anything that writes outside the document's folder, opens other files or closes with unsaved changes still asks (5.3). The agent has no shell, no file tool beyond the existing export/import verbs, and `IMPORT`/`OPEN` of a path not under the document's folder asks. |
| Screenshots | sent only per `agent/screenshot`; the composer chip shows when one is attached; never for MCP resources unless the client requests the resource. |
| Cost | usage from the provider (`usage.input_tokens`/`cache_read_input_tokens`/`output_tokens`; OpenAI `usage` via `stream_options`) times `resources/agent/pricing.json` (model -> $/MTok, user-overridable in `XDG_CONFIG_HOME`; unknown model = tokens only). Panel footer per turn and session; `agent/maxCostPerTurn` (default $1), `agent/maxCostPerSession` (default $10), `agent/maxToolCallsPerTurn` (40), `agent/maxTurnSeconds` (600) stop the turn with a message; limits are soft for local providers (tokens only). |
| Logging | `XDG_DATA_HOME/FabriCAD/agent/<date>.jsonl`: requests without keys, tool calls, results, usage; `agent/log` off|on; a `COPY LOG` button in the panel for bug reports. |

---

## 7. Offline and no-agent builds

| | |
|---|---|
| CMake | `option(FABRICAD_AGENT "Chat agent and providers" ON)`, `option(FABRICAD_MCP "MCP server" ON)`. Both need `Qt6::Network` (in qt6-base); `FABRICAD_AGENT` also finds `Qt6Keychain` (if missing: build with the env-var key fallback only, printed at configure). OFF removes `src/agent/providers`, the panel and the Preferences page (`#if FABRICAD_AGENT`); the Layout page hides `ui/agentPanel`. |
| Runtime without a provider | the panel shows one setup card (`CONNECT A PROVIDER · PREFERENCES`, or `LOCAL SERVER FOUND: OLLAMA` when `localhost:11434` answers a probe the user triggered); no network call happens until the user configures something. `agent/enabled=false` or `--no-agent` hides the panel and the MCP server. |
| Scripts, headless runner, tests | unchanged; the agent never starts in `--script` runs. `--mcp-stdio` is the only headless agent entry. |
| Packaging (AUR) | `qtkeychain-qt6` becomes an optional dependency (`optdepends`), `qt6-base` already covers Network. |

---

## 8. Testing

| Test | What |
|---|---|
| `fabricad_agent_test_catalogue` (ctest, headless) | every registered tool id in the catalogue; schema types for every `PropertyType`; enum options accepted by `name=value`; the twelve meta-tool schemas are valid strict JSON schemas |
| `fabricad_agent_test_providers` | fake endpoints on a local `QTcpServer`: Anthropic SSE (asserts headers, `anthropic-version`, `stream`, `strict`, no forced `tool_choice`, `thinking.adaptive`, `max_tokens` retry, `refusal` never runs tools, `pause_turn` continues, 429 backoff) and OpenAI-compatible SSE (`tool_calls` fragments, `[DONE]`, usage chunk); `SseParser` and `JsonArgsAccumulator` unit tests incl. invalid JSON -> `is_error` |
| `fabricad_agent_test_session` (real `MainWindow`, offscreen, `MockProvider`) | a transcript builds the bracket plate: lock taken and released, one undo step per turn (`collapse`), revert turn gives byte-equal `toJson()`, approval `ask` holds `OK`, `plan` refuses, cost limit stops, conflict with an active user command (user starts `EXTRUDE`, agent waits, user `OK`, agent proceeds), input suspension (QTest clicks on the viewer/FEATURES are swallowed while locked, accepted after), other tab stays interactive |
| `fabricad_agent_test_mcp` | `QLocalSocket` client: `initialize` (token), `tools/list` == the twelve, `tools/call fabricad_script`, `resources/read fabricad://features`, notifications after a change; and the headless `--mcp-stdio` process end to end through `QProcess` |
| `fabricad_agent_transcripts` (ctest, data-driven) | `tests/agent/transcripts/*.jsonl` recorded from real runs (bracket, chair components, a drawing with views and PDF export, an assembly joint); each ends with `CHECK` lines the mock asserts, like `tests/chair/chair.fcs` |
| Tool-call conformance | the same transcript runs through the in-app session and through MCP `tools/call`; results identical |
| GUI (Sonnet testers, `ui.sh`, `FABRICAD_AGENT_MOCK`) | T-agent: dark and light, panel at 260 and 320 px, approval row by keyboard, Stop mid-turn, user clicks during a turn (refused with the state visible), revert turn, the live row and chip, both sidebar sides and the docked card; screenshots compared with the token rules (no new literals: `theme_color_lint`) |
| Live smoke (manual, not in ctest) | `FABRICAD_LIVE_API_TEST=1` with a key in the keyring: one short turn per configured provider |

---

## 9. Phased roadmap

| Phase | Content | Size (lines incl. tests) | Gate |
|---|---|---|---|
| **A0** Command API and catalogue (no UI, no network) | `ICommandApi`, `ShellCommandApi`, `CommandResult`/`stateDelta`, `StateQuery` (shared with `CHECK`; the tutorial reuses it), `ToolCatalogue` + schemas, core requests `UndoStack::collapse`, `ITool::createsFeatureType()`, `LIST tools --json` | 1 400 | catalogue and session-less tests green; `tests/scripts` unchanged and green |
| **A1** Providers | `IProvider`, `HttpClient`, `SseParser`, `JsonArgsAccumulator`, `AnthropicProvider`, `OpenAiCompatProvider`, `MockProvider`, `KeyStore`, `Pricing`, `agent/*` settings in `UiSettings` | 1 700 | provider tests against fake endpoints; one manual live turn each (Anthropic, OpenRouter, Ollama) |
| **A2** Session, lock, undo | `AgentSession` turn loop, `ActionLock`, `InputSuspender`, approval engine (modes, always-ask list), per-turn undo collapse and revert, limits, logging, `DocumentContext` text | 1 500 | `test_session` green; bracket transcript builds headless |
| **A3** UI | `AgentPanel` (transcript view, composer, approval rows, footer), sidebar integration, activity in FEATURES/card/breadcrumb/status/tab dot, Preferences > Agent, setup card | 2 300 | T-agent tester pass; `theme_color_lint`, `test_layout_text` at 260 px; existing toggles pass with the panel on and off |
| **A4** MCP | `McpServer`, local socket + token, `fabricad-mcp` bridge, `--mcp-stdio` headless, resources and notifications, participant chip | 1 100 | `test_mcp` green; Claude Code drives a running window through the bridge (manual) |
| **A5** Context and cost polish | screenshots (viewport and sheet), prompt caching layout and `role:system` deltas, compaction, per-tool deferred schemas (measured), cost display, pricing file, `docs/AGENT.md`, README section | 900 | cache hit shown by `cache_read_input_tokens` > 0 on turn 2; cost within 10 % of the console |
| **A6** Transcripts and verification | recorded transcripts (part, assembly, drawing, export), conformance test, GUI pass repeated in light, tester issues fixed | 900 | all ctest green; no open high/medium issue |

Total ≈ 9 800 lines, 11-14 sessions. A0 and A1 touch no UI file and can run in a worktree beside roadmap step 4;
A2-A6 follow it. Worktrees: `agent/api` (A0), `agent/providers` (A1), `agent/session` (A2), `agent/ui` (A3),
`agent/mcp` (A4). Each builds in its own `build-<area>` with `-j2`.

---

## 10. Risks

| Risk | Mitigation |
|---|---|
| Provider wire formats drift (Anthropic betas, OpenRouter quirks, Ollama tool support varies by model) | capability probing and per-provider feature flags; fake-endpoint tests pin the shapes we send; one manual live turn per release |
| Models write bad selectors or wrong ids | the catalogue text carries the selector grammar and the "prefer world points" advice from `SCRIPTING.md`; every call returns the current `OPTIONS`/`LIST` delta; `CHECK` is advertised as the way to verify |
| Long synchronous recompute freezes the panel and the Stop button | Stop sets `cancelRequested_` before the next call; calls are one line each where possible; a worker-thread recompute stays out of scope (roadmap step 2 (d) notes) |
| Undo collapse interacts with merges/clean index | `collapse` keeps `cleanIndex_` semantics (if the clean index falls inside the range it becomes `resetClean`); unit tests in `test_undo_selection.cpp` |
| Sidebar height: four panels at 1080 px | AGENT defaults collapsed when a command card is docked; `SidebarHost::applyLayout` treats it like the others; tested at 1280x800 |
| Token cost of the catalogue | measured in A5; deferred per-tool schemas and the `fabricad_search_tools` fallback; the catalogue is in the cached prefix |
| Prompt injection via file content | data block + rules + always-ask list; a transcript test feeds a STEP with a hostile component name and asserts no file is written |
| Keyring absent on some desktops | env-var and session-only fallbacks, clearly labelled; never plaintext QSettings |

---

## 11. Open questions for the user

1. **Default provider and model** for a fresh install: none (setup card) or Anthropic `claude-opus-5-5` pre-selected
   with an empty key? Should Ollama be auto-detected on `localhost:11434` (one local probe when the panel opens)?
2. **Panel default**: AGENT panel visible and collapsed by default, or hidden until a provider is configured?
3. **Approval default**: `ask` (every mutating call) or `auto` with the always-ask list? Should "APPLY ALL THIS TURN"
   persist for the session?
4. **Camera during a turn**: keep orbit/pan/zoom and view commands available while the agent works (proposed), or
   freeze the viewport completely?
5. **Stop gesture**: Esc Esc on the canvas (proposed) plus the button, or a dedicated shortcut (e.g. Ctrl+Shift+.)?
6. **Undo granularity**: one step per turn (proposed, via `UndoStack::collapse`) or one step per agent command with a
   separate "revert turn"?
7. **Screenshots to the model**: off, on request (chip / Ctrl+Enter, proposed), or every turn? Downscale limit?
8. **MCP exposure**: local socket + `fabricad-mcp` stdio bridge only (proposed). Is a TCP/Streamable-HTTP transport
   wanted later (would need QtHttpServer or a hand-written HTTP server)?
9. **Transcript persistence**: keep transcripts per document in a sidecar file (opt-in) or in memory only?
10. **Cost limits** defaults ($1 per turn, $10 per session) and whether local providers show token counts only.
11. **Build flag defaults**: `FABRICAD_AGENT` and `FABRICAD_MCP` ON for the AUR package, with `qtkeychain-qt6` as a hard
    or optional dependency?
12. **Thinking display**: show the provider's thinking summaries in a collapsed row (costs nothing extra on Anthropic,
    `display:"summarized"`) or hide them?
