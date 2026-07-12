# Understanding Your Own Codebase — CardSat in Plain Language

*A tour of what the pieces are and what they do, written for the person who built CardSat
but doesn't write C++. No prior programming knowledge assumed. The goal is that you can open
any file, read this, and know roughly what you're looking at — so it stops feeling like a
black box.*

---

## First, the big picture

CardSat is **firmware** — software that runs directly on the little computer inside the
M5Stack Cardputer, with no operating system like Windows underneath it. When you flash the
device, you're loading this program into it, and it takes over the whole machine: the screen,
the keyboard, the radio, the WiFi, everything.

The program is written in a language called **C++**. Think of the code as a very large,
very literal instruction manual that the chip follows thousands of times per second. Every
file you'll see is part of that manual, organized by topic — one file for the radio, one for
the map math, one for talking to the internet, and so on.

There are two forms of the same program in your project, and this trips people up, so let's
clear it up first.

### The two copies: `src/` and `CardSat.ino`

- **`src/` folder** — the code split into many small, tidy topic files. This is the "organized"
  version, nice for reading and maintaining.
- **`CardSat.ino`** — the *entire* program flattened into one enormous single file. This is the
  version that the simple Arduino flashing tool actually compiles and loads onto the device.

They contain **the same code**. The `.ino` is just all the `src/` files poured into one bucket
so the Arduino IDE can swallow it. This is why, throughout development, every change had to be
made **in both places, identically** — miss one and the two copies disagree. The little checker
scripts (`balance.py`, `parity.py`) exist precisely to confirm the two stayed in sync after
every edit. When you saw "gate green, mirror-identical," that was the confirmation that the
tidy version and the flattened version matched byte-for-byte.

Don't worry about *why* it's done this way — just know that `src/` is for humans and `CardSat.ino`
is for the flashing tool, and they're twins.

---

## How the device actually runs (the two heartbeats)

Almost every device like this works on a simple rhythm with just two parts:

1. **Setup** — runs *once*, the moment the device powers on. It turns on the screen, starts
   WiFi, loads your settings and satellite list, warms things up. In the code this lives in a
   section called `setup()`.

2. **Loop** — runs *over and over*, forever, thousands of times a second, for as long as the
   device is on. Each trip through the loop it asks: *Did a key get pressed? Is it time to
   update the screen? Is a satellite passing? Should I nudge the radio's frequency?* — and acts
   on the answers. This lives in `loop()`.

That's the whole engine. Setup gets everything ready; the loop is the device "living its life"
one tiny tick at a time. When we added the on-demand audio, or the pass-search that runs "one
pass per loop so the screen stays responsive," we were fitting work into that repeating
heartbeat so no single tick takes too long and freezes the device.

---

## The map: what each file is for

Here are the main files, biggest and most important first, in plain terms. (Line counts are
rough size indicators — bigger means more lives there.)

### The heart

- **`app.cpp`** (~24,000 lines) — **this is CardSat.** The overwhelming majority of the program
  lives here: every screen you see, every key you press, the menus, the world map, the polar
  plots, the pass lists, the workable-horizon and target-search features, the games, the
  settings, the web-control server. When we spent whole sessions on the workable-horizon sweep
  or the on-demand audio, we were almost always in this file. If CardSat were a building,
  `app.cpp` is nearly the entire building.
- **`app.h`** (~1,800 lines) — the "table of contents and inventory" for `app.cpp`. It lists
  what exists (every screen, every function, every stored value like `audioUp` or the color
  palette) without the full details. Programs use these "header" files so different parts can
  find each other. Whenever you saw a change to a `.h` file, it was usually declaring that
  something *new exists*, with the actual behavior added in the matching `.cpp`.

### Talking to satellites (the math)

- **`predict.cpp` / `predict.h`** — the **orbit brain.** This is where CardSat figures out
  where a satellite is, when it will rise and set, how high it'll get, and how fast it's moving
  toward or away from you (which is what Doppler correction needs). It uses a well-known,
  decades-old orbital-mechanics method called SGP4. The infamous "nextpass re-init" bug we
  fought lived at the seam between this file and the orbit library it leans on.
- **`satdb.cpp` / `satdb.h`** — the **satellite database.** Holds the list of satellites in
  memory, reads the orbital-element files (the modern "GP/OMM" data that replaced old "TLE"
  data), and keeps track of your favorites. This is the file that reads `gp.json` line by line
  so it never has to hold the whole thing in memory at once.
- **`location.cpp` / `location.h`** — your **position on Earth:** GPS handling, grid-square
  (Maidenhead) conversions, the observer location the math is done from.

### Talking to radios and rotators (the hardware)

- **`rig.cpp` / `rig.h`** — the **generic radio interface.** A common language the rest of the
  program uses to say "set the frequency" or "switch to satellite mode," without caring which
  brand of radio is attached.
- **`civ.cpp` / `civ.h`** — **ICOM radios specifically.** ICOM uses a control protocol called
  CI-V; this file speaks it. The IC-821H bench-radio work, the CI-V command tables — all here.
- **`kenwood.cpp` / `kenwood.h`** and **`yaesu.cpp` / `yaesu.h`** — the same idea for Kenwood
  and Yaesu radios.
