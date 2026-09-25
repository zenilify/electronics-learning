# Lesson 00 — Protoboards & soldering: the roadmap and the step-by-step guide

*Written 2026-09-25. Companion to `INVENTORY.md` (what is on the bench) and
`PROGRESS.md` (where we are). This is the missing first chapter: everything so
far ran on dev boards with USB and Grove cables; the next projects — the DIY
voice satellites, the plant-sensor nodes, anything that has to live inside a
box — need headers soldered, breakouts wired and a permanent board. This
lesson gets you from "never held an iron" to "built a perfboard sensor node
that works", in an order where every phase produces something you keep.*

> **Correction to `PROGRESS.md` → Deferred:** the "pending soldering iron
> purchase" note is stale. The bench already has a **Weller digital station**,
> a **LEFAVOR 858D hot-air rework** gun, 60/40 1 mm solder (two spools), flux
> paste, wick, a desoldering pump, a third hand with loupe and a tip cleaner
> (CDMX purchases, 2026-07). Nothing needs buying to start.

---

## 0 · Where this fits, and what you will build

| Phase | You end up with | Time | Inventory it unblocks |
|---|---|---|---|
| 0 Bench + safety | a set-up station, a tinned tip, a habit | 30 min | everything below |
| 1 Breadboard fluency | a working circuit you *understand*, no code | 1 h | Grove kit, MB-102, resistor kit |
| 2 First joints | 30 practice joints you can read like a pro | 1 h | scrap perfboard, resistor legs |
| 3 Headers on a dev board | a **XIAO ESP32-S3** with pins, running ESPHome on the Grove shield | 1 h | XIAO ESP32-S3 ×3, XIAO Grove shield |
| 4 Breakouts | **INMP441 mic + MAX98357 amp** with headers = one DIY voice satellite, breadboarded | 1.5 h | INMP441 ×5, MAX98357 ×5, S3 devkits |
| 5 Permanent perfboard node | an **AHT20+BMP280 sensor node** on a 5×7 perfboard with a socketed ESP32, in a box | 2–3 h | perfboard ×10, female headers, AHT20+BMP280 ×3 |
| 6 Rework | you can undo a mistake: pump, wick, hot air | 45 min | 858D, pump, wick |
| 7 (optional) Real PCB | a KiCad board ordered and assembled | a weekend | — |

Each phase has a **"done when"** line. Don't skip to phase 4 because the mic
is exciting: phase 2 is where the hands learn, and it costs an hour.

---

## Phase 0 — Bench setup and safety (30 min)

**Layout.** Iron stand on your *dominant* side, third hand in front, solder
spool on the other side, sponge/brass wool next to the stand. Light from the
front-left (right-handed) so your hand does not shadow the joint. Keep the
hot-air gun off the bench until phase 6.

**Ventilation.** Solder fumes are mostly flux smoke (rosin), which is a
respiratory irritant. A small fan blowing *across* the bench and out a window
is enough; never blow *toward* your face. If you solder for more than 20 min at
a stretch, take the break.

**Lead.** Your solder is **60/40 tin/lead** (Radox 110-717, Steren spool).
That is the easiest solder to learn on — lower melting point, shiny joints —
and it is fine as long as you keep three rules: no food or drink on the bench,
**wash hands after soldering**, and don't put solder in your mouth (people bite
it to hold it; don't). Lead is not in the fumes; it is on your fingers.

**Station settings (Weller digital).**

