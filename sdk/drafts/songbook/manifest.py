"""SONGBOOK -- 128 arrangements per project, as 16 books of 8 (setlists follow in a later version).

Book 1 is stock's arr01-arr08; book 2 is arr09-arr16, and so on to book 16, arr121-arr128: ordinary stock
arrangement files in the project directory, written and read by stock's own routines. In ARR > CHANGE, stopped,
LEFT/RIGHT step the book.

A switch posts stock's own "write the project" job (SYNC TO CARD's, 0x40022cd4) and does its card work inside it, in
stock's card task: the module steps into stock's per-arrangement writer at its entry (0x400915ac, the one site it
patches; no hook or other module uses it). On book 1 stock writes arr01-08 as always; on another book the book's 8
files are written instead; then the next book is loaded. Leaving book 1 keeps a backup (arr01.bak .. arr08.bak);
arriving at book 1 loads it and marks all 8 changed, so arr01-08 are rewritten from it. SAVE/RELOAD PROJECT, project
loads and the install use the base's project hooks (runtime.json). Which book the slots hold is kept in the module's
battery-RAM range with a fingerprint of the slots. OS 1.40C addresses in songbook.c and RESEARCH.md.
"""

import os

from remix.schema import Category, Detour, Gate, Kind, Linked, Module, Poke, Proof, SymbolRef
from remix.stock_guard import stock_guard

_HERE = os.path.relpath(os.path.dirname(os.path.abspath(__file__)))
WRITER = 0x400915ac  # stock's arrangement writer: link.w %fp,#-324 ; movem.l %d2-%d7/%a2-%a5,(%sp)
MENU_NAME = 0x400be796     # the Arranger menu's fourth entry (descriptor 0x400be7e6): its 8-byte name, CHAIN
MENU_HANDLER = 0x400be7a2  # the same entry's YES routine (stock's CHAIN list, 0x40058b38)
CHAIN_TEXT = 0x400b4652    # "CHAIN ARR. %d": the arranger rows' chain line (0x40048eb6, 0x400496e4)

MODULE = Module(
    name="songbook",
    key="SONGBOOK",
    kind=Kind.CF_PATCH,
    category=Category.MACHINES,  # display group "machines and the sequencer"; the site category is system
    author="Andrey-Sidorkin",
    author_url="https://github.com/Andrey-Sidorkin",
    proof=Proof.UNTESTED,
    proof_note="Host gate (89 checks); the developer's MKI checks, last on build b976e98ff (base 0.7.13, the test procedure and the owner's reinstall and Undo checks); the book 1 fix since is host-tested only.",
    doc="128 arrangements per project as 16 books of 8, all stock arrangement files: arr01-arr08 stay book 1.",
    linked=(Linked("songbook", os.path.join(_HERE, "songbook_core.s")),),
    detours=(Detour(WRITER, stock_guard(WRITER, 8, "b692e058a2f3049810c18b0571d61c085c3daba589a09b531fed42f528b96012"),
                    "songbook", "as_writer_entry",
                    "stock's arrangement writer: on book 1 stock's own, on another book its arr files; a switch's load",
                    kind="jmp", pad_to=8),),
    pokes=(Poke(CHAIN_TEXT, stock_guard(CHAIN_TEXT, 14, "d4ec8d003fc22ad62c6f9a94e70bfa4e638a41d3b5c1b1eb6b4df9cf7d3dc23d"),
                b"NEXT SONG\0\0\0\0\0", "the arranger's chain row reads NEXT SONG: with SETLSTS in CHAIN's place, a chain is a setlist's next song"),
           Poke(MENU_NAME, stock_guard(MENU_NAME, 8, "78048c5f30cf2d55c3a3b36c966c7e6b6c55d55df4295ee1c9129101860b71c9"),
                b"SETLSTS\0", "the Arranger menu's CHAIN item renamed SETLSTS (stock draws the name with %s: 7 letters)"),),
    symbol_refs=(SymbolRef(MENU_HANDLER, 0x40058b38, "songbook", "as_setlists_open",
                           "the Arranger menu's CHAIN item opens the SETLSTS page instead of stock's CHAIN list"),),
    gates=(Gate("modules/songbook/verify.py", remix_arg=False),),
)