- **`radio_profiles.h`** — a lookup table of the quirks of specific radio models.
- **`rotator.cpp` / `rotator.h`** — **antenna rotators:** pointing an az/el rotator at the
  satellite as it crosses the sky, including the Yaesu GS-232 command set.
- **`icomnet.cpp` / `icomnet.h`** — controlling certain ICOM radios over the **network** (LAN)
  rather than a cable.

### Talking to the internet

- **`net.cpp` / `net.h`** — the **networking file.** Downloads orbital data and space-weather
  info, and — the part we spent so long on — uploads your logged contacts to LoTW and Cloudlog
  over a secure (TLS/HTTPS) connection. The "TX stall" and "after POST" heap logs we added for
  the upload investigation live here. This file is where the no-PSRAM memory constraints bit
  hardest, because a secure connection needs a big contiguous chunk of memory.
- **`lotw.cpp` / `lotw.h`** — building and **cryptographically signing** the `.tq8` file that
  LoTW requires, so ARRL's system trusts that the contacts really came from you. This is
  genuine cryptography (the same kind of signing a web browser does), done on a tiny chip.
- **`lotw_subdiv.h`** (~2,200 lines) — a big reference table (states/provinces and the like)
  that LoTW needs. It's mostly data, not logic.

### Radio-to-radio messaging (no internet)

- **`lora.cpp` / `lora.h`** and **`lorarx.cpp` / `lorarx.h`** — **LoRa**, a long-range,
  low-power radio link that lets two CardSat units (or compatible gear) send small messages and
  share satellite data directly, without any internet.
- **`irbeacon.cpp`** — an infrared beacon feature.

### Awards, logging, notes, storage

- **`notes.cpp`** — your per-satellite notes.
- **`voicememo.cpp` / `voicememo.h`** — recording and playing short voice memos through the
  speaker and microphone (which share hardware — the reason the audio-memory handling needed
  care).
- **`storage.cpp`** — reading and writing files on the microSD card (the `/CardSat` folder).
- **`settings.cpp` / `settings.h`** — your saved preferences, and loading/saving them.
- **`config.h`** — the **master settings-and-constants file.** Fixed values that shape the whole
  program: the firmware version number, how many satellites can be held in memory, buffer sizes,
  and the caps that shaped so many decisions (like `MAX_SATS`). This is the kind of file where
  a single number changes behavior everywhere. (One famous dial — the display's color depth,
  `CANVAS_DEPTH = 4`, which halved the screen buffer's memory — actually lives in `app.cpp`'s
  startup code rather than here, but it's the same idea.)

### Reference data (mostly tables, not logic)

- **`dxcc_lookup.h`**, **`ituzones.h`**, **`cqzones.h`** — big lookup tables: countries/entities,
  ITU zones, CQ zones. These are the "facts about the world" the award and map features consult.
  They rarely change; they're just *known*.

---

## A few ideas that came up constantly — demystified

Because these shaped so much of our work, here's what they actually mean:

**"Heap" and "memory fragmentation."** The chip has a small pool of working memory (the "heap").
Imagine a parking lot. "Free memory" is how many empty spaces exist in total. But some things —
like a secure internet connection — need a *row of adjacent* empty spaces, not just scattered
ones. Over time, as things park and leave, the lot gets patchy: plenty of spaces, but no long
unbroken row. That's *fragmentation*, and it's why uploads failed even when there was "enough"
memory. The 4-bits-per-pixel screen change worked because it permanently freed up a big block,
widening the longest available row. This is *the* recurring theme of the whole project, because
the chip has no extra memory chip (no "PSRAM") to fall back on.

**"Palette" and the screen.** Rather than storing a full color for every pixel, CardSat keeps a
short list of 13 colors (the "palette") and stores each pixel as a small number pointing into
that list. That's why halving the storage per pixel (8 bits to 4 bits) changed *nothing* about
how it looks — the colors were always just list positions, and the list didn't change.

**"Streaming."** Instead of loading a whole big file into memory (which the chip can't afford),
CardSat reads and handles it in small pieces — a chunk at a time — like sipping through a straw
instead of swallowing a bucket. The file downloads and the web-file downloads both work this
way, which is why they cost almost no memory.

**"Jobbed" work / "one per loop."** Some tasks (like scanning ten days of passes for every
favorite) are too big to do all at once without freezing the screen. So they're broken into
tiny steps, one step per heartbeat, with progress shown as it goes. That's why the workable
horizon has a progress bar and stays responsive.

**"Gate: balance / parity."** Small automated checks that ran after every change, confirming the
code's structure was sound (all brackets matched) and that the two copies (`src/` and the
`.ino`) still agreed exactly. "Gate green" meant "safe to flash."

---

## What to do with this

You don't need to read the C++. But now, if you open `civ.cpp`, you know it's the ICOM radio
conversation; if you open `net.cpp`, you know it's the internet and uploads; if something's
wrong with the map, you know it's somewhere in `app.cpp`; and if you're worried about memory,
you know `config.h` is where the big dials are.

That's what "knowing your own house" means here. You built this. You don't have to have laid
every brick to walk through it confidently and know which room is which. And if you ever want a
deeper tour of any single room — how a pass is predicted, how Doppler is applied, how the upload
signing works — that's a conversation you can have any time, one room at a time.