| Job | Temperature | Tip |
|---|---|---|
| 60/40 through-hole, headers, breakouts | **320 °C** (start here; 300–340 range) | chisel 2.0–2.4 mm |
| Lead-free (you don't have any yet) | 350–370 °C | same |
| Big pads, ground planes, thick wire | 350 °C | bigger chisel |
| Tiny pads, castellated edges (XIAO, Super Mini) | 320 °C | small chisel or conical 1 mm |

Hotter is *not* faster: above ~350 °C with 60/40 the flux burns off before it
does its job and the tip oxidises in minutes. A chisel tip beats a conical one
for almost everything; it transfers heat through its flat face.

**Tinning the tip — do this every session.** Heat to 320 °C, wipe on the brass
wool, immediately melt a little solder onto the tip so it is bright silver,
then wipe again just before each joint. A black, dull tip does not transfer
heat; if it won't take solder, use the tip cleaner (the tin-and-flux puck) and
rub it in. **Before you switch the station off, leave a blob of solder on the
tip** — it protects the plating.

**Done when:** the tip is shiny after wiping, you have a stand, light, fan and
a clear 40 × 40 cm of bench.

---

## Phase 1 — Breadboard fluency (1 h, no code)

You have the **Radox 830-point** board (sealed) and the **MB-102 kit** (400 +
830 points, power module, 65 jumpers). Open the MB-102 first; keep the Radox
sealed as the "clean" board for a real project.

### How the holes are connected

```
      power rail (+)  ───────────────────────  (one strip, sometimes split in the middle!)
      power rail (−)  ───────────────────────
   a b c d e   |   f g h i j
 1 o-o-o-o-o   |   o-o-o-o-o        each ROW of 5 (a–e) is one node; f–j is another
 2 o-o-o-o-o   |   o-o-o-o-o        the trench in the middle separates them
 3 o-o-o-o-o   |   o-o-o-o-o        (a DIP chip straddles the trench)
```

- Rows a–e are joined; f–j are joined; **a–e is never joined to f–j.**
- The long rails along the edges are joined *lengthwise*. On many 830 boards
  the rail is **split in the middle** — check with the multimeter's
  continuity beep before you trust a rail end-to-end; bridge it with a
  jumper if needed. This single fact explains half of all "my LED doesn't
  light" moments.
- Numbers run down the board; letters across. Say a hole out loud as you
  place a wire ("j12") — it stops you counting wrong.

### The MB-102 power module — read this before plugging it in

It takes 6.5–12 V on the barrel jack (or USB) and outputs **3.3 V or 5 V per
rail**, chosen by a jumper on each side. Two rules:

1. Set both jumpers **before** powering. A 5 V rail feeding a 3.3 V-only
   sensor (many are) kills it silently.
2. Its USB port is *output* (5 V out), not a way to power it from the PC.
   Power the module from a 9 V adapter or leave it in the box and use the
   dev board's own 3V3/5V pins as your rails — for everything in this
   lesson, the ESP32's pins are enough.

### Exercise 1.1 — LED + resistor (the only circuit you need to memorise)

Materials: any LED, a **220 Ω** resistor (red-red-brown), two jumpers, a
3V3 source (a dev board's 3V3 and GND pins are fine — the board is just a
power supply here, no code).

1. Long leg of the LED (anode, +) into `f10`. Short leg (cathode, flat side of
   the rim) into `f11`.
2. Resistor from `j10` to the **+** rail. Any orientation.
3. Jumper from `j11` to the **−** rail.
4. Rails to 3V3 and GND. It lights. Swap the LED legs: it doesn't. That is a
   diode.

Why 220 Ω: (3.3 V − 2 V LED drop) / 220 Ω ≈ 6 mA. Enough to see, far below the
20 mA the LED and the GPIO pin can take. You will use this exact value on every
status LED for years.

### Exercise 1.2 — button with a pull-down, then read it later with code

Button across the trench (legs on rows 15 and 17, straddling), a **10 kΩ**
resistor (brown-black-orange) from the "output" side to GND, the button's other
side to 3V3. Measure the output node with the multimeter: 0 V released, 3.3 V
pressed. This is what `INPUT_PULLUP` in project 04 did for you in silicon.

### Exercise 1.3 — I²C sensor on the breadboard

AHT20+BMP280 combo: VIN→3V3, GND→GND, SCL→GPIO 9, SDA→GPIO 8 on an ESP32-S3
devkit (or whichever pins you set in ESPHome). Flash the project-09 YAML with
the two sensors added. You have just done, on a breadboard, exactly what phase
5 makes permanent.

**Jumper discipline that saves hours:** red = power, black = ground, yellow =
SCL, blue = SDA/data, and **wires go around the board, not over components**.
A tidy breadboard is a debuggable breadboard.

**Done when:** all three exercises work, and you found (with the meter) whether
your rails are split.

---

## Phase 2 — First joints (1 h): learn to read a joint

Materials: one **5×7 perfboard** (you have ten — sacrifice one), a handful of
resistors from the kit, the third hand, wick.

### The five beats of one joint

Say them until they are automatic:

1. **Touch** — tip touches *both* the pad and the lead at once (chisel face
   flat on the pad, edge against the lead). Count *one*.
2. **Feed** — touch the solder to the **joint**, not to the tip. It should
   melt from the heat of the pad/lead. Count *two*.
3. **Flow** — feed 2–3 mm more; watch it wet the pad and climb the lead in a
   cone. Count *three*.
4. **Solder off** — pull the solder away first.
5. **Iron off** — lift the iron straight up. Don't blow on it, don't move the
   part for 2 seconds.

Whole thing: about **three seconds**. Longer than five and you are cooking the
pad (perfboard pads lift; module pads too).

### Reading the result

| Looks like | Name | Cause | Fix |
|---|---|---|---|
| shiny concave "volcano", lead visible at the tip | **good joint** | — | — |
| dull, grainy, ball sitting on top | cold joint | iron only heated the lead *or* only the pad; or moved while cooling | reheat with a touch of fresh solder (fresh flux is the point) |
| solder blob touching the neighbour pad | bridge | too much solder, iron dragged | wick it (phase 6), or drag the hot tip *away* along the trench |
| pad not wet, solder on the lead only | pad never reached temp | tip not on the pad; tip dirty | clean tip, retouch with flux |
| big round ball, lead hidden | too much solder | feeding too long | wick a bit off; not fatal |
| pad lifted / copper ring loose | overheated | > 5 s, or reheating many times | move to the next pad; on perfboard just use another hole |

### Exercise 2.1 — 30 joints

Push 15 resistors through the perfboard in a grid, bend the legs slightly so
they don't fall out, solder all 30 pads. Do the first 10 slowly counting
beats; the next 20 at rhythm. Then **clip the legs** with flush cutters just
above the fillet (cut away from your face; hold the leg so it doesn't fly).

### Exercise 2.2 — solder a wire to a pad

Strip 3 mm of **solid-core 22 AWG** wire, tin it (melt solder onto the bare
end first), then join it to a tinned pad: touch, feed a little, off. Tinning
both sides first is *the* trick for wires.

**Flux.** Your 60/40 has a rosin core, so most joints need nothing extra. Use
the flux paste on: anything you reheat, tinned wires, breakouts with tiny
pads, and any joint that just looks wrong. Clean residue afterwards with
isopropyl alcohol and an old toothbrush — rosin is mildly corrosive over years
and it hides the joint.

**Done when:** you can look at your grid and point at every cold joint and
bridge yourself. If there are none, make one on purpose and fix it.

---

## Phase 3 — Headers on a dev board (1 h): XIAO ESP32-S3

Why this board first: it is cheap (you have three), it has **castellated
edges** (teaching moment), and once it has pins it snaps into the **XIAO Grove
shield** already in the drawer — OLED, RTC, buzzer and 8 Grove ports, all
plug-and-play. It also runs ESPHome, which the SAMD21 cannot.

1. **Break the headers** to 7 + 7 pins (score with a knife on the plastic,
   snap; the leftover half-pin is normal).
2. **Use the breadboard as a jig.** Push the two header strips into the
   breadboard, long pins down, 7 rows apart so the XIAO sits on top. The
   board rests on the short pins, perfectly square. This is the single best
   header trick: gravity and the breadboard hold everything, the third hand
   stays free.
3. **Tack one corner pin.** Touch-feed-off. Check the board is flat and
   square. If not, reheat that one pin and press the board down while it is
   molten (fingers on the *far* side).
4. **Tack the opposite corner.** Check again. Now it cannot move.
5. **Solder the rest** in sequence, pausing every 4 pins so the board does
   not heat-soak. Castellated pads: the solder wets the half-hole *and* the
   flat pad; you want a fillet that fills the half-circle, not a ball.
6. **Inspect** with the loupe: every pad shiny, no bridges between the
   0.1"-spaced pins (bridges here are the classic beginner fault — if you
   see one, don't add solder; drag the clean tip between the pins, or wick).
7. **Test before use:** multimeter continuity between adjacent pins must be
   *silent* (no beep). Then plug into the Grove shield, flash ESPHome with
   the OLED and a `binary_sensor` on the shield button. If the OLED shows
   text, phase 3 is done and you have a second HA node type.

Repeat on the second XIAO the same day while the movement is fresh. Leave the
third bare — a bare XIAO is also useful (castellations can be soldered flat
onto a perfboard later).

**Done when:** two XIAOs with pins, one running on the Grove shield, zero
bridges by meter.

---

## Phase 4 — Breakouts: the voice-satellite BOM (1.5 h)

The inventory already holds the full parts list for five DIY voice satellites
(project 06, the "cheap parallel route"): **ESP32-S3 devkit 44-pin + INMP441
I²S mic + MAX98357 I²S amp + 4 Ω speaker**. All of it is sitting there with
headers unsoldered. This phase builds *one* on the breadboard.

1. **INMP441 (6-pin).** Pins point *down* through the breadboard so the mic
   hole faces up — the mic must hear. Jig it in the breadboard, tack a corner,
   solder six pads. These pads are small: 320 °C, small chisel, a dab of flux
   paste, two seconds each.
2. **MAX98357 (7-pin header + the green screw terminal).** Header down, jig,
   tack, solder. The screw terminal comes soldered on most of these; if yours
   is loose, its two big pins need 350 °C and a full three seconds each — they
   are heat sinks.
3. **Speaker**: the mini 4 Ω 3 W has a JST 1.25 mm lead. Either crimp/solder
   two 22 AWG wires to it and screw them into the amp's terminal, or (better
   for the breadboard) solder the JST pigtail to two header pins.
4. **Wire it** (ESP32-S3 devkit, one of the antenna ones):

   | INMP441 | ESP32-S3 | | MAX98357 | ESP32-S3 |
   |---|---|---|---|---|
   | VDD | 3V3 | | VIN | 5V (USB) |
   | GND | GND | | GND | GND |
   | SCK | GPIO 5 | | BCLK | GPIO 15 |
   | WS | GPIO 4 | | LRC | GPIO 16 |
   | SD | GPIO 6 | | DIN | GPIO 7 |
   | L/R | GND (left channel) | | GAIN | leave floating = 9 dB |
   | | | | SD (shutdown) | leave floating = amp enabled |

   Any free GPIOs work; these avoid the strapping pins (0, 3, 45, 46) and
   the USB pins (19, 20).
5. **ESPHome**: `i2s_audio` with two buses, `microphone:` and `speaker:`
   components, `voice_assistant:` pointing at HA, `micro_wake_word` if you
   want a wake word on-device. Push a TTS announcement from HA: if the
   speaker talks, the amp joint set is good; if HA transcribes you, the mic
   set is good.

**Done when:** one satellite hears and speaks on the breadboard. The other
four sets of headers can wait for the day you build them — soldering headers
is now a 10-minute job, not a project.

---

## Phase 5 — The permanent node: perfboard sensor board (2–3 h)

This is the graduation: an **AHT20+BMP280 sensor node** for the plant project
(07), on a 5×7 perfboard, with the ESP32 in a socket so it stays reflashable
and replaceable, mounted in a box.

### Plan on paper first (15 min, non-negotiable)

Print or draw the 5×7 grid (18 × 24 holes at 0.1"). Place:

- **Socket for the MCU**: two rows of female header (from the 40-pin strips,
  cut to length with flush cutters — cut *through* a pin position, you lose
  one pin per cut). For a XIAO: 7 + 7. For a 44-pin S3 devkit: 22 + 22, which
  fills the board — for those use the Freenove terminal shield or the 44-pin
  screw breakouts instead; the perfboard route is for XIAO / Super Mini.
- **Sensor**: the AHT20+BMP280 module on a 4-pin female header, so it can be
  swapped or moved to the end of a cable later.
- **Status LED + 220 Ω** and a **10 kΩ pull-up** if you use a button.
- **Screw terminal** (2-pin) for power if the box will be powered by a barrel
  adapter instead of USB.

Draw the tracks: on the *bottom* you will run **solid-core wire** or **solder
bridges** between adjacent pads. Keep GND along one edge and 3V3 along the
other; sensor lines cross the middle. Ten minutes here saves an evening.

### Build order

1. Solder the **female headers** first (jig trick: plug the XIAO into them,
   put the sandwich on the perfboard, tack corners, remove the XIAO before
   soldering the rest so you don't heat it).
2. Solder the **lowest** components next (resistors, flat), then taller ones
   (LED, sensor header, screw terminal). Low-to-high means the board lies flat
   on the bench for each step.
3. **Bottom-side wiring.** Two techniques:
   - *Wire hops*: 22 AWG solid core, stripped both ends, tinned, laid flat
     along the pads, soldered at each end. Tidy, easy to trace.
   - *Solder bridges*: for **adjacent** pads only, drag a well-loaded tip
     across two or three pads to join them. Faster, uglier, fine for GND runs.
   Never rely on a bridge for more than three pads; use wire.
4. **Continuity test before power — every single net.** Meter on beep:
   3V3 to every 3V3 pad (beep), 3V3 to GND (**must be silent**), SDA to the
   sensor's SDA (beep), SDA to SCL (silent). Five minutes; catches the bridge
   that would otherwise cook the board.
5. Plug the XIAO in, USB, flash the project-09 YAML with the two sensors.
   Readings in HA → the node exists.
6. **Box it.** Drill/cut for the USB and the sensor (or run the sensor on a
   Grove/Dupont lead outside the box so it measures room air, not the ESP32's
   warmth — a sensor sitting next to an ESP32 reads 3–5 °C high). Label with
   the Dymo. Add it to `INVENTORY.md` and the HA device list.

**Done when:** the node sits in a box on a shelf reporting to HA, and you can
pull the XIAO out and put a fresh one in without an iron.

---

## Phase 6 — Undoing mistakes: desoldering and rework (45 min)

Do this on the phase-2 practice board, not on the node.

- **Wick** (braid): lay the braid over the joint, press the hot iron onto the
  braid, the solder climbs into it by capillary action. Move to a fresh
  section of braid each time. Add flux to the braid — it works twice as well.
  Best for bridges and excess solder.
- **Pump**: heat the joint until molten, bring the pump nozzle in next to the
  tip, trigger. Best for through-hole pins you need to free. Practice: remove
  three resistors from the grid cleanly.
- **Hot air (858D)**: 300–350 °C, air flow 3–4 of 8, nozzle 1 cm above,
  small circles, 10–20 s until everything on the part is molten, lift the
  part with tweezers. Shield neighbouring plastic (headers, connectors) with
  Kapton or aluminium foil. Use it for: removing a whole header strip, a
  module, or a surface-mount part — not for a single pin. Practice: remove
  a header strip from the practice board; then put a fresh one on.
- **Salvage rule:** a pad you have reheated four times is a pad you should
  stop using. Move over one hole.

**Done when:** you have removed a header strip with hot air and re-soldered a
new one without lifting a pad.

---

## Phase 7 (optional) — From perfboard to a real PCB

When a design is going to be built more than twice (five voice satellites,
three plant nodes), a printed board beats perfboard: no wiring errors, the
mounting holes are where you drew them, and assembly drops to 20 minutes.

1. **KiCad** (free): draw the schematic — you already know every symbol from
   phases 1–5 — then the board. The XIAO and INMP441/MAX98357 footprints exist
   in the KiCad libraries or as free downloads from Seeed/Adafruit.
2. Order 5 boards from JLCPCB/PCBWay (~USD 5–10 + shipping to Mexico; 10–14
   days). Two-layer, 1.6 mm, HASL finish, standard everything.
3. Assemble with exactly the phase-3/5 technique. Through-hole first; if you
   used surface-mount passives, that is what the hot-air gun and a stencil are
   for — a separate lesson.

---

## The safety card (tape it to the station)

- Tip is 320 °C. It looks the same cold. **Stand, always**; never lay the
  iron on the bench "for a second".
- Fan across the bench, window open. No food, no drink; wash hands after.
- Eyes: clip leads *away* from you; hot solder spits — glasses when clipping
  or desoldering.
- Unplug the board before soldering on it. Never solder a board that is
  powered or USB-connected.
- ESP32 modules and MEMS mics are ESD-sensitive: touch the grounded station
  body before picking them up; keep them in their bags until the header is
  in the jig.
- Hot air: nothing plastic within 3 cm of the nozzle; the nozzle stays hot
  for a minute after the gun stops.

## Glossary

**Castellated** — half-holes on a board's edge (XIAO, Super Mini) that can be
soldered as pins *or* flat onto another board. **Cold joint** — solder that
never wet the metal; dull, weak, intermittent. **Fillet** — the concave cone
of a good joint. **Flux** — the chemical that removes oxide so solder can wet;
in the core of your wire, extra in the paste. **Jig** — anything that holds
the part square while you tack it (the breadboard is the best one you own).
**Perfboard** — pads only, no tracks; *stripboard* has copper strips (you
don't have any; not needed). **Tack** — one quick joint to fix position before
soldering the rest. **Tinning** — pre-coating a tip, wire or pad with solder.
**Wetting** — solder flowing and bonding to the metal, the sign of a joint
that will last.

## Where this goes next

- `PROGRESS.md` → after Task 18, the next chapter is **phase 3 + 5 of this
  lesson** (a XIAO ESP32-S3 node on the Grove shield, then the perfboard plant
  node). That also retires the "XIAO SAMD21 chapter blocked on soldering iron"
  line — the iron exists and the SAMD21 is superseded by the XIAO ESP32-S3.
- Home-Lab dashboard → this repo is a **sub-project of Home-Lab** since
  2026-09-25 (cockpit decision); rows live under Home-Lab as `🔌 Electrónica ·`.
- Project 06 (voice satellites) → phase 4 here is its first physical step.
- Project 07 (plant sensors) → phase 5 here *is* node #1.
