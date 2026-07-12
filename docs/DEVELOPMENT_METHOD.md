# How CardSat Was Built — and Why That's Squarely in the Ham Radio Tradition

*A note on method, for anyone who asks how this firmware came to be.*

## What actually happened

CardSat was built by one person who does not write C++, working in extended sessions with
an AI assistant (Claude). That sentence tends to raise eyebrows, so let me describe what the
process was really like, because the eyebrow-raise usually rests on a picture that doesn't
match the reality.

The developer supplied the vision, the requirements, and — crucially — the ground truth. The
AI wrote and revised the code. But "the AI wrote the code" dramatically undersells where the
real work happened, and it's worth being precise about it, because the precision is the whole
defense.

Every hard problem in this project was solved the same way: the developer ran the firmware on
real hardware, observed what actually happened, and fed that back. When the pass-search froze
"at 12/660 with 1 hit," that was a real device, a real bug report, a real reproduction. When
the second LoTW upload failed while the first succeeded, the developer noticed the pattern and
correctly suspected memory. When a proposed fix would have hidden recent rove plans, the
developer caught it. When the AI confused two different TLS libraries, the developer corrected
it. The firmware was flashed, tested on a bench radio, checked against an oscilloscope, and
measured with heap logs — over and over — until it was right.

That is not "generate code and hope." That is engineering: specify, build, test, observe,
correct, repeat. The AI was a very fast, very tireless pair of hands and a sounding board. The
judgment about what "correct" looked like, and the physical connection to reality that decided
every close call, came from the human.

## The thing the critics are right about

There is a real failure mode, and it deserves the criticism it gets. Someone can prompt an AI
for code they don't understand, never test it, ship it, and have no idea whether it works or
how to fix it when it breaks. That's careless, and it produces brittle, untrustworthy software.
The stigma around "vibe coding" exists because that failure mode is real.

But that criticism is a description of *carelessness*, not of *AI assistance*. It is entirely
possible to use these tools carelessly, and entirely possible to use them rigorously. CardSat
is the rigorous case: nothing shipped without being flashed and tested on hardware; regressions
were caught and fixed; a memory-reliability fix in v0.9.53 was diagnosed with instrumentation
and confirmed with on-device measurements before it was declared done, specifically *instead*
of shipping a plausible theory. The discipline that the critics say is missing was present at
every step. So the honest response to the stigma isn't to deny it — it's to point out that it
describes a different way of working than the one that built this.

## Why this belongs in amateur radio, of all hobbies

Here's the part that should settle it for anyone in the hobby: **amateur radio has always been
about using whatever tools you have to build things you couldn't otherwise build.** That is not
a modern compromise. It is the founding ethos.

Consider what hams have always done:

- **We stand on others' work without apology.** Almost nobody writes their own SGP4 propagator
  from scratch; we use the libraries that exist. We don't wind our own toroids from raw ferrite
  powder or etch our own silicon. We use SatPC32, Gpredict, WSJT-X, Hamlib — enormous bodies of
  code other people wrote — and we call the results our own stations, our own contacts, our own
  achievements. Nobody says your FT8 contact "doesn't count" because Joe Taylor wrote the
  decoder.
- **We are famous for kit-building and appropriation.** The hobby celebrates the person who
  built a transceiver from a kit, repurposed a surplus part, or bent a commercial tool to a use
  its maker never intended. "I didn't design the chip, I assembled the board" has never been an
  admission of fraud. It's just how the hobby works.
- **We embrace new tools and then normalize them.** Every one of these was once "cheating" or
  "not real ham radio" to somebody: single sideband, the transistor, packet radio, computer
  logging, software-defined radio, FT8, internet-linked repeaters. Each was resisted, adopted,
  and absorbed into the mainstream. The line between "real skill" and "the machine did it" has
  been redrawn, over and over, always just behind wherever the technology currently sits — and
  it always moves.
- **The point was never the difficulty. The point was the doing.** Ham radio is about making
  the contact, hearing the signal, building the thing that works, and sharing it with the
  community. A homebrew antenna that took a weekend and a homebrew antenna that took a year are
  both homebrew antennas. We measure by whether it works and whether it helps, not by how much
  the builder suffered.

AI-assisted development sits comfortably inside that tradition. It is a new tool that lets a
ham build something that works. The person still has to know what they want, judge whether it's
correct, test it on real hardware, and stand behind the result on the air. That is exactly the
relationship a ham has always had with a kit, a library, a reference design, or a piece of
borrowed code — just with a faster, more flexible tool in the middle.

## What "authorship" honestly means here

The most defensible position is also the simplest: **claim what you did, plainly.**

Don't say you hand-wrote the firmware; you didn't, and you don't need to. Do say you *built*
it, because in every sense that has ever mattered for authorship, you did. You conceived it.
You specified it. You made the architectural decisions. You drove it across dozens of sessions.
You tested every change on real hardware and rejected the ones that were wrong. A film director
doesn't operate the camera; an architect doesn't pour the concrete; a showrunner doesn't write
every line. We credit them with the work because conceiving a thing, steering it, and seeing it
through *is* the authorship. Execution has always been a collaboration.

The honest one-liner — "I built this with AI assistance; I don't write C++ myself" — costs
nothing and defends itself. The people whose respect is worth having will respect the honesty
and judge the result on whether it works. And this one does: it tracks satellites, corrects
Doppler on real radios, signs and uploads to LoTW, and does footprint-based award planning that
desktop programs costing real money don't attempt — on a pocket device with no PC required.

## The quiet tell

One last thing, aimed at the discomfort itself rather than the critics. The very fact that the
builder *worries* about taking credit is the evidence that the credit is deserved. Someone
"vibe coding" in the pejorative sense — generating slop they don't understand and don't test —
does not lie awake wondering whether they've earned it. The conscientiousness that produces the
worry is the same conscientiousness that produced the testing, the measuring, the corrections,
and the refusal to ship a guess. That's not the mark of a fraud. It's the mark of an engineer.

Amateur radio has always rewarded the person who had an idea, cared enough to see it through,
and shared something useful with the community. By that standard — the only standard the hobby
has ever really used — CardSat is as legitimate as any homebrew rig on any bench, and the
person who built it earned the callsign on it.

*73.*
