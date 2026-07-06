# Scoping: orbit parameter tool, orbit-animation Learn screen, and a PikaScript interpreter

**Status:** scoping only -- no code written. Feature 3 (interpreter) is a **do-not-build**
recommendation on heap grounds; the reasoning is below so the call is auditable.

Lens throughout: **flash is cheap (8 MB, plenty free), heap is the scarce resource.** This
is a no-PSRAM ESP32-S3; the codebase already carries ~13 distinct no-PSRAM workarounds, a
permanently-allocated 63 KB canvas sprite, and a field-observed largest-contiguous-block
that falls to ~14-20 KB after TLS. Every feature below is judged first on heap.

---

## 1. Orbit parameter explorer (new orbit tool) -- **RECOMMEND BUILD (heap-safe)**

**Idea:** a screen where you vary one Keplerian element at a time and watch the derived
orbital characteristics update -- "what does raising apogee do to period / velocity /
footprint / coverage time?" A teaching-and-planning instrument, not another live tracker.

**Why it's heap-safe:** it is arithmetic into text and a few drawn lines, exactly like the
existing Tools form engine and drawOrbit. All six elements already live in `SatEntry`
(incl, ecc, raan, argp, ma as float; meanMotion as double). The derived quantities are
closed-form:
- semi-major axis from mean motion (Kepler's third law), apogee/perigee from a and ecc,
  period, orbital velocity at apo/peri (vis-viva), nodal-regression and apsidal-precession
  rates (J2), footprint radius and max pass duration from altitude.
- No new buffers, no allocation in the draw loop, no network. Reuses `canvas` and the
  existing footer/scroll idiom. Cost: a few KB of flash, ~0 heap.

**Two viable shapes:**
- **A. Form-tool style** (cheapest): a numeric form (period or apogee, ecc, incl) with a
  scrolling results panel -- drops straight into the existing tool engine, no new screen
  plumbing, inherits value persistence and units discipline. **The orbital outputs stay
  metric always** (per the standing constraint: altitudes/distances/velocities are km,
  km/s -- never imperial), independent of the antenna-length units setting.
- **B. Dedicated screen with a mini side-view sketch**: same math plus a small 2D ellipse
  drawn against an Earth circle that reshapes as you change ecc/apogee. More illustrative,
  slightly more draw code, still heap-flat (drawing primitives only). Could live as an
  extra page on the existing `drawOrbit` pager (currently 10 pages) rather than a new
  screen -- cheapest way to add the visual.

**Recommendation:** shape A as a Tools entry ("Orbit explorer") for the numbers, OR add it
as an 11th `drawOrbit` page if you'd rather keep all orbital analysis in one place. Either
is a modest, safe build. Flag: decide whether it edits a *hypothetical* orbit (pure
teaching, starts from typed values) or the *active satellite's* elements (planning aid,
pre-filled). Pre-filled is friendlier; hypothetical is clearer pedagogy. Could support
both (start from active sat, then free-edit).

## 2. Orbit-type animation Learn screen -- **RECOMMEND BUILD (heap-safe with discipline)**

**Idea:** off the Learn/About area, an animated explainer cycling orbit archetypes -- LEO,
MEO, GEO, Molniya/HEO, sun-synchronous, polar -- each showing a satellite tracing its path
around a drawn Earth, with a caption on what defines it and what it's used for.

**Why it can be heap-safe:** it renders into the *existing* canvas sprite; no second
framebuffer. The satellite position each frame is one vis-viva/Kepler evaluation -- cheap
math, no allocation. The one thing that would make it unsafe is caching a big trail/point
array or allocating per frame; avoided by (a) computing positions on the fly, (b) drawing a
fixed-length fading trail from a small static ring buffer (e.g. 32 points = a few hundred
bytes, statically sized, never grown), (c) no String churn in the loop.

**The real cost is the redraw cadence, not memory.** The loop already supports periodic
redraw (`ms - lastDrawMs > 500/1000`); an animation needs a faster tick (~15-20 fps ->
50-66 ms) while this screen is active, reverting on exit. That's a scheduling change local
to one screen, not a global one. Watch: keep the physical `pushSprite` the only per-frame
heavy op; the existing globe screen already proves 2D spherical projection at interactive
rates on this hardware.

