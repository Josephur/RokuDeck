# Lua Hooks — Design Proposal (v2)

- Status: **Proposed — for discussion** (v2 candidate; the structured rule
  editor remains the primary surface)
- Date: 2026-11-15
- Owner: daemon (`rokudeckd`) embeds the interpreter; widget/CLI are clients
- Extends: `2026-11-14-network-peers-design.md` (same v2 posture), the
  runScript containment work
- Motivation: power users hit the structured editor's expressiveness ceiling
  (the media/playing field confusion, cross-device edges, re-assertion
  policies). A scripted hook gives them full expressiveness — without
  weakening the safety model that makes the daemon trustworthy.

## 0. The non-negotiable

**Scripts request; the engine executes.** A Lua script never touches wpctl,
rds, or sockets directly. It reads state through the API and *requests*
actions, which flow through the same validated, queued, ceiling-clamped
pipeline as editor rules and remote controls. Every script-initiated action
produces the same honest run records as any other origin.

## 1. Shape

- Rules gain one new trigger and one new action flavor, not a parallel
  engine: a **Lua predicate** (a script evaluated on an edge, returning
  true/false) and a **Lua action** (a script run when the rule fires). Both
  are named files in the daemon's scripts directory, exactly like `runScript`
  — the containment model (name regex, canonical-path resolution, no shell)
  is already built and tested.
- The interpreter is embedded (Lua 5.4 via sol2), **one fresh Lua state per
  evaluation** — no script can observe or influence another script's state,
  and a crashed evaluation cannot poison the daemon.
- **Bounded execution**: `lua_sethook` instruction-count budgets and a wall
  clock deadline per evaluation (the runScript timeout machinery, reused).
  A script that overruns is killed and the run records `timeout`.

## 2. The API (deliberately small)

```lua
-- Lua predicate: return true/false. Anything else is a parse-time error.
function evaluate()
    local pc = state.device("LOCAL-PC")
    local tv = state.device("X00000N6509S")
    return pc.playing == true and tv.volume ~= nil and tv.volume > 30
end

-- Lua action: request through the pipeline; requests are recorded like any
-- action and the run outcome reflects what actually happened.
function execute()
    actions.mute("X00000N6509S")
    actions.set_volume("X00000N6509S", 15)   -- clamped by the ceiling
end
```

- `state.device(serial)` returns the latest snapshot fields (volume, muted,
  playing, power, health, activeApp) — read-only, no I/O.
- `actions.*` mirrors exactly the editor's action set (mute, set_volume,
  keypress, launch, run_rule) — each request is validated and queued with
  the same delivery verdicts.
- No sockets, no file I/O, no os/io libraries (Lua's sandbox: strip those
  libraries at state creation), no timers — the daemon's triggers schedule
  evaluations, not the script.

## 3. Semantics already settled by the existing engine

- Edges: a Lua predicate on a stateChange trigger is evaluated on snapshot
  transitions with the same baseline-safe rising-edge rule; trigger scoping
  rules (device list or scoped trigger) apply unchanged.
- Safety gates: ceiling clamps, stale-state rejections, no-op matching, and
  per-device queues all live below the script layer.
- Run records: every evaluation and action request is recorded with honest
  outcomes (`ok`, `rejected`, `timeout`, `condition-false`).

## 4. Why not "replace the editor with Lua"

- The structured model is what allows the daemon to be trusted: validation,
  ceilings, no-op matching, and delivery verdicts are engine-side. A script
  can express anything — including things the daemon should refuse — so the
  guards would need re-implementation inside the interpreter anyway.
- The editor stays for the common shapes; Lua is the power-user escape hatch
  when a shape doesn't fit. JSON copy/paste (shipped) covers most of the
  remaining gap without an interpreter.

## 5. Deliberate cuts

- No persistent Lua state between evaluations (scripts are pure functions of
  the snapshot; state belongs in the daemon).
- No network, no file I/O, no os library, no `require`.
- No script *editing* in the widget — scripts live in the daemon's scripts
  directory and are edited like `runScript` scripts today.

## 6. Open questions

1. Sol2 vs raw C API: sol2 is ergonomic; raw keeps dependencies minimal.
2. Should Lua predicates be evaluated for edges only, or also for interval
   re-assertion policies (the "re-mute while playing" pattern)?
3. Which Lua version baseline (5.4 latest vs distro-pinned)?
