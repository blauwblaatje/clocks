# CRG WebSocket protocol notes

Read from the CRG source, not from memory.

- Repo: https://github.com/rollerderby/scoreboard
- Commit read: `3ba7f744a7c4e5d9d9e60369ca78d95705913ea4` (dev head, 2026-11-09)
- Server: `src/com/carolinarollergirls/scoreboard/jetty/WS.java`
- Browser client: `html/json/WS.js`
- Existing jam timer screen (eJT): `html/nso/jt/index.html`, `index.js`
- Game logic: `core/game/GameImpl.java`, clocks: `core/game/ClockImpl.java`

Captured against a running server on 2026-11-10 (the same commit, still
origin/dev that day): raw WebSocket client, `/nso/jt/` and `/nso/sbo/` in
headless Chrome. Section 11 has what the capture showed; sections 1 to 10
are corrected where it differed. Re-check against the version the league
actually runs.

## 1. Connection

- URL: `ws://<host>:<port>/WS/?source=<text>&platform=<text>`
- `source` and `platform` are optional and only shown in Settings > Clients.
  Without `source` the client is listed as `CUSTOM CLIENT`; without
  `platform` the `User-Agent` header is used. Send both, e.g.
  `source=jamtimer&platform=ESP32-C6%20fw0.1`.
- Text frames only, one JSON object per frame. Server max message 1 MiB.

## 2. Session and device identity

- The server identifies a *device* by the HTTP session cookie
  `CRG_SCOREBOARD` (HttpOnly, SameSite=Lax, 15 days, refreshed on each
  request).
- A session is created on any request, including the WebSocket upgrade
  itself. So a separate HTTP fetch is **not required**: read `Set-Cookie`
  from the upgrade response (or from a plain `GET /`), store the value in
  NVS, and send `Cookie: CRG_SCOREBOARD=<value>` on every later connect.
- Without the stored cookie every reconnect creates a new device entry and
  loses any per-device write permission. Persisting it is mandatory.
- The WebSocket library must allow reading response headers and setting a
  request header. Check this when choosing the library; otherwise do
  `GET /` over HTTP first, as the Stream Deck plugin does.
- Devices not seen for 15 days are garbage collected.
- Captured: `GET /` and every WebSocket upgrade answer with `Set-Cookie`,
  also when a valid cookie was sent (the expiry is refreshed; the value
  stays the same). The value looks like `node0<random>.node0`; store and
  send back the whole value. Reconnecting with it gives the same
  `WS.Device.Id` and name; without it, a new device each time.

## 3. Client to server messages

| Action | Frame | Permission |
|---|---|---|
| Register | `{"action":"Register","paths":["...", "..."]}` | always |
| Ping | `{"action":"Ping"}` | always |
| Set | `{"action":"Set","key":"<path>","value":<v>,"flag":""}` | write |
| StartNewGame | `{"action":"StartNewGame","data":{...}}` | write (not needed) |

- `Register` is additive. The server immediately sends the current values
  of the newly registered paths, then changes as they happen. A path is a
  prefix match, and `*` works as an id wildcard, e.g.
  `ScoreBoard.CurrentGame.Clock(*).Running`.
- `Ping` every 30 s (the browser client does the same). Reply is
  `{"Pong":""}`. It also marks the device as recently seen. CLOCK pings
  after 5 s with nothing received and drops the connection after 12 s of
  silence (2026-11-13), so a lost Wi-Fi shows within about 12 s.
- `Register` is answered with one `state` frame holding every current value
  of the new paths (the minimal list below: 1.6 KB).
- `Set`: `value` is converted with `toString()` on the server, so `true`
  and `"true"` are equivalent. `null` deletes. `flag` is `""`, `"change"`
  (value is a delta, e.g. `-1000` on a clock time) or `"reset"`.
- **Commands** are sent as a `Set` on the command key with value `true`.
  Anything that does not parse as boolean true is ignored.