**Scope shape:** a single `SCR_ORBITZOO` (or a mode on an existing Learn page) with `,`/`/`
to switch orbit type, a static table of {name, a, ecc, incl, caption, use-case}, and a
draw routine that projects the orbit ellipse + moving dot + short trail. All static/PROGMEM
data; flash cost a few KB, heap essentially flat if the trail buffer is fixed-size and
nothing allocates in the frame path. **This is buildable safely** -- the discipline is
"no allocation inside the animation loop," which the codebase already lives by.

**Open decisions:** how many orbit types (6 is a good set); whether to show a realistic
tilted 3D view (reuse globe projection, prettier, more math) or a clean 2D side/top view
(simpler, arguably clearer for teaching); trail length. Recommend 2D side view first --
clearest pedagogy, lowest risk -- with 3D as a possible later polish.

## 3. PikaScript interpreter in Tools -- **RECOMMEND DO NOT BUILD (fails the heap test)**

The request was explicit: *do not implement if there is possible cost to heap.* There is
not just possible cost -- there is near-certain cost that collides with the device's
tightest constraint. Laying out why, because a "no" deserves as much rigour as a "yes":

**a. It needs a contiguous heap arena this device often can't spare.** PikaPython runs on
its own allocator over a contiguous memory pool. A usable REPL (parse a script, build the
object graph, do string/list/dict work) needs tens of KB *contiguous*. The field-observed
largest free block here falls to ~14-20 KB after TLS/downloads, and the codebase already
fights (13 documented workarounds, per-batch reboots for LoTW, in-RAM GP streaming) to keep
single ~32 KB handshakes alive. Dropping a second large contiguous consumer into that arena
is exactly the pressure the whole design has been engineering *away* from.

**b. "Clear memory on leaving" does not make it safe.** Freeing the VM arena on exit
returns the bytes but hands them back **fragmented**: a large block is allocated, filled
with many small VM objects, then freed piecemeal -- the classic pattern that leaves the
heap pocked. On a part with no PSRAM and no compaction, the *largest contiguous block* can
stay depressed after the free even though total free recovers. So the very next TLS
handshake or transponder cache -- the operations already on the knife-edge -- can fail
*because* the interpreter ran earlier this session. The cleanup requested doesn't prevent
the damage; it just delays where the failure surfaces, which is worse (a mystery failure in
an unrelated feature).

**c. The safe way to run it isn't really "in Tools" at all.** The pattern this codebase
uses for heap-hostile operations (LoTW upload) is *run it in its own pristine boot and
restart after*. Applying that to an interpreter means: reboot into a bare REPL, no
tracker/radio/canvas, run, reboot back. That's a legitimate design -- but it's a separate
mode, not a Tools entry, it can't coexist with the running app (defeating the point of an
in-app tool), and it's a large amount of new surface (VM integration, editor, I/O binding,
persistence) for a feature far outside CardSat's mission of tracking and working amateur
satellites.

**d. Flash is fine; that's not the blocker.** ~32 KB+ of VM code in 8 MB flash is nothing.
This is purely a runtime-heap and fragmentation judgement. If the hardware ever gains PSRAM
(a different board), the calculus flips entirely and it becomes reasonable.

**Recommendation:** don't build it on this hardware. If the goal underneath the request is
"let users compute/automate on-device," the heap-safe way to serve most of that intent is a
small, *non-allocating* expression evaluator (fixed-size parse stack, doubles only, the
functions CardSat already needs -- trig, log, the link/orbit formulas) -- essentially an
extension of the existing scientific calculator, not a Python VM. That stays flat on heap
and fits the mission. Happy to scope that as an alternative if it's of interest.

---

## Summary

| Feature | Heap verdict | Recommendation |
|---|---|---|
| 1. Orbit parameter explorer | flat (math->text) | **Build** -- Tools entry or 11th Orbit page |
| 2. Orbit-type animation | flat *if* no per-frame alloc + fixed trail buffer | **Build** -- 2D side view first, discipline = no alloc in frame loop |
| 3. PikaScript interpreter | near-certain contiguous-arena + fragmentation cost | **Do not build** on this no-PSRAM part; offer a non-allocating expression evaluator instead |

Both recommended features render into the existing sprite, add only static/PROGMEM data,
and keep orbital quantities metric. The interpreter is declined specifically on the
condition attached to the request.
