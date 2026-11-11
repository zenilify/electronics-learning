# Tab5 — what to test, in order

Written 11 Nov 2026 for Erick by the T5 session on cachy (`7f4fa96c`). Last updated: **16:14 CST, final version (soak result added 16:34)**.

Total time: about 35 minutes. Tests 1 to 3 are the important ones (15 minutes); the rest can wait.

## Before you start

1. Leave the Tab5 **plugged into cachy's top USB-C port** (as it is now). I read its console there, so I can see
   numbers you cannot see on the screen.
2. Open **Ajustes** on the Tab5. The first line must say **PAPP OS v0.4.4**. If it says anything else, stop and tell me.
3. Tell me in the chat: **"I'm back"**. Then say the test number before each test ("test 1") so I can match my log to
   what you do.
4. If the screen ever freezes: **do not touch it, wait 15 seconds**. It restarts by itself. Tell me the time.

What changed while you were out:

- **Live subtitles** showed nothing this morning because the microphone signal was about ten times too quiet for the
  server's speech detector. Fixed on both sides: the Tab5 (0.4.4) raises the level while subtitles run, and the server
  on cachy (0.2.3) now judges loudness against the room instead of against a fixed number.
- New on the subtitles screen: the line under the round button says **"oyendo voz"** while it hears speech, and
  **"no oigo voz: acerca el Tab5 a quien habla"** after 8 seconds without any.
- **Memory.** While checking the subtitles I found that the Tab5 was slowly using up its internal memory whenever it
  showed Chinese text: each font size kept up to 512 drawn characters and never released them. That is bounded now.
  With every app opened, 172 kB are free instead of 137 kB, and 537 different Chinese characters cost 15 kB instead
  of 103 kB.

---

## Test 1 — Live subtitles (5 min) — the one you reported

| Step | Do | You should see |
|---|---|---|
| 1 | Home → **Subtítulos** | Title "Subtítulos en vivo · 实时字幕", three buttons on the left, a round button |
| 2 | Tap **→ English** | It turns teal |
| 3 | Tap the **round button** | It turns green with a square. Under it: "escuchando · 正在听" |
| 4 | Hold the Tab5 at arm's length and say, at normal volume: *"Hola, buenos días. ¿Dónde está la estación de tren?"* | While you speak: the bar under the button moves and the line says **"oyendo voz"**. One to two seconds after you stop: your sentence in small grey letters and the English translation in big blue letters |
| 5 | Put the Tab5 on the desk, step back **2 metres**, say another sentence | The same. If the line says "no oigo voz…" you were too quiet for that distance — note it |
| 6 | Play any **Chinese** video on your phone, 30 cm from the Tab5, for 20 seconds | Chinese text in small grey, English in big blue, one caption per sentence |
| 7 | Tap **→ Español**, speak one English sentence | Translation in Spanish (teal) |
| 8 | Tap the round button | It goes dark again: "toca para empezar" |

Tell me: which steps gave a caption, and any caption that was wrong.

Good to know:

- It captions **everything it hears**: a television or other people in the room appear too. That is expected.
- A sentence may be cut in two captions at a pause. Also expected.
- This works at home (server on cachy). **Away from home it is not fixed yet** — see decision A at the end.

## Test 2 — Push-to-talk still works (3 min) — I changed the audio code, so this is the safety check

Do this **right after test 1**, without restarting the Tab5.

| Step | Do | You should see / hear |
|---|---|---|
| 1 | Home → **Traductor**. Pair **ES ⇄ 中文** selected at the top | Two big panels: "ES — Mantén para hablar" and "中文 — 按住说话" |
| 2 | **Hold** the left panel, say *"¿Cuánto cuesta esto?"*, release | Your Spanish text, then the Chinese text, and the Tab5 **speaks** the Chinese |
| 3 | **Hold** the right panel, say anything in Chinese or play a Chinese clip, release | Chinese text, Spanish text, spoken Spanish |
| 4 | Repeat step 2 once | Same as before: not quieter, not distorted |

Tell me: "test 2 ok", or what was different from yesterday (too quiet, too loud, distorted, nothing).

## Test 3 — Memory with everything open (5 min) — decision D-10 from last night

| Step | Do | You should see |
|---|---|---|
| 1 | From Home open **every tile once** and go back each time: Traductor, Escribe, Frases, Llévame a, Convertir, Cámara, Notas, Diálogo, Subtítulos, Controles, Línea, Ajustes | Each opens normally |
| 2 | Tell me **"all open"** | I read the free memory |
| 3 | Do **three** translations in Traductor (as in test 2) | All three work |
| 4 | Tell me **"done"** | I read the memory again and tell you the numbers |

Pass: all three translations work and I read more than 80 kB free. Alone I measured 172 kB free with every app
opened (see "What I measured alone" below); what is missing is the reading while a real recording and playback run.

## Test 4 — Frases from the SD card (3 min) — never tapped by hand since the change

| Step | Do | You should see / hear |
|---|---|---|
| 1 | Home → **Frases** | Groups on the left (Básico, Comida, Transporte, Hotel, Compras, …), phrase cards on the right |
| 2 | Tap **Negocios**, then any phrase | The phrase is **spoken** in Chinese. In Ajustes the voice is set to "Voz: Erick" |
| 3 | Tap **Electrónica**, scroll down, tap a phrase near the bottom | Spoken, no delay longer than a second |
| 4 | Tap **Home**, open **Frases** again | The list is where you left it (same group, same scroll position) |

Tell me: any phrase that stayed silent or spoke the wrong text (group and phrase).

## Test 5 — Camera (5 min) — the app that froze on 8 and 11 Nov