## 4. Server to client messages

| Frame | Meaning |
|---|---|
| `{"state":{"<path>":<value>, ...}}` | Batch of changed values. `null` = key removed. |
| `{"Pong":""}` | Ping reply |
| `{"authorization":"Not authorized for Set"}` | Write refused, nothing was changed |
| `{"error":"Unknown Action '...'"}` | Bad action |

First frame after connect is a pseudo update:

```json
{"state":{"WS.Device.Id":"...","WS.Device.Name":"...",
          "WS.Client.Id":"...","WS.Client.RemoteAddress":"..."}}
```

There is **no acknowledgement** of a `Set`. Success shows up only as state
changes. A refused write is the `authorization` frame. A press the server
silently ignores (see 7) produces nothing at all. So the firmware needs a
"no confirming state change within N ms" timeout per press.

## 5. Authorization

`Device.mayWrite()` = device `MayWrite` flag, or (connection from
127.0.0.1 and `AllLocalDevicesWrite`).

- `ScoreBoard.Clients.NewDeviceWrite` defaults to **true**: a new device
  gets `MayWrite=true` when first seen. With the default setting the timer
  can write immediately.
- `ScoreBoard.Clients.AllLocalDevicesWrite` also defaults to true, but only
  covers connections from 127.0.0.1, so never the handheld. (It also means a
  refused write cannot be tested from the server's own machine unless this
  is turned off.)
- Captured with both off: a `Set` is answered with
  `{"authorization":"Not authorized for Set"}` and changes nothing;
  `Register` and `Ping` keep working. Granting write
  (`ScoreBoard.Clients.Device(<id>).MayWrite` true, from Settings > Clients)
  reaches the device at once as a state change on that key.
- If the operator turned that off, the device must be enabled per device in
  Settings > Clients. This is tied to the cookie from section 2.
- Own status can be read back: register
  `ScoreBoard.Clients.Device(<WS.Device.Id>).MayWrite` and use it to show
  NOT AUTHORIZED before the first press instead of after it.

## 6. Keys

All under `ScoreBoard.CurrentGame.` (shortened to `G.` below).

### Commands (Set with value true)

| Key | Effect |
|---|---|
| `G.StartJam` | Start jam. Ignored if already in a jam. |
| `G.StopJam` | In jam: stop jam, lineup starts. Timeout running: end timeout, lineup starts. Nothing running: start lineup (or overtime lineup). Ignored while a plain lineup runs. |
| `G.Timeout` | Start a timeout (untyped). During a timeout: starts a new timeout. |
| `G.OfficialTimeout` | Start/mark an official timeout |
| `G.ClockUndo` | Undo the last clock action |
| `G.ClockReplace` | The undo button used by the eJT and SBO screens: undoes and lets the next button press replace the action. Label is `Label(Undo)`. |
| `G.Early5` | eJT "jam in N": schedules the five-second call early. Forces setting `AutoStart5=Jam`. |
| `G.Team(1).Timeout`, `G.Team(2).Timeout` | Team timeout |
| `G.Team(1).OfficialReview`, `G.Team(2).OfficialReview` | Official review |

### Clocks

`G.Clock(<id>).<prop>`, ids: `Period`, `Jam`, `Lineup`, `Timeout`,
`Intermission`.

| Prop | Type | Notes |
|---|---|---|
| `Time` | ms | Value to display. |
| `InvertedTime` | ms | Counts the other way |
| `MaximumTime` | ms | |
| `Direction` | bool | true = counts down |
| `Running` | bool | |
| `Number` | int | Period / jam number etc. |
| `Name` | string | |

Clock commands also exist (`Start`, `Stop`, `ResetTime`) but the jam timer
should use the game commands above.

`Time` updates are pushed when the displayed second changes, not every tick.
With the `ScoreBoard.Clock.Synch` setting on, all clocks change second
together. It is off by default, and the capture shows each clock ticking on
its own phase (e.g. Period at .3 s, Lineup at .9 s after a press).

