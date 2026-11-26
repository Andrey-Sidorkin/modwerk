# Songbook testing

Version 0.2.0-experimental (setlists; section "Setlists (0.2.0)" below) on 0.1.0-experimental (books), called Arr
Setlists until 22 November 2026. Host gate, emulator runs and the developer's own tests on their MKI (flashed base
`6c189889…`), each on the named build below; the last build tested on the unit is package `37f7e94f…` (source
`b976e98ff`), on base 0.7.13, 23 November 2026 (the test procedure below); the book 1 fix after it is host-tested only.

## Host gate

`verify.py`: 218 checks of songbook.c (89 for the books of 0.1.0) against simulated battery RAM and simulated stock file routines: switching
both ways, arr09-arr128 named and written 8 at a time, book 1's backup and arr01-08 repaired on return, a file that
will not read never written back, refusals (playing, card busy, no project), reinstall and power-cycle detection by
the battery record, SAVE and RELOAD PROJECT, returning to where a project was left (in stock's order of hooks and its two
load jobs, and a write between them), arranger mode after LOAD, the label. Passes.

## Emulator (ot_emu, adapted, beta base, OS 1.40C MKI layout), 18 November 2026

Not hardware evidence. Package `songbook.mwrm` built from this folder, trial over the emulator's USB link
(`--dsp-rt`), a project created on an empty Set, the card written back (`--card-rw`).

- Book 1 to 2: stock's SYNC TO CARD job ran (NOW SAFE TO EJECT!), book 2 came up with 8 empty arrangements, the label
  read BOOK 2. Five rows added on book 2. Book 2 to 3 to 2: its five rows back. Back to book 1: its three rows back,
  unchanged. Book 1 to 2 again: the five rows.
- On the card afterwards: arr01-arr08.work, ARR01.BAK-ARR08.BAK and arr09-arr16.work, all 11,336 bytes; arr09 has
  stock's header and differs from arr01 in 9 bytes.
- Earlier build (own file format, same switching): a power cycle on book 2 with no project load kept book 2 in the
  slots and the module took it as book 2; a power cycle with the project loaded put book 1 in the slots, as recorded.
  Switching after that power-up could not be checked here (this emulator then reports "no project"): on the unit.
- Not run in the emulator: SAVE PROJECT and RELOAD PROJECT, switching while playing, removing the module on book 2,
  a file that will not read.

## On the unit

Andrey Sidorkin's MKI, flashed base `6c189889…`, scratch project, 18 November 2026. The development base did not
upload over USB that day (four attempts dropped at 10-20 %), so my driven stage 1 could not run: a 10-second and a
15-second trial of package `101941a7…` logged no crash, stall, freeze or skipped track, and it was kept for the
developer's own test.

- As the developer reported it: the label stepped BOOK 1 to BOOK 16 in about half a second a press, but ARR > CHANGE's
  list kept book 1's names, and an arrangement opened straight from it still showed book 1's rows. Leaving to the main
  screen showed the new book's (empty) arrangement; reopening CHANGE showed its names.
- A 2-minute trial of a diagnostic build counted, on the unit: each switch to a new book cleared all 8 arrangements
  (arrangement 1 then had 0 rows); book 1 read its 8 backup files back. So the switch worked and only the open list
  was stale: stock draws it once, when it opens.
- Fixed in the next build: after a switch the module has stock draw the open list again (0x40036e18). Under ot_emu,
  names SONG ALPHA, BRAVO, CHARLIE on book 1: RIGHT showed an empty list with BOOK 2 at once, LEFT the three names.
  On the unit (package `caab80cb…`, kept): books 3 and up showed empty lists at once; book 2 showed book 1's names.
  As the developer reported it, a clean run on books 3 and 4 then passed: rows added on book 3 came back after book 4,
  book 1 was unchanged, book 4 stayed empty. Book 2's copy of book 1 dates from the first test, before the fix;
  how it was written is not known.
