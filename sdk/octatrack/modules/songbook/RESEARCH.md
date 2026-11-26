# Songbook research (OS 1.40C)

Found by disassembling the developer's own OS 1.40C (`m68k-elf-objdump -m m68k:cfv4e`; the image file starts at
0x40000400, so `--adjust-vma=0x40000400` gives real addresses) and checked under ot_emu. Addresses only; no stock
code is copied here.

## Where stock keeps arrangements

| What | Address |
| --- | --- |
| Pointer to the arrangement in use | 0x10000000 |
| 8 working arrangements, 5652 bytes each (20-byte header: name, flags, row count; 256 rows of 22 bytes) | 0x10000004 |
| 8 saved copies (ARR SAVE / RELOAD copy working and saved in RAM only) | 0x1000b0a4 |
| 8 "has a saved copy" flags | 0x10016144 |
| Changed mask, one bit an arrangement, cleared when written | 0x1001614c |
| ARR > CHANGE's window word, nonzero only while the list is open | 0x400c0b40 |
| ARR > CHANGE's list drawn into that window: stock calls it once, from the open routine 0x40058abc; a filled bar marks the chosen arrangement, a frame the cursor, `*` unsaved changes | 0x40036e18 |
| ARR > CHANGE's first row shown, and its cursor row | 0x400c0ba0, 0x400c0ba4 |
| select_arr(n): stock's choice of arrangement n (index, pointer, playback reset; -1 while the arranger is busy) | 0x400a03a0 |
| The arrangement chosen, 0-7 | 0x100b14d1 |
| Arranger mode, and stock's switch into it (FUNC + PATTERN's; nothing if already on) | 0x460d1aec, 0x4005200c |
| project.work's ARRANGEMENT_MODE line sets 0x80000010 and 0x100b14dc when a project is read (0x40087e20); LOAD's request saves the current project after switching arranger mode off, so reopening the same project starts in pattern mode | 0x40087e20 |
| The mode stock applies at the end of a load (0x4002390a calls 0x40055fd8 with it). LOAD's request 0x40023b68 switches arranger mode off and clears it before posting the load job | 0x80000010 |

Battery RAM 0x100f8f06-0x100f8ffc was free; the module claims 0x100f8f10-0x100f8f20 (battery-ram.json).

## Stock's arrangement files

- One file a slot, `%s/arr%02d.work` (and `.strd`), 11,336 bytes: the `FORM … DPS1ARRA` header, the working
  arrangement, the saved copy, all 8 slots' "has a saved copy" flags and a checksum.
- `0x40089644(file, slot)` writes one slot into an open file; `0x4008b4d4(file, slot, 0)` reads one file into a slot,
  sets all 8 flags from it and clears that slot's changed bit (third argument nonzero: check only). Both return < 0 on
  failure; the reader -52 for a file from a newer OS.
- `0x400915ac(dir, mask, …)`, the per-arrangement writer: for each slot changed (or whose file is missing) it opens
  `arrNN.work` and calls 0x40089644. Its entry is two instructions, 8 bytes (`link.w %fp,#-324; movem.l
  %d2-%d7/%a2-%a5,(%sp)`, SHA-256 b692e058…); nothing branches into 0x400915ae-0x400915b4, and no module or base hook
  patches it.
- ARR > CHANGE's FUNC + PLAY (0x400610ee) clears the arrangement under the cursor with stock's undo (pressing it again
  restores it) and does not set its changed bit; the SYNC job's writer call asks for all 8 (mask 0xff), so stock
  saves it anyway.
- `0x4008fd28(dir, mask)` reads the masked slots' files when a project is loaded. A power cycle does not: on the
  developer's MKI the arrangements in battery RAM came back as they were (book 3 stayed book 3).