The Timeout clock keeps `Running` true after a timeout ends: through the
following "Post Timeout" lineup (`Clock(Lineup).Name` = `Post Timeout`)
until the next jam starts. Do not use `Clock(Timeout).Running` to decide
that a timeout is in progress; use `Label(Stop)` = `End Timeout`. Local interpolation between updates is therefore needed only for
sub-second smoothness; a plain "show last received value" display is already
correct to the second.

Display rounding used by the frontend: count-up shows floor(ms/1000),
count-down shows ceil(ms/1000).

### Game state

| Key | Type | Use |
|---|---|---|
| `G.InJam` | bool | Jam in progress |
| `G.InPeriod`, `G.InOvertime` | bool | |
| `G.NoMoreJam` | bool | Period clock out, no further jam |
| `G.OfficialScore` | bool | When true, all clock commands are ignored |
| `G.TimeoutOwner` | string | `""` (untyped or none), `O` (official), or a team id. Team ids are `<game id>_1` / `<game id>_2`, not `1` / `2`: compare with `G.Team(1).Id`, `G.Team(2).Id` |
| `G.OfficialReview` | bool | |
| `G.FiveSeconds` | bool | Five-second call active |
| `G.AutoFive` | bool | |
| `G.Label(Start)`, `Label(Stop)`, `Label(Timeout)`, `Label(Undo)`, `Label(Replaced)` | string | Current meaning of each button |

Label values: `Start Jam`, `Stop Jam`, `End Timeout`, `Lineup`, `Timeout`,
`New Timeout`, `Overtime Lineup`, `---`, `No Action`, and `Un-<action>` for
undo. Captured per state:

| State | Label(Start) | Label(Stop) | Label(Timeout) |
|---|---|---|---|
| Nothing running (before the first jam) | `Start Jam` | `Lineup` | `Timeout` |
| Lineup, also post-timeout lineup | `Start Jam` | `---` | `Timeout` |
| Jam | `---` | `Stop Jam` | `Timeout` |
| Timeout | `---` | `End Timeout` | `New Timeout` |

`Label(Undo)` is `---` when there is nothing to undo, `Un-<action>` (e.g.
`Un-Stop Jam`) otherwise, and `No Action` while a replace is pending
(section 7); `Label(Replaced)` then names the undone action. `Label(Stop)` tells the device what the crown will do next; show it
instead of hard-coding the logic.

### Minimal register list for the device

```json
{"action":"Register","paths":[
 "ScoreBoard.CurrentGame.Clock(*).Time",
 "ScoreBoard.CurrentGame.Clock(*).Running",
 "ScoreBoard.CurrentGame.Clock(*).Direction",
 "ScoreBoard.CurrentGame.Clock(*).Number",
 "ScoreBoard.CurrentGame.InJam",
 "ScoreBoard.CurrentGame.NoMoreJam",
 "ScoreBoard.CurrentGame.OfficialScore",
 "ScoreBoard.CurrentGame.TimeoutOwner",
 "ScoreBoard.CurrentGame.OfficialReview",
 "ScoreBoard.CurrentGame.FiveSeconds",
 "ScoreBoard.CurrentGame.Label(*)",
 "ScoreBoard.CurrentGame.Team(*).Id"
]}
```

Plus `ScoreBoard.Clients.Device(<id>).MayWrite` once the id is known.

## 7. Server-side press guard (matters for the button design)

`GameImpl.quickClockControl`, threshold 1000 ms. It looks at the three clock
buttons Start, Stop and Timeout (team timeouts and reviews go through
Timeout when no timeout is running). A press only counts for the guard when
the button applies in that state: Start Jam during a jam or Stop Jam during
a lineup is refused before the guard and does not reset its timer.

Second press within 1 s of the previous one:

| Previous, then | Result (captured) |
|---|---|
| same button | ignored, silently |
| Start Jam, then Timeout | correction: the jam start is undone, the timeout starts instead |
| Stop Jam, then Timeout | correction: the jam stop is undone, the timeout ends the jam instead |
| Start Jam, then Stop Jam | **ignored**, the jam keeps running (but see the first jam below) |
| Stop Jam, then Start Jam | **ignored**, the lineup keeps running |
| any, after 1 s or more | normal action |

Exception, captured 2026-11-10 by the client live test: Stop Jam within
1 s of the **first** jam's start is a correction. The jam start is undone
(jam number back to 0, `Label(Undo)` `Un-Lineup`) and a lineup starts.
Before the first jam nothing runs, so it is not "a plain lineup".

The two "ignored" rows come from `canReplace`: a Start cannot replace an
action taken during a jam, and a Stop cannot replace one taken during a
plain lineup. So a fast stop-start or start-stop never shows up on the
scoreboard, and nothing tells the device. The device must treat it like any
other press without a confirming state change (timeout per press) and
should probably block the crown for 1 s after a crown press, with a visible
cue, so the official knows to press again.

The guard is global across all clients: a press on the device within 1 s
of a press on the operator laptop is subject to the same rule.

### Undo and replace

