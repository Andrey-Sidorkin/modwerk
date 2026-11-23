# Songbook

128 arrangements per project, as 16 books of 8, every one an ordinary stock arrangement file, and 16 setlists of up to
16 songs from any book, played one after the other without a gap. OS 1.40C, MKI and MKII.

## Overview

Stock keeps 8 arrangements per project, in `arr01.work` to `arr08.work`. Songbook makes them book 1 of 16
books of 8: book 2 is `arr09` to `arr16`, book 3 `arr17` to `arr24`, and so on up to book 16, `arr121` to `arr128`.
In ARR > CHANGE, with the transport stopped, LEFT and RIGHT step to the book before or after, and BOOK 2 (and so on)
shows at the screen's top right; the list shows the new book's names at once. Inside a book, every stock arrangement function works as usual: edit, ARR SAVE,
RELOAD, COPY, CLEAR, chaining and playback.

The Octatrack's own code writes and reads every one of these files, so they are stock arrangement files in every
byte. A project saved with the module opens on a unit without it with book 1 as it was, and an arrangement from a
later book can be moved into book 1 on a computer by renaming its file (`arr37.work` to `arr03.work`, say).

A setlist plays songs from any book in a chosen order. Songbook keeps its songs in memory and plays them through
two of the open book's arrangement slots, 7 and 8, chaining each to the next with stock's own CHAIN, so each song
follows the last without a gap. While a setlist plays, the main screen shows it: the setlist's name in the Part box,
the song's place (`|≡ 2/6`) under it, and the next song's name after the playing song's last row.

## Controls