- The developer asked that each book come back on the arrangement last chosen in it. Built with stock's own choice,
  select_arr (0x400a03a0), and the list's cursor moved onto it. Under ot_emu: book 1 on arrangement 3, book 2 then on
  1, arrangement 2 chosen there, book 1 back on 3, book 2 on 2, a new book on 1; after a switch DOWN and YES chose the
  row below the remembered one.
- On the unit, package `351f45ab…` kept, as the developer reported it: book 1 on arrangement 3 to book 3 on 1;
  arrangement 2 chosen there; book 1 came back on 3 and book 3 on 2. While playing, LEFT/RIGHT said STOP TO CHANGE
  BOOK and nothing changed. ARR SAVE, an edit and ARR RELOAD in book 3 brought the saved version back. A power cycle on
  book 3 came back on BOOK 3 with book 3's arrangements: the unit keeps them in battery memory, and the module's record
  matched them (not a project load, so not book 1, as I had expected).

Package `c6b2c9a4…`, as the developer reported it: after FUNC + PATTERN (arranger mode off), SYNC and CHANGE the
project opened in pattern mode (passed); SAVE on book 3, arrangement 5 and RELOAD landed on book 1 (failed). Event
log: save, reload, stock's new-project step, then arranger mode off/on twice, and no ev_banks_load, where the module
now read the saved place. Fixed: the RELOAD hook reads it too. `verify.py`: 69 checks.

Package `da945df5…`, as the developer reported it: SAVE on book 3, arrangement 5 and RELOAD showed book 1, then
came back to book 3, arrangement 5 (passed); SYNC in arranger mode on book 2 and CHANGE to the same project came back
on that book and arrangement in arranger mode (passed).

### Book 1 overwritten with book 2 (found on the unit after the module was removed, 19 November 2026)

As the developer reported it: after removing the module (on book 1, as the module said) the arrangements shown were
book 2's. Cause, from stock's code: LOAD and RELOAD are two card jobs. The first (0x40085276: project, banks, and the
arrangement files checked only) raises ev_banks_load and ev_banks_loaded; the second (job 27, 0x40085342) starts with
the new-project step (0x400909d8, ev_project_new) and loads arr01-08 into the slots last (0x4008fd28). The module took
ev_banks_load as "the slots hold book 1". When its return switch ran between the two jobs (its event log once showed
the switch before ev_project_new), leaving "book 1" made stock's writer save the slots, still holding book 2, as
arr01-08, and the backup took the same; the second job then loaded them. Fixed: the book changes to 1 only at
ev_project_new, and the return switch is asked for only after it, so it queues behind the job that loads arr01-08.
`verify.py` (71 checks) simulates both jobs and a write between them; against the previous code 8 checks fail,
among them that write landing in arr01-08. On the unit: not yet (the unit is paused while #1980, base freezes, is
open).

Package `7b4329e6…` (the book 1 load fix), kept beside USB AUDIO on base 0.7.13 (`6d23e888…`), a new project with
book 1's arrangements named B1 ONE and B1 TWO, as the developer reported it, 22 November 2026: book 2 with B2 ONE, SYNC
and PROJECT > CHANGE came back on book 2, B2 ONE, in arranger mode; book 1's B1 ONE and B1 TWO were intact (passed).
SAVE and RELOAD on book 2 came back on book 2, but the main screen showed book 1's arrangement name until the ARR menu
was opened and closed. Stock refuses select_arr while a change of its own is pending (0x800066a0), as after a RELOAD in
arranger mode; the module now tries again on the next frames, up to two seconds. `verify.py`: 72 checks.

Package `d731cb92…` (the retry), as the developer reported it: the main screen still showed book 1's arrangement
after RELOAD. Event log: the reload's load, stock's new-project step, arranger mode off, return posted, arranger mode
on (stock's end of the load, before the module's switch ran), switch to book 2. Stock had set up the arranger over
book 1's slots; choosing the arrangement does not rebuild that. Fixed: after a return switch that follows a load, with
arranger mode on, the module applies it again with stock's own routine (0x40055fd8, as stock's end of a load does);
never after LEFT/RIGHT. `verify.py`: 74 checks.