- `G.ClockUndo` (the SBO's default undo) restores the state before the last
  clock action, and the clocks run on at once.
- `G.ClockReplace` (the eJT undo, and the SBO's with its Replace setting)
  restores the same state but then **stops the server's clock**: every game
  clock is frozen and sends no `Time` updates. `Label(Undo)` becomes
  `No Action` and `Label(Replaced)` the undone action. The next clock button
  takes effect at the moment of the undone press, and the clocks then catch
  up in a burst of `Time` updates (captured: four Jam/Period updates within
  4 ms). Pressing undo again instead (`No Action`) keeps the restored state
  and lets the clocks run.
- While a replace is pending, the 1 s guard is off.
- For the device: after a ClockReplace undo, show the frozen state
  ("choose the replacement") rather than counting locally, and have a way
  out (undo again = No Action).

## 8. What eJT is

eJT = the existing electronic jam timer screen at `/nso/jt/`. It is a
normal web client, not a server role. It expects these global settings
("eJT defaults", set by its own button):

| Setting key (`ScoreBoard.Settings.Setting(...)`) | eJT value |
|---|---|
| `ScoreBoard.AutoStart` | `Timeout` |
| `ScoreBoard.AutoStart5` | `Jam` |
| `ScoreBoard.Auto5` | `true` |
| `ScoreBoard.AutoEndJam` | `false` |
| `ScoreBoard.AutoEndTTO` | `true` |

Server defaults (`SettingsImpl.setDefaults`): AutoStart `""`, AutoStart5
`""`, Auto5 `false`, AutoEndJam `false`, AutoEndTTO `true`. So a fresh
server differs from the eJT values in the first three.

It warns that these are global and affect the SBO. The handheld is in
effect a hardware eJT, so it should follow the same conventions: read these
settings, warn on the display if they differ, and never change them
silently.

The eJT screen also has features the three-button device does not cover:
timeout type (team TO / OR / OTO), retained review, +/- 1 s clock
corrections, injury continuation, Auto 5 / Early 5. Decide which of these
the device needs; timeout typing is the main gap, since `G.Timeout` alone
leaves the timeout untyped for the SBO to classify.

## 9. Corrections to the handover

| Handover said | Source says |
|---|---|
| Fetch an HTTP session first | Optional. The WS upgrade creates the session. Persisting the cookie is what matters. |
| Device must be authorized in Settings > Clients | Only if `NewDeviceWrite` was turned off. Default allows new devices. |
| Hold-to-confirm undo, firmware debounce against double press | Server has its own 1 s same-button guard and replace-on-different-button behaviour. |
| eJT assumed to be a role | It is the `/nso/jt/` screen plus a set of global automation settings. |
| Interpolate clock between updates | Updates arrive once per displayed second; interpolation is optional polish. |
| "Slow or missing response must be visible" | Confirmed necessary: there is no ack frame for Set. |
| (protocol.md before the capture) A different button within 1 s is a correction | Only Timeout after Start/Stop. Start right after Stop, or Stop right after Start, is silently ignored (section 7). |
| (protocol.md before the capture) Timeout clock running = in a timeout | The Timeout clock runs on until the next jam. Use the labels. |
| (protocol.md before the capture) TimeoutOwner is a team id like 1 | It is `<game id>_1`; compare with `Team(n).Id`. |
| (handover 11) `SettingsImpl` forces UsePBT back to false | UsePBT needs UseLT (lineup tracking): setting UsePBT true sets UseLT true, setting UseLT false sets UsePBT false. |

## 10. Crown mapping in connected mode

Follow the labels (captured values in section 6) instead of the clocks,
since the Timeout clock runs on into the next lineup:

| Labels | Crown sends | Display |
|---|---|---|
| `Label(Stop)` = `Stop Jam`, `End Timeout` or `Overtime Lineup` | `StopJam` | the Label(Stop) text |
| else `Label(Start)` = `Start Jam` | `StartJam` | Start Jam |
| else | nothing | --- |

Stop must win over Start for `Overtime Lineup`: captured at the end of the
last period with a tied score, both labels are offered, and StartJam then
only adds a jam to period 2 (no overtime lineup, `InOvertime` stays false).
StopJam starts the overtime lineup (`InOvertime` true, `NoMoreJam` true),
after which the crown starts the overtime jam as usual. `Lineup` (nothing
running, e.g. before the first jam) is left to StartJam, as the eJT's big
button does.

Period end, captured:
- A jam running when the period clock runs out continues; Stop Jam then
  starts the intermission (`Clock(Intermission).Running`, `InPeriod` false,
  labels Start Jam / Lineup, or Start Jam / Overtime Lineup after the last
  period with a tied score).
- Start Jam during an intermission depends on the time left: in the first
  half it **extends the previous period** (period number and clock stay,
  the jam is added to it; meant for a period ended too early), in the
  second half it starts the next period (`_endIntermission`). After the
  last period it always extends it. For the device: a crown press during an
  intermission or after the last period has a big effect; consider a hold
  or a confirmation there. Decided 2026-11-10: a 1 s hold (HANDOVER section 6).

Timeout button: `Timeout` (Label(Timeout) says Timeout or New Timeout),
then the type from the touch screen: `Team(n).Timeout`,
`Team(n).OfficialReview`, `OfficialTimeout`. Sent during a running timeout
these only type it (captured: an untyped timeout becomes a team timeout and
the team's Timeouts count drops; an OR changed to OTO gives the team its
review back).

Undo button: `ClockReplace`, like the eJT, with the behaviour in section 7.

Use `ScoreBoard.CurrentGame.<command>` like the eJT: it follows the game the
operator has made current. The SBO sends the same commands on
`ScoreBoard.Game(<id>).<command>`.

## 11. What the capture showed (2026-11-10)

- Set to first state change: 2 to 40 ms on localhost; the state frame
  carries every key the action changed at once.
- First frame and Register reply as in sections 4 and 3.
- The eJT registers about 70 paths (with duplicates) and gets 3.3 KB on
  load. The SBO registers 718 paths (298 unique) and gets 120 KB: the
  device's minimal list is the right size.
- The eJT sends Ping every 30 s and reconnects 1 s after a close.
- `Clock(Intermission).Time` is 86400000 (24 h) before the game starts.
- Clock corrections from the eJT: `Set` `G.Clock(Period).Time` value
  `-1000` flag `change`.
- Idle timeout: a connection with no traffic is closed by the server after
  exactly 300 s (Jetty default; close code 1001, reason
  `java.util.concurrent.TimeoutException: Idle timeout expired`). Any
  regular Ping keeps it well inside that. Note that a device whose registered
  clocks are all stopped receives nothing, so it must ping even when the
  scoreboard is quiet.

## 12. Penalty box timing (box timer roles)

Read from the source at 3ba7f744 (`BoxTripImpl`, `FieldingImpl`,
`PositionImpl`, `PenaltyImpl`, `GameImpl.jammerBoxEntry`, `/nso/pbt/`) and
confirmed on 2026-11-10 against a local server with
`tools/crg-capture/box.py` (UseLT and UsePBT on, a game with rosters).
`G` = `ScoreBoard.CurrentGame.`

### Setting

`ScoreBoard.Settings.Setting(ScoreBoard.Penalties.UsePBT)` must be `true`
(it turns on `...UseLT` too). While it is on, the lineup tracker can no
longer set `Skater(*).PenaltyBox`: the box timer owns the box. The device
should show a clear message when it is off rather than send commands.

### Where trips live

A trip is created as `G.BoxTrip(<uuid>)` and moves to
`G.Team(n).BoxTrip(<uuid>)` (same id) as soon as it gets a fielding. The old
keys are removed. **A `Set` on a stale path creates a new, empty trip**
(GameImpl creates children on WS writes; seen in the capture), so always
build the key from the current mirror. Register both
`G.BoxTrip(*)` and `G.Team(*).BoxTrip(*)`.

Finished trips stay in the state with `IsCurrent` false and no `Clock`.
Show only `IsCurrent` true.

Trip keys the device needs: `IsCurrent`, `Jammer`, `RosterNumber`
(`"?"` when the position has no skater), `CurrentSkater`, `CurrentFielding`,
`PenaltyCodes`, `TotalPenalties`, `TimingStopped`, `Shortened`,
`EndJamNumber`, `Clock.Time`, `Clock.MaximumTime`, `Clock.Running`.

Position keys (`G.Team(n).Position(Jammer|Pivot|Blocker1..3).`):
`Skater`, `RosterNumber`, `CurrentFielding`, `PenaltyBox`, `HasUnserved`,
`PenaltyTime`. Plus `G.Team(n).StarPass` (after a star pass the jammer is
the Pivot position) and `G.InJam`.

### Commands

| Action | Set (value true unless given) | Seen |
|---|---|---|
| Skater sat, position known | `G.Team(n).Position(p).StartBoxClock` | trip on that fielding, clock 30 s |
| Blocker sat, who unknown yet | `G.StartBoxTrip` | trip without fielding, clock already running |
| ...then assign | `G.BoxTrip(id).CurrentFielding` = value of `Position(p).CurrentFielding` (`<teamjam>_<jam>_<Position>`) | trip moves to `Team(n)` |
| Jammer sat, team unknown | `G.StartJammerBoxTrip` | see jammer swap |
| + Penalty (code ?) | `G.Team(n).Skater(id).Penalty(<PenaltyCount+1>).Code` = `"?"` | penalty linked to the trip, clock +30 s, codes `? ?` |
| Pause one clock | `<trip>.TimingStopped` true / false | clock stops / restarts (only in a jam) |
| Release | `G.Team(n).Position(p).PenaltyBox` false | trip `IsCurrent` false, clock removed |
| Undo a release | `G.Team(n).Position(p).UnendBoxTrip` | trip current again with its clock |
| Cancel trip | `<trip>.Delete` | trip gone (see the note below) |
| Clock correction | `<trip>.Clock.Time` value ms, flag `change` | |

This is exactly what the PBT page sends: the "Skater Sat Down" button in
its position dialog is `StartBoxClock`, its "Add" penalty writes
`Penalty(n+1).Code`, and a tap on a trip's position button sets
`CurrentFielding`.

### Clock behaviour

- A box clock runs only while a jam runs. A skater sat between jams gets a
  trip on the upcoming jam (`StartBetweenJams` true) with the clock
  standing at 0:30 until the jam starts. When the jam ends all box clocks
  stop. The device must show this waiting state; "the clock starts on the
  crown press" is only true during a jam.
- Length: rule `Penalties.Duration` (30 s default) per penalty;
  `Clock.MaximumTime` grows by that per linked penalty.
- At 0 in a jam the trip is ended (`EndJamNumber` set) but stays
  `IsCurrent` and in the box, and `Clock.Time` keeps running **below zero**
  (-1000, -2000, ...). The skater leaves only on a release. This matches
  the handover's "release is always a deliberate tap".
- Released early: the remaining time is stored on the skater as
  `Skater.ExtraPenaltyTime` (seen 25000), `HasUnserved` stays true, and the
  next trip of that skater adds it to the clock.
- Released in the same interjam it started, with no jammer swap: the trip
  is deleted (source, not captured).

### Penalties

- A skater who sits **without** a noted (unserved) penalty gets one
  automatically, code `?` (`PenaltyImpl` default). Seen: `codes='?'`.
- A skater **with** noted penalties gets them all linked, no `?` added.
  Seen: noted `B`, trip codes `B`, clock 30 s.
- Linked penalties show `Served` true and `Serving` true while the trip is
  current.
- Cancel (`Delete`) removes the trip but **not** the automatic `?`
  penalty: it stays on the skater as unserved (`Served` false), so the
  skater shows up as having a noted penalty. The CRG PBT page behaves the
  same.
- `Position.HasUnserved` is the "noted penalty" highlight for the picker.

### Jammer swap

- Both jammers known (`StartBoxClock` on each team's Jammer position, each
  with a skater on the lineup): when the second one's penalty is linked,
  CRG shortens both. Seen: team 1 jammer sat, 5 s later team 2 jammer.
  Team 1's clock dropped to max 5 s (already at or below 0, trip ended,
  still current until released); team 2's to max 5 s (serves what team 1
  served). Both `Shortened` 1. A further `+ Penalty` on team 2's jammer
  added 30 s (max 35 s) with no new shortening.
