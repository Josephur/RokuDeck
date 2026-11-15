# Network Peers — Design Proposal

- Status: **Proposed — for discussion** (implements the sketch in issue #2)
- Date: 2026-11-14
- Owner: daemon (`rokudeckd`); the widget is a client and learns nothing new
- Extends: `2026-08-23-automations-design.md`, `2026-08-24-pc-state-source-design.md`
- Motivation: a household with several PCs should be able to write
  "mute the *office* PC when the *living room* TV starts playing" without
  SSH or scripts. M7 made the local PC a first-class device; this extends
  the same pattern across machines.

## 0. The one non-negotiable

**Rules stay strictly per-machine local.** A rule is stored, evaluated, and
executed only by the daemon that owns it. A peer is an *observed device and
an action target* — never a rule host, never a rule sync target. If this
one property is ever in tension with a feature below, this property wins.

## 1. Identity

- A peer's identity is its machine: serial literal `PC-` + the first 8 hex
  chars of the peer's `/etc/machine-id` (e.g. `PC-9f3c2a10`). Collisions on
  a home LAN are negligible; a duplicate serial is rejected at add time
  exactly like a duplicate Roku serial.
- The display name defaults to the peer's advertised hostname and is
  user-renameable (the existing `nameIsUserChosen` machinery works
  unchanged).
- `RemoveDevice` on a peer removes the stored peer and revokes the
  controller's stored token for it. The peer device is never produced by
  Roku discovery.

## 2. Transport

- TCP listener on the peer, default port **49160** (private range; final
  choice is an open question), bound by default to private/LAN interfaces
  only when detectable, else all interfaces with the consent gate below as
  the real boundary.
- **Protocol**: newline-delimited UTF-8 JSON envelopes — the codebase's
  existing snapshot/rule envelope style, trivially testable, debuggable
  with `nc`. Every envelope carries `schemaVersion`.
- **Pull, not push, in v1**: the controller's *existing* poll scheduler
  polls each peer's snapshot endpoint on its normal `refreshPolicy`
  cadence. Stale marking, health flip, and offline outcomes then reuse the
  LOCAL-PC semantics unchanged (three consecutive failures → `offline`).
  Peer-push is a possible v2; nothing in the design should assume it.

## 3. Pairing and consent (both directions)

- **Pairing**: the peer mints a one-time pairing code (short, human
  copyable, shown by `rokudeckd peers` / the widget config page). The
  controller submits the code plus the peer's IP:port once; the peer
  records the controller's machine-id and hands back a long-lived
  connection token. The code is burned on first use.
- **Stored consent**: symmetric. The controller stores its token for the
  peer; the peer stores which controller machine-ids may pull state and
  issue actions. Either side can revoke by deleting its entry; revocation
  on the peer side makes in-flight pulls fail with a typed auth error,
  which the controller surfaces as device offline — honest, no hangs.
- **Threat model, stated honestly**: traffic is LAN-local and *not*
  encrypted in v1; the token travels on each connection. This matches the
  existing threat model of the Roku ECP path (plaintext HTTP on the LAN).
  TLS is future work and the envelope framing is deliberately versioned so
  it can be swapped without touching the registry.
- No rule definitions, no rule documents, no client list ever crosses the
  wire — only the envelope in §4 and the commands in §5.

## 4. State envelope

The peer answers a snapshot request with a **subset of the existing
snapshot envelope**, so the controller can reuse its parser and the
`capturedAt` ordering guard verbatim:

```json
{"schemaVersion":1,"serial":"PC-9f3c2a10","displayName":"office",
 "health":"online","volume":42,"muted":false,"playing":true,
 "capabilities":["volume","mute"],"capturedAt":"...iso8601..."}
```

- `serial` is the peer's own identity (`PC-…`), so the controller never
  has to rewrite identities.
- The peer computes its own snapshot from the same M7 sources (wpctl +
  MPRIS) — the LOCAL-PC poller code becomes a library used by both sides.
- Only volume/mute/playing leave the machine. No app names, no media
  titles, no player list. If MPRIS playing later wants player names, that
  is a new, explicitly consented field — not a silent addition.

## 5. Actions

- `{"operation":"setVolume","value":N}` — **absolute** (no nudge walks over
  the network), clamped by the peer's own volume ceiling, which remains
  stored and enforced on the *peer* (source of truth) and echoed to the
  controller for UI.
- `{"operation":"setMute","value":bool}` — **explicit**, not a toggle. The
  mute-optimism work on the widget just demonstrated why toggles over a
  latency-bearing path are ambiguous; the widget still does its local
  optimistic flip, the daemon translates the `VolumeMute` keypress into
  `setMute(!lastObserved)` for peer targets, and a contradiction reverts.
- `keypress` and `launchApp` targeting a peer are rejected
  (`SafetyRejected`) exactly like LOCAL-PC.
- Commands queue through the existing per-device command pipeline; a peer
  command honors the same budgets and typed failures as LOCAL-PC commands.

## 6. Registry and editor integration

- The peer enters the device registry exactly like a Roku: JSON device
  surface, refresh policy, allowAutomations (forced true — it is
  consent-gated by pairing instead), volume ceiling (peer-owned).
- The device detail page hides remote controls for peers (same as
  LOCAL-PC: nothing to press) and shows volume/mute/playing state.
- The rule editor's device pickers list peers automatically — no new
  editor code. `stateChange` triggers on `playing`/volume and
  `setVolume`/`mute` actions just work; a peer offline at fire time
  records the existing `partial`/`failed` outcomes.

## 7. Widget configuration

- New **Peers** section on the config page: add by IP:port + pairing code
  (mDNS discovery is explicitly *not* in v1 — see §9), list paired peers,
  revoke. The pairing-code UX is the one genuinely new UI surface.

## 8. Testing

- Protocol unit tests: envelope parsing (strict, typed-list rejection
  style consistent with `14b3a5a`), framing, version mismatch behavior.
- Integration: a fake peer over loopback TCP in the daemon-dbus-test
  harness (pair → pull → act → revoke → stale), asserting honest outcomes
  at every step.
- Plasmoid bridge tests: a peer device renders as read-only with working
  mute optimism.

## 9. Deliberate v1 cuts (and why)

- **No mDNS advertisement/discovery.** Qt's `QDnsLookup` cannot advertise;
  advertising needs an Avahi dependency. Add-by-IP + pairing code is
  testable with zero new dependencies; discovery is a natural v2.
- **No peer-push.** Pull reuses the scheduler and offline machinery
  untouched.
- **No TLS.** LAN-local, ECP-equivalent threat model; framing is versioned
  for a later swap.
- **No cross-machine rules, ever** — §0.

## 10. Open questions for review

1. Port number: 49160 as default, configurable via daemon config?
2. Pairing UX: does the peer-side code display live in the widget config
   page, a `rokudeckctl peers` CLI, or both?
3. Should the listener default-on or require explicit enable? (Proposal:
   explicit enable — consent is the design's spine.)
4. Does `playing` need MPRIS player identity in v2, and is that acceptable
   to expose at all?
5. Milestone split — proposed: M9a pairing+listener, M9b snapshots into the
   registry, M9c actions, M9d widget config + editor polish.