Package `08d0833a…` (source `f068cd608`), kept beside USB AUDIO on base 0.7.13, as the developer reported it,
22 November 2026: in arranger mode on book 2, SAVE and RELOAD PROJECT came back with the main screen showing B2 ONE,
PLAY played B2 ONE's rows, and book 1's B1 ONE and B1 TWO were intact afterwards (passed). Together with the round
before on `7b4329e6…` (LOAD back on book 2 in arranger mode, book 1 intact), the book 1 load fix is tested on the MKI.

### A backup file missing or unreadable (found by the owner in review, #1737, 22 November 2026)

Leaving book 1, deleting one of its `.bak` files and returning made the backup read treat the missing file as an empty
slot and report success; all eight slots were then marked changed, and the next save replaced the valid arr03.work
with an empty arrangement. Fixed: book 1 is restored slot by slot; a slot whose backup is missing or does not read
comes from its own arr0N.work and is not marked, so that file stays as it was. A backup counts only when all eight
files were written, so a partial one is never read back. `verify.py` (80 checks): a missing arr03.bak, an unreadable
arr05.bak and a backup write failing after three files; the previous code fails the missing-file case (arr03.work
saved empty). On the unit: not yet.

### An arrangement file that is there but fails to open (found by the owner in review, #1737, 22 November 2026)

Every failed open counted as "no such file": a book's file that failed to open once (a card fault) gave an empty slot
with no error, and the next sync wrote that empty slot over the saved arrangement. Fixed: a failed open is "no such
file" only when stock's own exists check (the file system's, at 0x46c823fa, as stock's copy uses for its -12) says the
file is not there and a second open fails too. A file that is there but did not open is a read failure, so the book
is reported (BOOK FILE ERROR) and written back only where edited. `verify.py` (86 checks): arr11.work failing to open
once on the way back to book 2 (reported, the next sync writes nothing, arr11.work keeps its arrangement, an edit of
another slot is still saved, the file reads again later), and the open and the exists check failing together with the
second open working; the previous rule fails six of them (arr11.work saved empty). Not covered: a card on which the
open and the exists check keep failing for a file that is there, which still reads as missing. On the unit: not yet.

### A reload between the failed read and the next sync (found by the owner in review, #1737, 23 November 2026)

The module kept which book is in the slots across a reinstall, but not that the book was read only in part: after a
reinstall, or back after another build's trial (Undo), the next sync wrote the empty slot over arr11.work. Fixed:
the battery record keeps that state too (its byte 6), and the install handler reads it back with the book.
`verify.py` (88 checks): the reinstall and the return after Undo, each between arr11.work's failed open and the next
sync; without the record byte both overwrite arr11.work. On the unit: not yet.

## The test procedure on the MKI (owner's request, #1737, 23 November 2026)

Package `37f7e94f7fff7b1828fc6a4eb3c8aab21ccf2f84a78739851929ab6b8b7233ba` (source `b976e98ff`), Andrey Sidorkin's
MKI, flashed base 0.7.13 (`6d23e888…`), USB directly on the Mac, the developer's scratch project, kept beside USB AUDIO
and Arr Rows 0.2.0.

| # | Step | Result |
|---|---|---|
| 1 | Software checks (`module:doctor --draft songbook`) | green |
| 2 | Beside every other module | by the doctor (a draft) |
| 3 | Name the build | `37f7e94f…` from `b976e98ff`, rebuilt to the same SHA-256 |
| 4 | DSP cost model (`cycle-scan.mjs check`) | current (no DSP code) |
| 5 | Emulator rehearsal | not run on this build (the setlists build ran in ot_emu the same day) |
| 6, 10 | Read the unit, put back | status and inventory read before and after: USB AUDIO and Arr Rows kept before; Songbook added by the Keep the test needed |
| 7, 8, 9 | Scripted pass, soak, cost | not run: they need a development base, and its RAM boot (`device boot DIR --chunk 4096`) broke off twice ("chunk was not confirmed", Errno 5), the unit running on but gone from USB until replugged; the flashed base refuses development requests |
| 11 | By hand | the owner's checks below, through installs, Keep and a trial over USB |
| 12 | The developer | as they reported it, below |