| Step | Do | You should see |
|---|---|---|
| 1 | Home → **Cámara** | Title "Señala y traduce · 拍照翻译". Live picture on the left; under the buttons on the right: "cámara trasera (XIAO)" |
| 2 | Point the small camera at Chinese text (a label, a box, your phone). Tap the round **Capturar · 拍照** button | The text with its translation, after about 2 seconds ("mejor ~2 s" is selected) |
| 3 | Tap **Home**, open Cámara again, capture again. Do this **five times** | Works every time |

If it freezes: wait 15 seconds, it restarts. Then Ajustes shows **"último arranque: vigilante de pantalla"**. Tell me
the time — with the console log running I can now see what the firmware was doing, which I could not at 09:15.

## Test 6 — Remotes (5 min) — the recording display on the Tab5 changed last night

| Step | Do | You should see |
|---|---|---|
| 1 | Switch both remotes on. Home → **Controles** | Two lines with a check mark: "dial-944c Dial … Wi-Fi" and "stick-353c Stick PTT … Wi-Fi" |
| 2 | Home → **Traductor**. **Hold KEY1** on the StickS3, speak Spanish, release | While you hold: a level bar and the seconds on the **left** panel. Then the translation, spoken |
| 3 | **Hold KEY2** on the StickS3, speak, release | The same on the **right** panel |
| 4 | **Short press KEY1** | The last translation is spoken again |
| 5 | Dial: **hold the knob pressed**, speak, release | Same as step 2 |
| 6 | Dial: **turn the knob** | The language pair at the top changes |
| 7 | Dial: **touch its screen for one second, then turn** | The volume changes |

## Test 7 — Your travel charger (2 min)

1. Unplug the Tab5 from cachy and wait until Ajustes says "usando la batería".
2. Plug in the charger you will take on the trip with a **USB-C to USB-C cable** (no USB-A adapter).
3. Pass: the Tab5 does **not** restart, and Ajustes says "cargando".
4. If it restarts, Ajustes shows "último arranque: baja tensión ×N". Tell me which charger and which cable.
5. Plug it back into cachy's top USB-C port afterwards.

---

## Decisions I need from you

| | Decision | My recommendation | Why |
|---|---|---|---|
| A | Put the new speech detector on **Cloud Run** too | Yes, before the trip. Say "deploy server to Cloud Run" | Away from home the Tab5 uses Cloud Run, which still has the old detector. The Tab5's own fix helps there, but in a noisy room subtitles can stay empty |
| A2 | Live subtitles **without internet** (MacBook offline brain) | Decide after test 1 | The offline brain has no subtitle route at all today. Adding it is about an hour of work with the MacBook |
| B | **Access token on foreign Wi-Fi** (D-9 from last night) | Fix before the trip. Say "do D-9" | The Tab5 sends its token to whatever announces itself as the offline brain on the Wi-Fi |
| C | **ESP-NOW** for the remotes | Stay on Wi-Fi for Shenzhen | Works today; the ESP-NOW bridge needs a cable and a bench hour |
| D | **Merge PR #6** (firmware 0.4.4 + server 0.2.3) | After tests 1 and 2 pass. Say "merge PR #6" | Both are already running on the Tab5 and on cachy; the merge only puts them on `main` |

## Two questions

1. Around **15:35** the target language on the subtitles screen changed to "→ English" without any command from me, and
   the Tab5 captioned Spanish speech I did not play ("Ok, quiero comer…"). Was someone in the room, or a television
   on? If nobody touched it, the touch screen pressed a button by itself and I need to look at that.
2. Around **09:15** the screen froze twice and restarted by itself. Were you in Subtítulos then, or in another app?

## Small things I found on the way

- Two characters of the phrase pack are **not in the Tab5's font**: 茉 (as in jasmine tea) and 踝 (ankle). Phrases
  that use them show an empty box for that character. Needs the font file rebuilt with those two added.
  The same goes for rarer characters in a translation, and for all **traditional** characters (Hong Kong signs).
- At **14:50** the Tab5 restarted by itself. Cause: another session logged into cachy and probed the serial ports
  (two new boards had been plugged into cachy five minutes earlier). Not a Tab5 fault; I left that session a note.
- cachy has no loudspeaker the Tab5 can hear, so my test voice was the Tab5's own loudspeaker. That is why your voice
  test (test 1) still matters.

## What I measured alone (no hands needed)

| Check | Result |
|---|---|
| Live subtitles, the Tab5 speaking test phrases through its own loudspeaker | 6 of 6 phrases (Spanish, Chinese, English) captioned and translated; typical delay 0.5 s; repeated on the final build: 2 of 2 |
| Start and stop subtitles 15 times in a row | no freeze, no error |
| A television in the room | its dialogue was captioned from across the room — an accidental far-field test |
| Built-in clip test (no microphone) | still passes |
| Microphone gain back to normal after subtitles | yes (measured) |
| Memory, every app opened twice (final build) | 172 kB free, the second round costs 1 kB (no leak). Last night: 137 kB |
| Memory, 537 different Chinese characters typed into Escribe | costs 15 kB now; it cost 103 kB before, and never came back |
| Memory, a 7-minute subtitles session with 16 captions | 46 kB used; the caption list is capped at 20 now |
| 13 minutes walking through all twelve apps, 12 rounds (camera preview, subtitles on/off, demo translations) | 144 app opens, no freeze, no restart, no error line; memory settles at 156 kB free |
| Full backup of the Tab5 after the last flash | taken at 16:13, verified against the build |

What I could **not** do alone: speak with a human voice at a known distance, touch the screen, hear the loudspeaker.

## Where the full record is

- This afternoon's work, with every number: `tab5-translator/docs/PDCA-2026-11-11.md`, section 7 (repo
  `zenilify/electronics-learning`).
- Screenshots I took from the Tab5: on cachy in `~/t5/shots/subs/`. One is next to this file.