- `0x40016388(to, from, 0)` copies a file; SAVE and RELOAD PROJECT use it for arr01-08 (-12: no such file, decided by the
  file system's exists(path) behind the pointer at `0x46c823fa`, before any open; the open `0x40016864` passes the file
  system's own error codes through, so a failed open alone does not say the file is missing).
- File layer: open 0x40016864 `(file, path, mode, buffer, size)`, close 0x4001677c; the project directory
  0x40025230(0, 0); `clear_arr` 0x400a041c(n).

Under ot_emu (card written back with `--card-rw`) the module's arr09-arr16 came out 11,336 bytes with stock's header,
differing from stock's arr01 in 9 bytes (rows and checksum).

## Project hooks, as the unit raised them

RELOAD PROJECT: ev_project_reloaded, then ev_banks_load, and no ev_banks_loaded (observed on the developer's MKI with
the module's event log, 18 November 2026). LOAD PROJECT: ev_banks_load and no ev_banks_loaded either (same log). LOAD then also raises ev_project_new (stock's new-project step)
after the banks, and reads book 1 again: a load is several jobs, and the module waits two quiet seconds after the last
project event before switching back.

## LOAD and RELOAD are two jobs

The first (0x40085276) reads the project file and banks (0x40090504, where ev_banks_load and ev_banks_loaded sit) and
only checks the arrangement files (0x4008fc54 calls the reader with its third argument 1). The second, job 27
(0x40085342), calls the new-project step 0x400909d8 (ev_project_new) first, then the project data and banks, and
last loads arr01-08 into the slots (0x4008fd28, mask 0xff). Until then the slots hold what they held before.

## LOAD of another project: the project directory and the arranger

Stock's project directory (0x40025230, built on the project name at 0x100f8378) still names the last project at
ev_banks_load and at ev_banks_loaded when another project is loaded: the LOAD request (0x40023b68) puts the new name
into its job's record (0x460bdec1), and the name in use changes only later (the developer's MKI, 24 November 2026: a
file read at either hook was the last project's). The module compares the two names and reads the new project's
arrbook.work in a card job of its own after the load.

After such a load the arranger's run state (0x800066a0: 1 playing, 2 held by a STOP, set by 0x4009f784) can stay
set with nothing playing, and select_arr (0x400a03a0) refuses every choice while it is set (seen on the same unit:
the book came back on arrangement 1). Stock's arranger reset (0x4009ae48: run state 0, row states cleared, CHAIN -1)
runs at start-up (0x4001fc84) and for each new project (0x40025af2); the module runs it before its choice on a
return after a load, once half a second of refusals has passed and nothing plays.

## Card jobs

`0x40022cd4(0)` posts job 7, SYNC TO CARD's "write the project". It returns a negative code without posting: -7 with
no project loaded, -3 or -53 while a job or a save is pending. Stopped, the job runs at once; playing, stock asks
PLAYBACK WILL BE STOPPED, so the module switches only when stopped.

## The design

The module steps into the writer's entry. On book 1 it runs stock's writer unchanged. On another book it writes the
book's 8 files together (every file carries all 8 flags, and reading sets all 8, so a book's files must agree). A
pending switch then backs up book 1 when leaving it and reads the next book's files; arriving at book 1 reads the
backup and marks all 8 changed, so stock's next write repairs arr01-08. A file that does not read leaves its slot
empty and is never written back unless that arrangement is edited. Which book the slots hold survives a reinstall in
the battery record, valid only while a fingerprint of the slots still matches; a project load always means book 1.

## Setlists (0.2.0)

- **CHAIN:** stock's chain target is `0x80006694` (-1 none, taken once). The arranger step takes it past an
  arrangement's last row (`0x4004a7e2`): index (`0x80000005`, `0x100b14d1`) and pointer (`0x10000000`) to that slot, row
  0, playing on; the start routine (`0x400a0230`) takes it when it starts an arrangement with no rows. Past the last row
  with no chain the arranger ends (`0x4004a75e`). Playing a setlist is a chain from slot 7 to 8 and back, the module
  putting the song after next into the freed slot; stock clears the chain only when taking it, but the module sets it
  again every frame.
- **The arranger menu:** descriptor `0x400be7e6` (2 rows of 4), entries of 16 bytes `{name[8], icon, routine}` from
  `0x400be766`: CHANGE `0x400be786` (routine `0x40058abc`), CHAIN `0x400be796` (icon `0x400be5c2`, routine
  `0x40058b38`). Names are drawn with `%s` (`0x400488c4`), so 7 letters at most; an icon is two pictures of
  `{27, 13, 1, image, mask}`, 27 columns of a word each, the top row in bit 31.
- **The menu title** is `sprintf("ARR %d:%s", index + 1, name)` at `0x40057f1c`: the index from `0x80000005`
  (operand at `0x40057f48`), the format `0x400b47e6` (operand at `0x40057f54`).
- **The chain line** in the arranger rows is drawn from `"CHAIN ARR. %d"` (`0x400b4652`, operands at `0x40048eb8` and
  `0x400496e6`). Its `"%d"` at `0x400b465d` is stock's shared number format (76 users: tempo, MIDI channel, bank);
  writing it blanked them all. Writing into a declared stock change at run time also makes the loader's check of its
  bytes fail, and every later install is refused as busy (refusal 5) until a power cycle. The module points the
  operands at its own text instead.
- **Redrawing the rows:** stock posts token `0x400c0c41` to the UI queue `0x460d17ae` (`0x40000c3c`) as it takes a
  chain; posting it again draws the rows with a chain set after stock's own drawing.
- **Key routing:** the handler of each key code at `0x46c7d8de + code * 0x18`. YES on the main screen `0x4005e4c8`,
  in the arranger menu `0x4003395c`, in the editor `0x40058f34`; with CUE held stock routes YES elsewhere
  (`0x400402ec`), so the module notes the screen when CUE goes down. CUE + BANK opens stock's SELECT BANK and CUE + NO
  its DISARM ALL on the main screen; CUE + YES and CUE + UP/DOWN do nothing there (ot_emu).
- **The name editor:** `0x4007e664(length, title, buffer, 1, done(ok))`, as RENAME (`0x40049c88`) calls it.
- **Text in stock's font:** `0x40012bd8(0x400ba876, surface, x, 59 - top, -1, text)` on a copy of the surface at
  `0x400bf10a` aimed at the module's frame (as pattern-sample-locks does).
- **0x1001614c** is set by edits and by stock's arrangement writer after it writes a slot (`0x40091748`); the writer
  chooses slots by its mask or a missing file (`0x40091680`), never by this byte.
- **Loads and slots 7 and 8:** about 10 s into a RELOAD, stock reads arr01-arr08 into the slots (ot_emu); after a power
  cycle the slots keep what they held (battery RAM) and the module comes up after stock's project load, so no load hook
  reaches it. The module keeps "slots 7 and 8 hold setlist songs" and the playing song in its battery record (byte 7),
  cleared only once the book is read back from its files.