The owner's checks, as the developer reported them (books 1 and 2 given distinct arrangements first: book 1's first
arrangement "ONE", last row 147; book 2's "DS SET", last row 046):

- Installed with a Keep (no crash, stall, freeze or skipped track logged), then book switches both ways: both books
  as made.
- Reinstall: a power cycle on book 2 (the module loads from the card with fresh memory): back on book 2, both books as
  made; then SYNC TO CARD on book 2: both books as made.
- A trial of another build (the setlists build `4016d35e…`) over the kept one, 110 s, a switch to book 1 and back during
  it (its event log: two switches done, on book 2 at the end), rolled back (Undo); then SYNC TO CARD: both books as made.

Seen on the way:

- Twice, EDIT opened right after the module wrote book 2 (a switch to book 2, a SYNC on book 2) showed an empty
  arrangement; NO and EDIT again showed every row. Display only, nothing lost; cause not yet found (stock's writer,
  which the module replaces on books 2-16, calls a per-slot callback the module does not; whether that is it is open).
- Once, opening the PROJECT menu froze the unit (no USB answer; the base logged no crash or new stall record before the
  power cycle). Not repeated: the PROJECT menu opened without a freeze afterwards, SYNC TO CARD included. Like the
  freezes of #1980 (base 0.7.5, no modules); the module does nothing when that menu opens.

### Book 1 read only in part, then a sync (found after the unit run, 23 November 2026)

Stock's arrangement writer writes each slot its mask names (a sync: all 8) or whose file is missing, and then SETS that
slot's bit in 0x1001614c (0x40091680, 0x40091748); it never chooses by that byte. The host test had modelled it as
writing the marked slots and clearing the byte, which hid a loss path: with book 1 read only in part (a `.bak` and its
`arr0N.work` both unreadable), the module still handed every write to stock's writer, and the next sync would write the
empty slots over arr01-08. Fixed: book 1 read only in part is written as books 2-16 are, only the edited slots. The host
model now writes as stock does; `verify.py` (89 checks) adds the sync on a partly read book 1, which the previous code
fails (with the corrected model, three checks). Host-tested only.

## Setlists (0.2.0)

Host gate: `verify.py`, 218 checks; the setlist part plays through a simulated CHAIN (stock taking it past a song's
last row), the end on END OF ARR, edits during a show, copy and paste, names and the old file format, the main-screen
and title displays, the CUE + YES window, and every path that can touch slots 7 and 8.

Emulator (ot_emu, adapted, the beta base, MKI panel, 23 November 2026), screenshots by the panel link: SETLSTS fits its
menu cell; the page, the window, the add message, the LIST box, the Part-box display and the menu titles drawn in
stock's font where intended; "SETLSTS" and "BOOKS" in the menu, the list icon. The emulator's sequencer does not advance
after PLAY (stock pattern mode neither), so no song change could be seen there. The busy refusal and the RELOAD loss
below were reproduced there and their fixes checked there.

On the developer's MKI (OS 1.40C, base 0.7.13, kept beside USB AUDIO and Arr Rows 0.2.0, a scratch project), as the
developer reported it, 23-24 November 2026, the last build `1d576a3e…` (source `7a53e26af`): a setlist of three songs
plays through with each song following the last, the last ending on END OF ARR; the window chooses the next song at
once with YES and follows a song change; copy and paste of songs and setlists; names; the Part-box display, the menu
titles and the playing mark; an edit made during a show (row 000 set to A16 in song 2) is in that song's book after
the setlist ends; RELOAD and a power cycle in a setlist come back into it on the song it was on, songs intact.
Not run: LOAD of another project and back during a setlist (host-tested); an MKII.

### Faults found on the unit and fixed

- **Installs refused as busy until a power cycle** (`7f2c9fb4` and later): the module wrote the next song's name into
  the bytes of its own declared poke; the loader's check of declared bytes then failed every later switch (refusal 5).
  Reproduced in ot_emu (starting a setlist, then an install). Fixed in `a5d48552c`: the chain line's operands point at
  the module's own text. Installs after a setlist went through on the unit from then on.