- The swap is triggered by the penalty, so **a jammer trip on a position
  with no skater gets no penalty and no swap** (seen: both `#?`, both at
  full time). The swap follows as soon as the lineup tracker fills in the
  skater.
- `StartJammerBoxTrip` twice, no positions: basic swap, the first clock
  ends, the second serves the time the first had served. With one team's
  jammer already in the box, it assigns the new trip to the other team's
  jammer.
- For the jammer role the device knows the team (left or right button), so
  it should send `Position(Jammer)` (or `Pivot` after a star pass)
  `.StartBoxClock`, not `StartJammerBoxTrip`.

### Checked later (2026-11-10, `tools/crg-capture/box2.py`)

- Tick rate: a box `Clock.Time` update once per second, on whole seconds
  (29000, 28000, ...), as the game clocks.
- Changing the skater of a running trip (`<trip>.CurrentFielding` set to
  another position's fielding, the PBT's and the device's "Change"): the
  trip moves to the new skater and keeps its time; the automatic "?"
  penalty moves with it; the old position leaves the box.
- Change after "+ Penalty": the trip falls back to one penalty (30 s, time
  on the clock reduced by 30 s); the added "?" penalty stays on the first
  skater as unserved, and the new skater gets a fresh automatic "?". So a
  "+ Penalty" is lost from the trip and would come back at the first
  skater's next trip. CRG behaviour, the same on its PBT page.
- Undo with a box clock running: `ClockReplace` after a Stop Jam restores
  the jam and the box clock runs again; while the replace is pending the
  box clock is frozen like the game clocks, and catches up after "No
  Action". `ClockUndo` restores it too. Box clocks follow the undo.
- Not checked: `StartEarlier`/`StartLater`/`EndEarlier`/`EndLater`
  (lineup tracker corrections, not used by the device).