| Where | Keys | What it does |
| --- | --- | --- |
| ARR > CHANGE, stopped | RIGHT | Write the current book to the card, then load the next one (up to 16) |
| ARR > CHANGE, stopped | LEFT | The same, to the book before (down to 1) |
| ARR > CHANGE, playing | LEFT / RIGHT | Nothing changes; it says STOP TO CHANGE BOOK |
| ARR > BOOKS (stock's CHANGE, renamed) | CUE + YES | Add the arrangement under the cursor to the end of the open setlist (B2 5 ADDED TO LIST 3) |
| ARR > BOOKS, stopped | CUE + LEFT / RIGHT | Move the arrangement under the cursor one place up or down in its book |
| Arranger menu | SETLSTS (stock's CHAIN, replaced) | Open the setlists page |
| SETLSTS page | LEFT / RIGHT | The setlist before or after (1 to 16) |
| SETLSTS page | UP / DOWN | Move the cursor |
| SETLSTS page, stopped | YES | Start the setlist from the song under the cursor (PLAY then plays it) |
| SETLSTS page | CUE + LEFT / RIGHT | Move the song under the cursor up or down |
| SETLSTS page | FUNC + PLAY | Take the song out of the setlist (the arrangement stays in its book) |
| SETLSTS page | FUNC + REC / CUE + FUNC + REC | Copy the song / the whole setlist |
| SETLSTS page | FUNC + STOP | Paste after the cursor |
| SETLSTS page | FUNC + YES | Name the setlist (stock's name editor, 15 characters) |
| SETLSTS page | NO | Close it |
| Main screen, a setlist playing | hold CUE, press YES | Show the setlist: the playing song ▶, the next one >; UP/DOWN move the cursor, YES makes the song under it the next one at once; releasing CUE closes it |
| ARR > BOOKS, stopped | (opening it) | Ends a setlist: songs edited while it played are written to their own files, the book comes back |

A book never used starts as 8 empty arrangements. Each book comes back on the arrangement you last chose in it (a
book not visited yet on arrangement 1), with the list's cursor on it; after a power cycle or a project load every book
but the one in use starts on arrangement 1 again. Switching takes about as long as stock's SYNC TO CARD and ends with
its NOW SAFE TO EJECT! message. Without a project loaded it says NO PROJECT; while the card is busy, CARD BUSY. If an
arrangement file does not read, it says BOOK FILE ERROR, that arrangement shows empty, and its file is left as it is
unless you edit that arrangement.

## Usage

1. Stop the transport.
2. Open ARR > BOOKS (stock's CHANGE). MKII: ARR, then BOOKS. MKI: FUNC + PATTERN switches arrangement mode on, FUNC +
   BANK opens the arranger menu, then BOOKS.
3. Press RIGHT or LEFT. The label at the top right shows the book, for example BOOK 3.
4. Choose one of its 8 arrangements as usual.

### Setlists

1. In ARR > BOOKS, put the cursor on an arrangement and press CUE + YES: it is added to the open setlist (LIST 1 at
   the bottom right). Add songs from any book this way.
2. Open SETLSTS in the arranger menu: the setlist, its songs as `book.arrangement NAME`. Move them with CUE +
   LEFT/RIGHT, name the setlist with FUNC + YES.
3. Put the cursor on the first song, press YES, close the menu with NO, and press PLAY. Each song is followed by the
   next; the last ends as any arrangement ends.
4. Opening ARR > BOOKS (stopped) ends the setlist and brings the book back.

### Quick tutorial

1. In ARR > CHANGE, press RIGHT: BOOK 2, with 8 empty arrangements.
2. Choose arrangement 1, open the editor and add a few rows.
3. Back in ARR > CHANGE, press LEFT: BOOK 1, your original arrangements.
4. Press RIGHT again: book 2's rows are back.
5. Press LEFT to return to book 1 before you finish.

## Compatibility and limitations

- **Setlists borrow the open book's arrangements 7 and 8.** While a setlist is active, slots 7 and 8 hold copies of
  setlist songs; nothing of them is written to the book's files, and the book's own arrangements 7 and 8 come back from
  their files when the setlist ends, after a load, a reload or a power cycle (the battery record keeps the setlist's
  state). **End a setlist (open ARR > BOOKS) before removing the module**: without the module, stock's next save would
  write those copies into the book's arrangements 7 and 8.
- **Edits during a show** are written to each song's own file when the setlist ends (opening ARR > BOOKS, stopped).
  A RELOAD or a power cycle drops edits not yet written, as stock does for unsaved changes. A song renamed while a
  setlist plays keeps its old name.
- **A project comes back into its setlist.** LOAD and RELOAD PROJECT return to the setlist and song saved with the
  project, a power cycle to the song playing. Without the module the project opens on book 1.
- **Changes to stock flows (only while the module is installed):** the arranger menu reads BOOKS for CHANGE and
  SETLSTS for CHAIN (CHAIN itself is replaced: a two-song setlist does its job, across books too); the menu's title
  reads `ARR B2:3:NAME` (book, arrangement, name) or, in a setlist, `STL:<setlist>:<song no>:<song name>`; the
  arranger rows' chain line reads `>` and the next song's name; while a setlist plays, the main screen's Part box and
  pattern line show the setlist instead of the Part and the pattern. CUE + YES on the main screen is taken only while a
  setlist plays (stock does nothing with it there).

- **Takes:** LEFT and RIGHT in ARR > CHANGE (stock does nothing with them there), the top right corner of that list
  for the label, and stock's arrangement writer, which it steps into at its entry: on book 1 stock writes arr01 to
  arr08 exactly as always, on another book that book's 8 files are written instead. For setlists: the arranger menu's
  CHANGE and CHAIN entries (their names, CHAIN's routine and icon), the operands of the menu title's index and format
  and of the arranger rows' chain line (pointed at the module's own text; stock's strings stay untouched), CUE + YES
  and CUE + LEFT/RIGHT in ARR > BOOKS, and CUE + YES on the main screen while a setlist plays. No other module or base
  hook uses these sites (`npm run modules:compat`).
- **A change to a stock flow: LOAD PROJECT and arranger mode.** Stock's LOAD (PROJECT > CHANGE) opens a project in
  pattern mode, while RELOAD keeps the mode. With the module, a project that was left (switched, synced or saved) in
  arranger mode opens in arranger mode again, after its book and arrangement are restored; one left in pattern mode
  opens as on stock. Why: a project of songs is reopened to play them, and the developer asked for it. Instead
  considered: leaving it to the musician (FUNC + PATTERN after every load). The module turns arranger mode on with
  stock's own routine (FUNC + PATTERN's) and never off. Only while the module is installed. Checked beside it: RELOAD,
  and a project left on book 1.
- **Stopped only.** A switch writes and reads the card through stock's own SYNC TO CARD job, which stock runs only
  stopped.
- **A project comes back where it was left.** A power cycle keeps the book in use (the Octatrack keeps its
  arrangements in battery memory). LOAD PROJECT returns to the book and arrangement of the last switch, SYNC TO CARD
  or SAVE PROJECT, and RELOAD PROJECT to those of the last SAVE PROJECT, as stock does for its own files: the module
  keeps them in `arrbook.work` (and `arrbook.strd`). Stock first loads book 1; on another book the module then
  switches as LEFT/RIGHT do, about two seconds after stock's load has finished. Without the module a project opens on
  book 1.
- **Switch back to book 1 before removing the module.** Without the module, stock's next save writes whatever book is
  in its 8 slots into arr01 to arr08. Leaving book 1 always keeps a backup of it (`arr01.bak` to `arr08.bak`, stock
  files too), and arriving at book 1 loads that backup and writes it back into arr01 to arr08; so if it happens,
  install the module again and step to book 1.
- **Saved with the project.** SAVE PROJECT copies arr09 to arr128 to their `.strd` copies as stock does for arr01 to
  arr08, and RELOAD PROJECT copies them back. Stock's own project copy functions copy book 1 only; copying the project
  folder copies every book.
- **Battery RAM:** 20 bytes at 0x100f8f10 (sdk/machines/octatrack/battery-ram.json) remember which book the slots
  hold, whether it was read in full, and whether slots 7 and 8 hold a setlist's songs (and which song plays).
- **Memory:** about 98 KB, most of it the setlist's songs (16 arrangements).

## Tests and measurements

See [TESTING.md](TESTING.md). The OS 1.40C addresses and how they were found are in [RESEARCH.md](RESEARCH.md).

## Authorship and licences

Original module by Andrey Sidorkin, MIT (LICENSE). No firmware, stock code or stock data is included.

## Screens and audio

![ARR > CHANGE on book 1](media/ot-change-book.png)

Emulator capture (ot_emu), not the unit. No audio: the module makes no sound.

## Develop

- Hear it: `npm run module:dev -- songbook` builds it and tries it on your unit over USB; `--watch` does that again on every save.
- Check it: `npm run module:doctor -- --draft songbook`, and `python3 -B sdk/drafts/songbook/verify.py` for the host gate.
- Before you write ColdFire code: the traps in sdk/octabam/AGENTS.md, and docs/OCTATRACK_API.md for the hooks a module can use.