- **Tempo, MIDI channel and bank numbers blank** (`88922349` to `c4e6b541`): the chain-text poke covered stock's shared
  `"%d"`. Fixed in `d3a8ace28`, then replaced by the operands above.
- **Data loss on RELOAD in a setlist** (`3382ca4f`): the reload hooks did not end the setlist in memory; stock refilled
  slots 7 and 8 with book 1's (empty) arrangements 7 and 8, and the module took them back as edits of songs 2-8 and
  wrote them when the setlist ended. Reproduced in ot_emu. Fixed in `a542bc4d4`: every load, reload and new project lets
  go of a setlist, and a slot no longer holding its song (name changed or rows gone) is never taken back.
- **Data loss on a power cycle in a setlist** (`26f14530`): slots 7 and 8 kept the setlist's copies (battery RAM) with
  a stale fingerprint; the module took them for book 1 and the resume's card job wrote them into arr07/arr08. Fixed in
  `5a67c187b`: the battery record alone says the slots hold setlist songs, and while it does and no setlist plays, a
  card job reads the book back first and writes none of it. The host test fails on the faulty code.
- **No resume after a power cycle** (`dc591db7`): the module comes up after stock's project load, so no load hook told
  it; fixed in `065d1bd20` (the first card job reads arrbook.work).
- **Another project's setlists after a LOAD** (`a95a5a5a` and earlier): the module read arrbook.work and arrlists.work
  while stock's project directory still named the last project (at `ev_banks_load`, and at `ev_banks_loaded` too), so
  project B came up with A's setlists and A then with B's. Fixed with #1737's three LOAD fixes (`be24ed97a`): read after
  the whole load when the project names differ, a stuck arranger run state reset before the return or a setlist's
  first song, the closed list's cursor set.
- **CUE left held after renaming a setlist** (`8c54d96b`): no key reaches the module in stock's name editor, so a CUE
  released there stayed held; LEFT/RIGHT then moved songs and arrangements. Fixed in `2bf33b38b` (CUE and FUNC forgotten
  around the editor).
- **PROJECT > CHANGE opened SETLSTS** (`83d01931` and earlier): the page took the release of the YES that opened it, so
  stock kept YES routed to the Arranger menu's dispatcher and every later YES ran its last item. Reproduced in ot_emu.
  Fixed in `d23b2fe85`: the page takes a release only when it took the press.
- **FUNC closed the SETLSTS page** (`83d01931`, `a94cdbd0`): a check on YES's route, which FUNC and CUE re-route, closed
  it. Removed in `d80a49602`.
- **NOW SAFE TO EJECT! after restarting a playing setlist from another song**: only switches and a stopped start closed
  it. Fixed in `d80a49602`: every card job the module posts closes it.

On `d80a49602` (package `8878af4a`), as the developer reported it: B on its own book 2, arrangement 1, with its own
setlists; PROJECT > CHANGE opens the project list after SETLSTS was used; A left on song 2 of a setlist came back into
it after LOAD of B and back, and PLAY went on to song 3; FUNC, FUNC + YES, FUNC + REC and FUNC + STOP on the page;
CUE + YES and CUE + LEFT/RIGHT in BOOKS; no message on a restart from another song. After a LOAD back into a setlist,
the main screen shows the book for a moment before the setlist. Once, in stock's name editor, the character choice
stayed open after every key was released; not reproduced.

After a LOAD back into a setlist the module took two or three card jobs of its own (read the project's files; switch
the book; start the setlist) and the main screen showed the book before the setlist. Since `3186f4341` the Part box
shows the setlist while it is due to come back, and since `331dd4536` one job of the module's does all three. On
`331dd4536` (package `346fd848`), as the developer reported it: A back in its setlist on song 2 and PLAY going on to song
3; the module's event log showed one job of its own after stock's load; about as many jobs on the screen as loading a
project the module brings nothing back for (B on book 1), one more.

## Qualification (0.1.0-experimental, package `84a8ba8a`)

- **ColdFire cost of the dearest event, under ot_emu** (24 November 2026, the adapted emulator, beta base 0.7.13 twin, this
  package installed over the emulated USB, a new project with empty arrangements): the PC watch on stock's arrangement
  writer's entry (0x400915ac, where the module's jmp sits) and its return (0x40091a76) during a book switch. Book 1 to 2:
  13,754,188 instructions (stock writes arr01-08, the module writes book 1's backup arr01.bak-arr08.bak, reads book 2's
  8 files and fingerprints the 8 slots); book 2 to 1: 7,849,450 (the backup read back). Executed instructions with
  interrupts and other tasks in that window, not cycles; card time on a unit is not measured. Budget: 100 ms of ColdFire
  time at 266 MHz (26,600,000), a key press's responsiveness target. The arrangement blocks are fixed-size, so their
  content does not change the count.
- **Memory:** the package's unit, 7,292 bytes of code and data and 4,728 of bss (the builder's report), and the 20-byte
  battery RAM record; no DSP memory.
- **Hardware:** the developer's MKI results on this exact package are above (build `c642ca255`); the test procedure's
  steps 7-9 are recorded when they run.

## Test procedure on the unit (24 November 2026)

`npm run test:procedure -- songbook` on the developer's MKI (OS 1.40C), development twin `4c7eb79b` of the beta base
RAM-booted over USB from the flashed `eb667962` (main `3043f8339`, with the MKI Part switch of #2103), package
`84a8ba8a` (this source), the developer's test project in pattern mode, transport stopped. Steps 1-10 passed:

| # | Step | Result |
|---|---|---|
| 1 | Software checks | passed |
| 2 | Beside every other module | not run: judged by module:doctor (step 1) |
| 3 | Name the build | passed: `84a8ba8ac39f74aaa8cd842f3179a1f8b0d38a24ffb32b56765daeb417c4ac47` |
| 4 | DSP cost model | passed |
| 5 | Emulator rehearsal | not run: no emulator session |
| 6 | Read the unit | passed |
| 7 | Scripted unit pass (device:test) | passed: installed as a trial, placed on the tracks, Parts and banks switched, self-test read, inventory back; no crash, stall, freeze or skipped track |
| 8 | Soak (120 s, MIDI over USB) | passed |
| 9 | Cost on the unit (device:price) | passed: frame interrupt 58.1 % before, 57.7 % installed and unused; CPU 68.3 % / 66.9 % (noise): 0 |
| 10 | Put back | passed |
| 11 | Agent by hand | partly: see below |
| 12 | The developer's own stage | the developer's results on this package above |

**What this run does not show.** On that twin the module's own UI hooks were not seen to run: ARR > CHANGE drew no
BOOK label, RIGHT switched no book, and the module's diagnostic word stayed `0xfccaacd0` across FUNC + PATTERN, which
the module logs. Steps 7-9 therefore show that the package installs, survives the scripted pass and the soak, and
costs nothing at rest on that twin, not that its features ran there; the features are the developer's results on the
flashed base above. Not run in step 11: MAIN and CUE recorded over USB (no recorder on this computer and no USB AUDIO on
the twin; the module makes no sound).

## Edge cases

| Case | Why this change makes it a risk | How run | Result |
|---|---|---|---|
| Switching books 1 to 16 and back, stopped | each switch writes 8 files and reads 8 inside stock's SYNC job, on the card task | the developer's MKI, builds 101941a7 to c5027552c | pass |
| LEFT/RIGHT in ARR > CHANGE while playing | stock's write job would stop playback | the developer's MKI, c5027552c | pass: refused with STOP TO CHANGE BOOK, nothing changed |
| PROJECT > CHANGE (load) on book 2, arranger mode | stock loads in two jobs; a return switch between them overwrote book 1 on an earlier build | the developer's MKI, 7b4329e6 (base 0.7.13) | pass: back on book 2 in arranger mode, book 1 intact |
| RELOAD PROJECT on book 2, arranger mode | stock re-applies arranger mode over book 1's slots before the return | the developer's MKI, 08d0833a (base 0.7.13) | pass: main screen and PLAY on book 2, book 1 intact |
| SAVE, edit, RELOAD on a book other than 1 | arr09+ need their own .strd copies | the developer's MKI, c5027552c | pass: the saved version came back |
| Power cycle on book 3 | the slots stay in battery RAM; the module must recognise them | the developer's MKI, c5027552c | pass: back on book 3 |
| FUNC + PLAY clear in the list, and its undo | stock's list clear sets no changed bit | the developer's MKI, 06f593e3 | pass: cleared stayed cleared, undo restored |
| A trial of another build over the kept one, switching during it | the kept copy came back with stale memory on an early build | the developer's MKI, 06f593e3 | pass: the kept copy reread its record |
| Beside USB AUDIO (installed with base 0.7.13) | both kept in one set, sharing the base | the developer's MKI, 08d0833a | pass: kept together, no crash, stall or freeze |
| Removing the module on book 1 | stock's next save writes the slots as arr01-08 | the developer's MKI, package 08d0833a removed on book 1, then a power cycle (an earlier build had left book 2 in book 1, the load fault since fixed) | pass: the stock arrangements were book 1's |
| An arr09+ file there but failing to open once | taken for a missing file, its empty slot was written back (owner's review) | host test (verify.py) | pass in the host test; not run on a unit |
| A reinstall or Undo between that failed read and the next sync | the module forgot the book was read only in part (owner's review) | host test (verify.py); on the MKI, `37f7e94f…`: distinct books 1 and 2 through a power cycle and a trial + Undo, each followed by a sync | pass on the MKI (both books as made); the failed read itself only in the host test |
| A sync on book 1 read only in part | stock's writer writes all 8 slots on a sync, the empty ones too | host test (verify.py), with stock's writer modelled as it works | pass in the host test; not run on a unit |
| Removing the module on a book other than 1 | stock would write that book into arr01-08 | README asks to step back to book 1 first | not run: documented limitation |
| A book file that does not read | must not be written back | host test (verify.py) | not run on a unit |
| A new project (ev_project_new outside a load) | must reset the books, not keep a load's return | host test (verify.py) | not run on a unit |
| A setlist's songs in the open book's arrangements 7 and 8 | the setlist borrows those slots | the developer's MKI (songs at 1:6, 1:7, 1:8), `214efe0e`/`1d576a3e` | pass: intact through a power cycle (lost on `26f14530`, fixed) |
| RELOAD during a setlist | stock refills slots 7 and 8 ~10 s into the reload | ot_emu reproduction; host test; the developer's MKI | pass on `fbb53513` and later (lost on `3382ca4f`, fixed) |
| Power cycle during a setlist | the slots keep the copies; no load hook reaches the module | host test (both); the developer's MKI | pass on `214efe0e`: back on song 2, songs intact |
| LOAD another project and back during a setlist | stock replaces the slots; its project directory names the last project until the load is over | host test; the developer's MKI | pass on `d80a49602` (package `8878af4a`): each project with its own setlists, A back on song 2 |
| An install while a setlist plays | fresh module memory, slots 7 and 8 not the book's | host test | pass in the host test; on the unit the install after each test ran without loss |
| An empty song in a setlist | stock takes the chain at once when it starts an empty arrangement | host test (slots emptied) | pass in the host test |
| Removing the module during a setlist | stock would save the copies into arrangements 7 and 8 | README asks to end the setlist first | not run: documented limitation |
| MKII | another panel and key layout | none | not run: no MKII |
| device:test / device:qualify lifecycle | install, Keep, Undo, removal on the scripted path | none | not run: the development base's RAM boot broke off over USB (18 and 23 November 2026); install, Keep and a trial + Undo were run by hand on the flashed base |

## Stock flows

Not tested on the unit. In the emulator, ARR > CHANGE with the module and book 1 behaved as stock: YES chose the
arrangement, NO left the list.

## Performance

Not measured. Counted from `songbook_core.s`: the dearest work of the module's own is the fingerprint of the 8
arrangement slots, 6 instructions a byte over 90,440 bytes (542,640 instructions), once a switch or project event and
once every 120 ticks while a book other than 1 is in use. A switch's card work runs inside stock's own SYNC TO CARD
job on its card task. No evidence/performance.json yet: perf:audit asks for a measured count and presses while
playing, and switching is refused while playing by design.
