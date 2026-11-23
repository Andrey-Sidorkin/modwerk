/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Andrey Sidorkin
 *
 * SONGBOOK 0.1: 128 arrangements per project, as 16 books of 8, every one an ordinary stock arrangement file.
 *
 * Book 1 is stock's: arr01-arr08. Book 2 is arr09-arr16, and so on to book 16, arr121-arr128, in the project
 * directory beside them, each written and read by stock's own routines, so every file is in stock's format (the
 * arrangement, the copy ARR SAVE keeps, the flags). Stock always works on the 8 in its slots; switching books writes
 * the slots to their book's files and reads the next book's files in.
 *
 *   In ARR > CHANGE, stopped: LEFT / RIGHT step to the book before / after.
 *
 * The card is used only in stock's card task: a switch posts stock's "write the project" job (SYNC TO CARD's,
 * 0x40022cd4), whose per-arrangement writer (0x400915ac) this module steps into. On book 1 stock's writer runs as
 * always; on another book the book's 8 files are written instead. Then the next book is loaded. Leaving book 1 keeps a
 * backup of it (arr01.bak .. arr08.bak, stock's format); arriving at book 1 loads that backup and marks all 8 changed,
 * so stock's next write puts them back into arr01-08 even if those files were overwritten meanwhile.
 *
 * Every arrangement file stores all 8 slots' "has a saved copy" flags, and reading one sets all 8: the module always
 * writes a book's 8 files together, so whichever is read last holds the book's own flags.
 *
 * A project load makes stock read arr01-08 into the slots again, so the slots then hold book 1. A power cycle does not
 * (the developer's MKI kept book 3 through one): the slots keep the book they held.
 * After an install the module learns which book the slots hold from a fingerprint of the slots it keeps in battery RAM
 * (battery-ram.json): matching, they still hold the book it last loaded; otherwise book 1.
 *
 * Addresses: OS 1.40C (RESEARCH.md). generate.py compiles this into songbook_core.s; verify.py runs it on the
 * host against simulated memory and simulated stock file routines (AS_HOST). */
#include <stdint.h>

#define K_RIGHT 0x21
#define K_LEFT 0x34
#define K_FUNC 0x2d
#define K_PATTERN 0x2e
#define K_CUE 0x2a
#define K_YES 0x31
#define K_NO 0x32
#define K_UP 0x33
#define K_DOWN 0x20
#define K_PLAY 0x28
#define K_STOP 0x27
#define K_REC 0x29

#define BOOKS 16u
#define SLOTS 8u
#define BLOCK 5652u                  /* one arrangement in battery RAM: a 20-byte header, 256 rows of 22 bytes */
#define WORKING 0x10000004u          /* the 8 working blocks, then the 8 blocks ARR SAVE keeps, then 8 flags */
#define CHANGED 0x1001614cu          /* one bit a slot: edited since read; stock's writer sets it again after writing (0x40091748) */
#define RECORD 0x100f8f10u           /* the module's battery-RAM record (battery-ram.json): magic, book, backup, sum, serial, events */
#define CHANGE_WINDOW 0x400c0b40u    /* ARR > CHANGE's window; 0 while closed */
#define PLAYING 0x800065b8u          /* the transport */

/* Stock routines, OS 1.40C. */
#define POST_WRITE 0x40022cd4u       /* post_write(0): SYNC TO CARD's job; < 0 refused */
#define PROJDIR 0x40025230u          /* projdir(0, 0) -> the project directory */
#define SPRINTF 0x40013a08u
#define F_OPEN 0x40016864u           /* (fo, path, mode, buffer, size) -> < 0 failed */
#define F_CLOSE 0x4001677cu          /* (fo) -> < 0 failed */
#define F_COPY 0x40016388u           /* (to, from, 0) -> < 0 failed, -12 no such file */
#define FS_EXISTS 0x46c823fau        /* -> the file system's exists(path): nonzero when there (F_COPY's own -12 test) */
#define ARR_WRITE 0x40089644u        /* (fo, slot) -> < 0 failed: one slot in stock's file format */
#define ARR_READ 0x4008b4d4u         /* (fo, slot, 0) -> < 0 failed: one file into a slot */
#define CLEAR_ARR 0x400a041cu        /* clear_arr(slot): empty rows, no saved copy, default name */
#define MESSAGE 0x4005a2b8u          /* message(text, 0x30), UI task only */
#define MESSAGE_UP 0x460d1e70u       /* stock's message box window, 0 while none is open */
#define MESSAGE_CLOSE 0x40056bec     /* closes it (stock's own, before it opens another) */
#define DRAW_CHANGE 0x40036e18u      /* draws ARR > CHANGE's list into its open window (stock: once, when it opens) */
#define SELECT_ARR 0x400a03a0u       /* select_arr(n): stock's choice of arrangement n (index, pointer, playback reset) */
#define INDEX 0x100b14d1u            /* the arrangement chosen, 0-7 */
#define ARR_MODE 0x460d1aecu         /* arranger mode on (FUNC + PATTERN) */
#define ARR_MODE_WANTED 0x80000010u  /* the mode stock applies when a load ends (0x4002390a); LOAD leaves it 0 */
#define ARR_MODE_APPLY 0x40055fd8u   /* stock's end of a load: arranger mode off, then on if asked (0x4002390a) */
#define ARR_MODE_ON 0x4005200cu      /* stock's switch into arranger mode (FUNC + PATTERN's, 0x4006103e); nothing if on */
#define LIST_TOP 0x400c0ba0u         /* ARR > CHANGE: the first row shown */
#define LIST_CURSOR 0x400c0ba4u      /* ARR > CHANGE: the cursor's row (stock opens the list with it on the choice) */
#define POST_EVENT 0x40000c3cu      /* post(queue, token): an event to a task's queue */
#define UI_QUEUE 0x460d17aeu        /* the UI task's queue */
#define ARR_EVENT 0x400c0c41u       /* the token stock posts as the arranger takes a chain (0x4004a834): the rows drawn again */
#define CHAIN 0x80006694u           /* stock's CHAIN: the slot the arranger moves to past its last row, -1 none (0x4004a7e2) */
#define KEYS 0x46c7d8deu            /* each key code's routed press handler, 0x18 apart (as arr-rows reads it) */
#define MAIN_YES 0x4005e4c8u        /* YES's handler on the main screen (measured under ot_emu: menu 0x4003395c, editor 0x40058f34) */
#define LIST_CHOICE 0x400c0ba8u      /* ARR > CHANGE: the arrangement under the cursor, as YES selects it (0x400612b4) */

#define REC_MAGIC 0x41534231u        /* "ASB1" */
#define PLACE_MAGIC 0x41534250u      /* "ASBP": arrbook.work, the book in use and each book's last choice */
#define F_READ 0x40016564u           /* (fo, dst, n) -> 1 done */
#define F_WRITE 0x400166b8u          /* (fo, src, n) -> 1 done */
#define IOB_LEN 0x1000u
#define SUM_LEN (2u * SLOTS * BLOCK + SLOTS)

#ifdef AS_HOST
uint8_t host_rd8(uint32_t a);
void host_wr8(uint32_t a, uint8_t v);
int host_post_write(void);
const char *host_projdir(void);
int host_arr_write(const char *path, unsigned slot);
int host_open_read(const char *path);
int host_exists(const char *path);
int host_read_slot(const char *path, unsigned slot);
int host_copy(const char *to, const char *from);
int host_put(const char *path, const void *src, unsigned n);
int host_get(const char *path, void *dst, unsigned n);
void host_clear_arr(unsigned slot);
void host_message(const char *text);
int host_stock_writer(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4);
#define rd8(a) host_rd8(a)
static void wr8(uint32_t a, uint32_t v) { host_wr8(a, (uint8_t)v); }
#else
static uint32_t rd8(uint32_t a) { return *(volatile uint8_t *)a; }
static void wr8(uint32_t a, uint32_t v) { *(volatile uint8_t *)a = (uint8_t)v; }
#endif
static uint32_t rd32(uint32_t a) { return (uint32_t)rd8(a) << 24 | (uint32_t)rd8(a + 1) << 16 | (uint32_t)rd8(a + 2) << 8 | rd8(a + 3); }
static void wr32(uint32_t a, uint32_t v) { wr8(a, v >> 24); wr8(a + 1, v >> 16); wr8(a + 2, v >> 8); wr8(a + 3, v); }

#define MAGIC 0x41535335u /* "ASS5": card builds do not zero .bss; the size beside it catches a layout change */

/* Setlists: 16 of up to 16 songs, each a book, its slot and the name it had (arrlists.work). */
#define LISTS 16u
#define LIST_LEN 16u
#define NAME_LEN 16u
struct sl_entry { uint8_t book, slot; char name[NAME_LEN]; };
struct sl_lists {
    uint8_t magic[4];        /* "ASL2" ("ASSL": the same without the names, still read) */
    uint8_t open;            /* the setlist CUE + YES adds to, 0 = 1 */
    uint8_t reserved[3];
    uint8_t count[LISTS];
    struct sl_entry e[LISTS][LIST_LEN];
    char name[LISTS][16];    /* stock's name editor's (15 characters and its end); empty: "SETLIST n" */
};
#define LISTS_V1_BYTES (8u + LISTS + LISTS * LIST_LEN * sizeof (struct sl_entry))

struct as_state {
    uint32_t magic;
    uint32_t size;           /* sizeof (struct as_state) when it was set up: another layout starts afresh */
    uint8_t book;            /* the book in the slots, 0 = book 1 */
    int8_t pending;          /* the book a posted write job loads, -1 none */
    uint8_t failed;          /* the card task could not finish a switch: the UI task says so */
    uint8_t backup;          /* arr01.bak .. arr08.bak hold book 1 as it was left */
    uint8_t ready;           /* the battery record was read since install */
    uint32_t seq;            /* the serial this copy last wrote into the record: another one there means another build ran */
    uint8_t damaged;         /* a file of the book in the slots did not read: write back only what is edited */
    uint8_t redraw;          /* a switch finished: the UI task chooses the book's arrangement and redraws the list */
    uint8_t reapply;         /* the switch was a return after a load: rebuild the arranger as stock's end of a load does */
    uint8_t select_tries;    /* frames stock refused that choice (a change of its own still pending, as after RELOAD) */
    uint8_t last[BOOKS];     /* the arrangement last chosen in each book (0 = arrangement 1), kept in arrbook.work */
    int8_t restore;          /* a project was loaded on this book: the UI task switches to it once stock is done, -1 none */
    uint16_t restore_ticks;
    uint8_t mode_on;         /* after a load: the project was left in arranger mode, so turn it on again */
    uint8_t mode_pref;       /* arranger mode as the musician left it: off only after their own FUNC + PATTERN */
    uint8_t func_down, mode_key, mode_key_ticks, mode_seen;
    uint8_t quiet;           /* frames since the last job, switch or mode change, after a load */
    uint16_t since_hook;     /* frames since stock's last project event: a load is several jobs */
    uint8_t loading;         /* a load or reload began: the slots hold the old book until stock's new-project step */
    uint8_t reloaded_slots;  /* that step came: stock now puts arr01-08 in the slots, and a return may follow it */
    uint8_t hush;            /* frames after a switch of ours in which the job's NOW SAFE TO EJECT! is closed */
    int8_t restore_rc;       /* the last answer to posting the return after a load, for the diagnostic word */
    uint8_t loads, reloads;  /* project loads and reloads seen, for the diagnostic word */
    uint8_t place[24];       /* arrbook.work: magic, book, the 16 last choices */
    struct sl_lists lists;   /* the setlists, as in arrlists.work once lists_dirty is 0 */
    uint8_t lists_dirty;     /* changed since written: the card task writes them with the next sync */
    uint8_t lists_unread;    /* installed into an open project: arrlists.work not read yet (the next card job reads it) */
    uint8_t lists_req;       /* a card job posted only to read them */
    uint8_t reread_posted;
    uint8_t reread;          /* installed while a setlist played (the record says so): slots 7 and 8 hold its songs, not
                              * the book's; the next card job reads the book again and writes none of it */
    uint8_t cue_down;        /* CUE held: YES adds to the setlist, LEFT/RIGHT move an arrangement in the book */
    uint8_t cue_yes;         /* a YES taken with CUE: its release is ours too */
    char msg[24];            /* the text of the module's last message box (stock may draw it again later) */
    uint8_t page;            /* the SETLSTS page is open (the Arranger menu's CHAIN item, replaced) */
    uint8_t page_cur, page_top;  /* its cursor (a song of the open setlist) and the first song shown */
    uint8_t page_play;       /* a FUNC + PLAY taken on the page: PLAY's release is ours too */
    uint8_t page_stop;       /* a FUNC + STOP taken on the page: STOP's release is ours too */
    uint8_t clip_n;          /* songs copied on the page (FUNC + REC one, CUE + FUNC + REC the setlist), 0 none */
    struct sl_entry clip[LIST_LEN];
    /* A setlist playing: its songs in the module's memory, two of them at a time in slots 7 and 8 (SL_A, SL_B), the
     * next one chained by stock's own CHAIN. The book's other slots stay; its files are not written meanwhile. */
    int8_t sl_req;           /* YES on the page: the song to start from, for the card job; -1 none */
    uint8_t sl_active;       /* the slots hold setlist songs */
    uint8_t sl_go;           /* loaded: the UI task selects the first song and sets the chain */
    uint8_t sl_end;          /* ending: the card job writes edited songs back and reads the book again */
    int8_t sl_restart;       /* YES on the page while one plays: after ending, start again from this song; -1 none */
    uint8_t sl_n, sl_cur;    /* its songs, and the one playing (or chosen) */
    uint8_t sl_list;         /* the setlist playing (0 = 1) */
    uint8_t peek;            /* CUE + YES on the main screen: the playing setlist shown while CUE is held */
    uint8_t peek_cur;        /* there, the cursor (UP/DOWN) */
    uint8_t peek_next;       /* and the song marked to come next ('>'; a further YES moves it to the cursor); 0xff none */
    uint8_t cue_main;        /* CUE went down on the main screen (stock routes YES elsewhere once CUE is held) */
    uint8_t sl_cur_slot;     /* the slot that song is in (SL_A or SL_B) */
    uint8_t song_at[2];      /* the song in SL_A, SL_B; 0xff none */
    uint16_t sl_edited;      /* songs changed while they played: written to their own files at the end */
    struct sl_entry sl_e[LIST_LEN];
    uint8_t songs[LIST_LEN][BLOCK];
    uint16_t ticks;
    char path[260];
    char path2[260];
    uint8_t fo[24];
    uint8_t iob[IOB_LEN];
};
static struct as_state st;
/* The last four project events, newest lowest (the diagnostic word's top half): 1 load begins, 2 load done, 3 reload
 * done, 4 save done, 5 arrbook read (then its book), 6 arrbook missing, 7 return posted, 8 refused, 9 switch done,
 * A write without a switch, B nothing to return to, C arranger mode went on, D it went off, E a new project,
 * F the install handler fell back to book 1. */
static uint32_t evlog;
static void ev(unsigned code)
{
    evlog = evlog << 4 | (code & 15u);
    wr32(RECORD + 16, evlog);                            /* kept through a power cycle, to read it back after */
}
static struct as_state *volatile st_at = &st;

#ifdef AS_HOST
void as_host_reinstall(void) { st_at->magic = 0; } /* a fresh install: the module's memory is new */
static struct as_state saved_st;
void as_host_keep_memory(void) { saved_st = st; }     /* another build runs a trial over this one ... */
void as_host_back_memory(void) { st = saved_st; }     /* ... and this one comes back with its memory as it was */
#endif

static void title_follow(const struct as_state *s);
static struct as_state *ready(void)
{
    struct as_state *s = st_at;
    if (s->magic == MAGIC && s->size == sizeof *s) return s;
    uint8_t *p = (uint8_t *)s;
    for (unsigned i = 0; i < sizeof *s; ++i) p[i] = 0;
    s->magic = MAGIC;
    s->size = sizeof *s;
    s->pending = -1;
    evlog = rd32(RECORD + 16);                           /* the events before a power cycle */
    s->restore = -1;
    s->sl_req = -1;
    s->sl_restart = -1;
    s->lists_unread = 1;                                 /* a project may be open already: its setlists not read */
    s->mode_pref = rd32(ARR_MODE) ? 1 : 0;
    s->since_hook = 4000;
    title_follow(s);
    return s;
}

/* ---------------------------------------------------------------- stock calls */
#ifdef AS_HOST
static int post_write(void) { return host_post_write(); }
void host_post_event(void);
static void post_event(void) { host_post_event(); }
static const char *projdir(void) { return host_projdir(); }
static void clear_arr(unsigned slot) { host_clear_arr(slot); }
static void message(const char *t) { host_message(t); }
void host_draw_change(void);
static void draw_change(void) { host_draw_change(); }
void host_arr_mode_on(void);
static void arr_mode_on(void) { host_arr_mode_on(); }
int host_close_message(void);
static void close_message(void) { host_close_message(); }
int host_select_arr(unsigned n);
void host_arr_apply(int on);
static void arr_apply(int on) { host_arr_apply(on); }
static int select_arr(unsigned n) { return host_select_arr(n); }
static int stock_writer(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4) { return host_stock_writer(a1, a2, a3, a4); }
static int arr_write(struct as_state *s, unsigned slot) { return host_arr_write(s->path, slot) >= 0; }
static int open_read(struct as_state *s) { return host_open_read(s->path); }
static int file_exists(struct as_state *s) { return host_exists(s->path); }
static int read_slot(struct as_state *s, unsigned slot) { return host_read_slot(s->path, slot) > 0; }
static void close_read(struct as_state *s) { (void)s; }
static int copy_file(struct as_state *s) { return host_copy(s->path2, s->path); }
static int put_data(struct as_state *s, const void *p, unsigned n) { return host_put(s->path, p, n); }
static int get_data(struct as_state *s, void *p, unsigned n) { return host_get(s->path, p, n); }
#else
static int post_write(void) { return ((int (*)(int))POST_WRITE)(0); }
static void post_event(void) { ((void (*)(uint32_t, uint32_t))POST_EVENT)(UI_QUEUE, ARR_EVENT); }
static const char *projdir(void) { return ((const char *(*)(int, int))PROJDIR)(0, 0); }
static void clear_arr(unsigned slot) { ((void (*)(int))CLEAR_ARR)((int)slot); }
static void message(const char *t) { ((void (*)(const char *, int))MESSAGE)(t, 0x30); }
static void draw_change(void) { ((void (*)(void))DRAW_CHANGE)(); }
static void arr_mode_on(void) { ((void (*)(void))ARR_MODE_ON)(); }
static void close_message(void) { ((void (*)(void))MESSAGE_CLOSE)(); }
static int select_arr(unsigned n) { return ((int (*)(int))SELECT_ARR)((int)n); }
static void arr_apply(int on) { ((void (*)(int))ARR_MODE_APPLY)(on); }
int as_stock_writer(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4); /* below: stock's writer, entered past the site */
static int stock_writer(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4) { return as_stock_writer(a1, a2, a3, a4); }

typedef int f_open_t(void *, const char *, const char *, void *, unsigned);
static int f_open(struct as_state *s, const char *mode) { return ((f_open_t *)F_OPEN)(s->fo, s->path, mode, s->iob, IOB_LEN); }
static int f_close(struct as_state *s) { return ((int (*)(void *))F_CLOSE)(s->fo); }
typedef int f_io_t(void *, void *, unsigned);

/* n bytes to s->path / from it, through the module's own buffer. 1 when done. */
static int put_data(struct as_state *s, const void *p, unsigned n)
{
    if (f_open(s, "w") < 0) return 0;
    int ok = ((f_io_t *)F_WRITE)(s->fo, (void *)p, n) == 1;
    return f_close(s) >= 0 && ok;
}
static int get_data(struct as_state *s, void *p, unsigned n)
{
    if (f_open(s, "r") < 0) return 0;
    int ok = ((f_io_t *)F_READ)(s->fo, p, n) == 1;
    f_close(s);
    return ok;
}

/* One slot to s->path, by stock's own writer. 1 when written and closed. */
static int arr_write(struct as_state *s, unsigned slot)
{
    if (f_open(s, "w") < 0) return 0;
    int ok = ((int (*)(void *, unsigned))ARR_WRITE)(s->fo, slot) >= 0;
    return f_close(s) >= 0 && ok;
}

static int open_read(struct as_state *s) { return f_open(s, "r"); }
static int file_exists(struct as_state *s) { return (*(int (**)(const char *))FS_EXISTS)(s->path) != 0; }
static int read_slot(struct as_state *s, unsigned slot) { return ((int (*)(void *, unsigned, int))ARR_READ)(s->fo, slot, 0) >= 0; }
static void close_read(struct as_state *s) { f_close(s); }

/* s->path to s->path2 by stock's own copy. 1 copied, 0 no such file, -1 failed. */
static int copy_file(struct as_state *s)
{
    int rc = ((int (*)(const char *, const char *, int))F_COPY)(s->path2, s->path, 0);
    return rc >= 0 ? 1 : rc == -12 ? 0 : -1;
}
#endif

/* ---------------------------------------------------------------- the files */
/* "<project>/arr<NN>.<ext>": NN is 1-128 (two digits at least, as stock's arr01). */
static void arr_path(char *out, unsigned number, const char *ext)
{
    const char *dir = projdir();
    char *t = out;
    while (*dir && t < out + 240) *t++ = *dir++;
    *t++ = '/'; *t++ = 'a'; *t++ = 'r'; *t++ = 'r';
    if (number >= 100) *t++ = (char)('0' + number / 100);
    *t++ = (char)('0' + number / 10 % 10);
    *t++ = (char)('0' + number % 10);
    *t++ = '.';
    while (*ext) *t++ = *ext++;
    *t = 0;
}

static unsigned arr_number(unsigned book, unsigned slot) { return book * SLOTS + slot + 1; }

/* "<project>/<name>.<ext>": arrbook (where the project was left, so a load comes back to it), arrlists (the setlists). */
static void project_file(char *out, const char *name, const char *ext)
{
    const char *dir = projdir();
    char *t = out;
    while (*dir && t < out + 240) *t++ = *dir++;
    *t++ = '/';
    while (*name) *t++ = *name++;
    *t++ = '.';
    while (*ext) *t++ = *ext++;
    *t = 0;
}
static void place_path(char *out, const char *ext) { project_file(out, "arrbook", ext); }
static void lists_path(char *out, const char *ext) { project_file(out, "arrlists", ext); }

static void write_place(struct as_state *s)
{
    s->place[0] = 'A'; s->place[1] = 'S'; s->place[2] = 'B'; s->place[3] = 'P';
    s->place[4] = s->book;
    for (unsigned b = 0; b < BOOKS; ++b) s->place[5 + b] = s->last[b];
    s->place[21] = s->mode_pref;                         /* not stock's flag: LOAD clears it before its own save */
    place_path(s->path, "work");
    put_data(s, s->place, sizeof s->place);
}

/* arrbook.work into the memory of choices: the book the project was left on, or -1 when it has none. */
static int read_place(struct as_state *s)
{
    place_path(s->path, "work");
    if (!get_data(s, s->place, sizeof s->place)) { ev(6); return -1; }
    ev(5); ev(s->place[4]);
    if (s->place[0] != 'A' || s->place[1] != 'S' || s->place[2] != 'B' || s->place[3] != 'P'
        || s->place[4] >= BOOKS) return -1;
    for (unsigned b = 0; b < BOOKS; ++b) s->last[b] = (uint8_t)(s->place[5 + b] & 7u);
    s->mode_on = s->place[21] == 1;
    s->mode_pref = s->mode_on;
    return s->place[4];
}

/* A fingerprint of the slots: the working blocks, the saved blocks and their flags. */
static uint32_t slots_sum(void)
{
    uint32_t h = 2166136261u;
    for (uint32_t a = WORKING; a < WORKING + SUM_LEN; ++a) h = (h ^ rd8(a)) * 16777619u;
    return h;
}

static void remember(struct as_state *s)
{
    s->seq = rd32(RECORD + 12) + 1u;
    wr32(RECORD + 12, s->seq);
    wr32(RECORD, REC_MAGIC);
    wr8(RECORD + 4, s->book);
    wr8(RECORD + 5, s->backup);
    wr8(RECORD + 6, s->damaged);                         /* a book read only in part: kept through a reload (#1737) */
    wr8(RECORD + 7, (uint8_t)(s->sl_active || s->reread));   /* slots 7 and 8 hold setlist songs */
    wr32(RECORD + 8, slots_sum());
}

/* The 8 slots to a book's 8 files (book 0: book 1's backup, arr01.bak .. arr08.bak). 1 when all were written. */
static int write_book(struct as_state *s, unsigned book)
{
    int ok = 1;
    for (unsigned n = 0; n < SLOTS; ++n) {
        arr_path(s->path, arr_number(book, n), book ? "work" : "bak");
        ok &= arr_write(s, n);
    }
    return ok;
}

/* s->path into a slot, by stock's own reader. 1 read, 0 no such file (the slot is untouched), -1 failed.
 * An open that fails is "no such file" only when the file system says the file is not there and a second open fails
 * too: a file that is there but did not open is a failure, so its book is written back only where edited and its
 * saved arrangement survives (found by the owner in review, #1737). */
static int arr_read(struct as_state *s, unsigned slot)
{
    if (open_read(s) < 0) {
        if (file_exists(s) || open_read(s) < 0) return file_exists(s) ? -1 : 0;
    }
    int ok = read_slot(s, slot);
    close_read(s);
    return ok ? 1 : -1;
}

/* A book's 8 files into the slots; a file not there yet gives an empty arrangement, and so does one that does not
 * read. 1 when nothing failed. */
static int read_book(struct as_state *s, unsigned book, const char *ext)
{
    int ok = 1;
    for (unsigned n = 0; n < SLOTS; ++n) {
        arr_path(s->path, arr_number(book, n), ext);
        int r = arr_read(s, n);
        if (r <= 0) clear_arr(n);
        if (r < 0) ok = 0;
    }
    return ok;
}

/* Book 1 from its backup, slot by slot. A slot read from arr0N.bak is marked changed, so stock's next write puts it
 * back into arr0N.work (which may have been overwritten while the module was away). A backup file missing or not
 * readable leaves that slot to arr0N.work itself, unmarked, so a valid arr0N.work is never replaced by an empty
 * arrangement (found by the owner in review, #1737). Returns the mask of slots to mark; *ok 0 when a slot read neither. */
static unsigned restore_book1(struct as_state *s, int *ok)
{
    unsigned mark = 0;
    *ok = 1;
    for (unsigned n = 0; n < SLOTS; ++n) {
        arr_path(s->path, arr_number(0, n), "bak");
        if (arr_read(s, n) > 0) { mark |= 1u << n; continue; }
        arr_path(s->path, arr_number(0, n), "work");
        int r = arr_read(s, n);
        if (r <= 0) clear_arr(n);
        if (r < 0) *ok = 0;
    }
    return mark;
}

/* Only the slots edited, to their files: after a failed load, a slot that did not read keeps its file untouched. */
static int write_changed(struct as_state *s, unsigned book)
{
    int ok = 1;
    for (unsigned n = 0; n < SLOTS; ++n) if (rd8(CHANGED) >> n & 1) {
        arr_path(s->path, arr_number(book, n), "work");
        ok &= arr_write(s, n);
    }
    return ok;
}

/* ---------------------------------------------------------------- setlists */
/* An entry copied byte by byte: a struct assignment can compile to a call of memcpy, which the module has not got. */
static void entry_copy(struct sl_entry *to, const struct sl_entry *from)
{
    const uint8_t *f = (const uint8_t *)from;
    uint8_t *t = (uint8_t *)to;
    for (unsigned i = 0; i < sizeof *to; ++i) t[i] = f[i];
}

static void sl_clear(struct as_state *s)
{
    uint8_t *p = (uint8_t *)&s->lists;
    for (unsigned i = 0; i < sizeof s->lists; ++i) p[i] = 0;
    s->lists_dirty = 0;
    s->lists_unread = 0;
}

/* A slot's name, as stock's list shows it ("%.16s" of the block's first bytes). */
static void slot_name(char *out, unsigned slot)
{
    for (unsigned i = 0; i < NAME_LEN; ++i) out[i] = (char)rd8(WORKING + slot * BLOCK + i);
}

/* The arrangement at slot of book, added at the end of the open setlist: its position (1-16), 0 when full. */
static unsigned sl_add(struct as_state *s, unsigned book, unsigned slot)
{
    unsigned l = s->lists.open, n = s->lists.count[l];
    if (n >= LIST_LEN) return 0;
    struct sl_entry *e = &s->lists.e[l][n];
    e->book = (uint8_t)book;
    e->slot = (uint8_t)slot;
    slot_name(e->name, slot);
    s->lists.count[l] = (uint8_t)(n + 1);
    s->lists_dirty = 1;
    return n + 1;
}

/* Two arrangements of a book changed places: every setlist keeps pointing at the same songs. */
static void sl_swapped(struct as_state *s, unsigned book, unsigned a, unsigned b)
{
    for (unsigned l = 0; l < LISTS; ++l) for (unsigned i = 0; i < s->lists.count[l]; ++i) {
        struct sl_entry *e = &s->lists.e[l][i];
        if (e->book != book) continue;
        if (e->slot == a) { e->slot = (uint8_t)b; s->lists_dirty = 1; }
        else if (e->slot == b) { e->slot = (uint8_t)a; s->lists_dirty = 1; }
    }
}

/* Song i of setlist l one place up (dir -1) or down: its new place. */
static unsigned sl_move(struct as_state *s, unsigned l, unsigned i, int dir)
{
    int j = (int)i + dir;
    if (j < 0 || j >= (int)s->lists.count[l]) return i;
    struct sl_entry t;
    entry_copy(&t, &s->lists.e[l][i]);
    entry_copy(&s->lists.e[l][i], &s->lists.e[l][j]);
    entry_copy(&s->lists.e[l][j], &t);
    s->lists_dirty = 1;
    return (unsigned)j;
}

/* Song i out of setlist l; the arrangement itself stays in its book. */
static void sl_remove(struct as_state *s, unsigned l, unsigned i)
{
    unsigned n = s->lists.count[l];
    if (i >= n) return;
    for (unsigned k = i; k + 1 < n; ++k) entry_copy(&s->lists.e[l][k], &s->lists.e[l][k + 1]);
    s->lists.count[l] = (uint8_t)(n - 1);
    s->lists_dirty = 1;
}

/* The book in the slots: its songs' names as they are now (a rename shows in the setlists). */
static void sl_names(struct as_state *s)
{
    char name[NAME_LEN];
    for (unsigned l = 0; l < LISTS; ++l) for (unsigned i = 0; i < s->lists.count[l]; ++i) {
        struct sl_entry *e = &s->lists.e[l][i];
        if (e->book != s->book) continue;
        slot_name(name, e->slot);
        for (unsigned k = 0; k < NAME_LEN; ++k) if (e->name[k] != name[k]) { e->name[k] = name[k]; s->lists_dirty = 1; }
    }
}

/* arrlists.work, in the card task: written when changed; read on a load (empty when missing or not ours). */
static void write_lists(struct as_state *s)
{
    if (!s->lists_dirty) return;
    s->lists.magic[0] = 'A'; s->lists.magic[1] = 'S'; s->lists.magic[2] = 'L'; s->lists.magic[3] = '2';
    lists_path(s->path, "work");
    if (put_data(s, &s->lists, sizeof s->lists)) s->lists_dirty = 0;
}
static void read_lists(struct as_state *s)
{
    lists_path(s->path, "work");
    int ok = get_data(s, &s->lists, sizeof s->lists)
        && s->lists.magic[0] == 'A' && s->lists.magic[1] == 'S' && s->lists.magic[2] == 'L' && s->lists.magic[3] == '2';
    if (!ok) {                                            /* the first format, without names */
        ok = get_data(s, &s->lists, LISTS_V1_BYTES)
            && s->lists.magic[0] == 'A' && s->lists.magic[1] == 'S' && s->lists.magic[2] == 'S' && s->lists.magic[3] == 'L';
        for (unsigned l = 0; l < LISTS; ++l) for (unsigned i = 0; i < 16u; ++i) s->lists.name[l][i] = 0;
    }
    ok = ok && s->lists.open < LISTS;
    for (unsigned l = 0; ok && l < LISTS; ++l) s->lists.name[l][15] = 0;
    for (unsigned l = 0; ok && l < LISTS; ++l) {
        if (s->lists.count[l] > LIST_LEN) ok = 0;
        for (unsigned i = 0; ok && i < s->lists.count[l]; ++i)
            if (s->lists.e[l][i].book >= BOOKS || s->lists.e[l][i].slot >= SLOTS) ok = 0;
    }
    if (!ok) sl_clear(s);
    s->lists_dirty = 0;
    s->lists_unread = 0;
}

/* ---------------------------------------------------------------- a setlist playing */
#define SL_A 6u
#define SL_B 7u
static uint32_t work_at(unsigned slot) { return WORKING + slot * BLOCK; }
static void block_from_slot(uint8_t *to, unsigned slot) { for (uint32_t i = 0; i < BLOCK; ++i) to[i] = rd8(work_at(slot) + i); }
static void block_to_slot(unsigned slot, const uint8_t *from) { for (uint32_t i = 0; i < BLOCK; ++i) wr8(work_at(slot) + i, from[i]); }


/* The chain row's text: stock's two arranger-row drawings take their format from here instead of "CHAIN ARR. %d"
 * (manifest.py's SymbolRefs on their pea operands, 0x40048eb8 and 0x400496e6): the name of what is in the slot the chain
 * names (">SONG BRAVO"). The module's own buffer: writing into a declared stock change made the loader's check of its
 * bytes fail, and every later install was refused as busy until a power cycle (the developer's MKI, 23 November 2026).
 * It is a format string, so a % in a name becomes a space; stock's own text and its shared "%d" stay untouched. */
char as_chain_text[16];

/* The Arranger menu's title ("ARR %d:%s" of stock's index + 1 and the name, 0x40057f1c): manifest.py's SymbolRefs point
 * its index byte and its format here. Stock's own outside a setlist; in one, "SONG 2:NAME", the song's place. */
uint8_t as_title_index;
char as_title_fmt[12];
static void title_follow(const struct as_state *s)
{
    const char *f = s->sl_active ? "SONG %d:%s" : "ARR %d:%s";
    unsigned i = 0;
    for (; f[i]; ++i) as_title_fmt[i] = f[i];
    as_title_fmt[i] = 0;
    as_title_index = s->sl_active ? s->sl_cur : (uint8_t)rd8(0x80000005u);
}
static void next_text(unsigned slot)
{
    unsigned k = 0;
    as_chain_text[k++] = '>';
    for (unsigned i = 0; i < NAME_LEN && k < sizeof as_chain_text - 1u; ++i) {
        char c = (char)rd8(work_at(slot) + i);
        if (!c) break;
        as_chain_text[k++] = c == '%' ? ' ' : c;
    }
    while (k < sizeof as_chain_text) as_chain_text[k++] = 0;
}

/* Stock's CHAIN to a slot, its row naming the song there, and the rows drawn again (the arranger draws them as it
 * takes a chain, before the module sets the next one: the developer's MKI showed END OF ARR, 23 November 2026). */
static void set_chain(unsigned slot)
{
    wr32(CHAIN, slot);
    next_text(slot);
    post_event();
}

/* Song k into one of the setlist's two slots: the arranger plays it from there. */
static void put_song(struct as_state *s, unsigned which, unsigned k)
{
    block_to_slot(which ? SL_B : SL_A, s->songs[k]);
    s->song_at[which] = (uint8_t)k;
    wr8(CHANGED, (uint8_t)(rd8(CHANGED) & ~(1u << (which ? SL_B : SL_A))));
}

/* A song leaves its slot: what was edited in it while it played is kept, to be written to its own file at the end. */
static void take_back(struct as_state *s, unsigned which)
{
    unsigned k = s->song_at[which], slot = which ? SL_B : SL_A;
    if (k >= s->sl_n) return;
    for (uint32_t i = 0; i < BLOCK; ++i) {
        uint8_t v = rd8(work_at(slot) + i);
        if (v != s->songs[k][i]) { s->songs[k][i] = v; s->sl_edited |= (uint16_t)(1u << k); }
    }
}

/* The file a setlist song lives in: arr<NN>.work, or book 1's backup while another book is in the slots. */
static void song_path(struct as_state *s, const struct sl_entry *e, const char *ext)
{
    arr_path(s->path, arr_number(e->book, e->slot), ext);
}

/* Card task, after the book in the slots was written: the open setlist's songs into memory, the first two into
 * SL_A and SL_B. Songs of that book come from its slots; the others from their files, through SL_B. 1 when done. */
static int sl_load(struct as_state *s)
{
    unsigned l = s->lists.open, n = s->lists.count[l], start = (unsigned)s->sl_req;
    s->sl_req = -1;
    if (!n || start >= n) return 1;
    int ok = 1;
    for (unsigned k = 0; k < n; ++k) {
        entry_copy(&s->sl_e[k], &s->lists.e[l][k]);
        if (s->sl_e[k].book == s->book) block_from_slot(s->songs[k], s->sl_e[k].slot);
    }
    for (unsigned k = 0; k < n && ok; ++k) {
        const struct sl_entry *e = &s->sl_e[k];
        if (e->book == s->book) continue;
        song_path(s, e, e->book == 0 && s->backup ? "bak" : "work");
        int r = arr_read(s, SL_B);
        if (r == 0) clear_arr(SL_B);                          /* never saved: an empty arrangement */
        if (r < 0) ok = 0;
        else block_from_slot(s->songs[k], SL_B);
    }
    if (!ok) {                                                /* the book as it was; the setlist does not start */
        read_book(s, s->book, "work");
        wr8(CHANGED, 0);
        return 0;
    }
    s->sl_n = (uint8_t)n;
    s->sl_list = (uint8_t)l;
    s->sl_cur = (uint8_t)start;
    s->sl_cur_slot = SL_A;
    s->sl_edited = 0;
    s->song_at[1] = 0xff;
    put_song(s, 0, start);
    if (start + 1 < n) put_song(s, 1, start + 1);
    s->sl_active = 1;
    s->sl_go = 1;
    return 1;
}

/* Card task, ending: songs edited while they played to their own files (read first, so their ARR SAVE copy stays),
 * then the book back into its slots. 1 when every write and the reread succeeded. */
static int sl_finish(struct as_state *s)
{
    take_back(s, 0);
    take_back(s, 1);
    int ok = 1;
    for (unsigned k = 0; k < s->sl_n; ++k) if (s->sl_edited >> k & 1) {
        const struct sl_entry *e = &s->sl_e[k];
        int backup = e->book == 0 && s->book != 0 && s->backup;
        song_path(s, e, backup ? "bak" : "work");
        if (arr_read(s, SL_B) < 0) { ok = 0; continue; }     /* missing: written new */
        block_to_slot(SL_B, s->songs[k]);
        ok &= arr_write(s, SL_B);
        if (backup) { song_path(s, e, "work"); ok &= arr_write(s, SL_B); }
    }
    s->sl_active = 0;
    s->sl_end = 0;
    s->sl_go = 0;
    ok &= read_book(s, s->book, "work");
    wr8(CHANGED, 0);
    wr32(CHAIN, 0xffffffffu);
    s->redraw = 1;
    return ok;
}

/* ---------------------------------------------------------------- the card task */
/* Stock's arrangement writer, stepped into at its entry (the site's jmp reaches as_writer_entry below). */
int as_writer(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    struct as_state *s = ready();
    if (s->lists_unread) read_lists(s);                   /* installed into an open project: its setlists first */
    if (s->lists_req) { s->lists_req = 0; s->hush = 1; }
    if (s->reread) {                                      /* installed while a setlist played: the book back, unwritten */
        s->hush = 1;
        int ok = read_book(s, s->book, "work");
        wr8(CHANGED, 0);
        s->reread = 0;
        s->reread_posted = 0;
        s->damaged = (uint8_t)!ok;
        if (!ok) s->failed = 1;
        s->redraw = 1;
        write_lists(s);
        remember(s);
        return 0;
    }
    if (s->sl_active) {                                   /* slots 7 and 8 hold setlist songs: no book file written */
        if (s->sl_end) {
            s->hush = 1;
            int ok = sl_finish(s);
            if (ok && s->sl_restart >= 0) { s->sl_req = s->sl_restart; ok = sl_load(s); }
            s->sl_restart = -1;
            s->sl_req = -1;
            if (!ok) s->failed = 1;
        }
        write_lists(s);
        remember(s);
        return 0;
    }
    if (s->sl_req >= 0) s->hush = 1;                      /* our job: its NOW SAFE TO EJECT! is closed after */
    int rc = 0;
    if (s->pending >= 0) s->hush = 1;                     /* our job: its NOW SAFE TO EJECT! is closed after */
    if (s->book == 0 && !s->damaged) rc = stock_writer(a1, a2, a3, a4);     /* book 1: arr01-08 as always */
    else if (s->book == 0) {
        /* Book 1 read only in part: stock's writer would write every slot its mask names (a sync: all 8), the empty
         * ones too, over arr01-08 (it chooses by the mask or a missing file, never by this byte). Only the edited ones. */
        if (rd8(CHANGED)) { if (write_changed(s, 0)) wr8(CHANGED, 0); else rc = -1; }
    }
    else if ((a2 & 0xffu) || rd8(CHANGED)) {
        /* Another book: its 8 files together, whenever stock asks for any (as stock's writer does with its mask: the
         * SYNC job, and so every switch, asks for all 8). Not only when something is marked changed: the list's
         * FUNC + PLAY clears an arrangement without marking it (0x400610ee), and stock saves it all the same. */
        if (s->damaged ? write_changed(s, s->book) : write_book(s, s->book)) wr8(CHANGED, 0);
        else rc = -1;
    }
    int switched = 0;
    if (s->pending >= 0) {
        unsigned to = (unsigned)s->pending;
        switched = 1;
        if (rc >= 0) {
            s->last[s->book] = (uint8_t)(rd8(INDEX) & 7u);     /* the book left: which arrangement was chosen */
            /* Leaving book 1: its backup, which counts only when all eight files were written: a partial one is never
             * read back (book 1 then comes from arr01-08 themselves). */
            if (s->book == 0) s->backup = !s->damaged && write_book(s, 0);
            int ok;
            if (to == 0 && s->backup) {
                unsigned mark = restore_book1(s, &ok);       /* stock's next write puts these back into arr01-08 */
                wr8(CHANGED, mark);
            } else {
                ok = read_book(s, to, "work");                /* never marked changed: nothing is written back */
                wr8(CHANGED, 0);
            }
            s->book = (uint8_t)to;                            /* the slots hold the new book now, even after a fault */
            s->redraw = 1;
            s->hush = (uint8_t)ok;
            s->damaged = (uint8_t)!ok;
            if (!ok) s->failed = 1;
        } else s->failed = 1;
        s->pending = -1;
    }
    ev(switched ? 9u : 0xau);
    if (!switched) s->last[s->book] = (uint8_t)(rd8(INDEX) & 7u);   /* the choice in the book in use, as saved */
    sl_names(s);                                          /* the setlists: names as the book in the slots has them */
    write_lists(s);
    write_place(s);                                       /* where the project is: a load comes back here */
    if (s->sl_req >= 0 && rc >= 0 && !s->damaged && !sl_load(s)) s->failed = 1;   /* YES on the SETLSTS page */
    s->sl_req = -1;
    remember(s);
    return rc;
}

#ifndef AS_HOST
/* The site's jmp lands here with the caller's frame as at stock's entry: forward its four arguments to as_writer and
 * return to the caller. as_stock_writer replays the two instructions the site displaced and runs stock's writer. */
__asm__(
    "\t.text\n"
    "\t.globl as_writer_entry\n"
    "as_writer_entry:\n"
    "\tmove.l 16(%sp),-(%sp)\n"
    "\tmove.l 16(%sp),-(%sp)\n"
    "\tmove.l 16(%sp),-(%sp)\n"
    "\tmove.l 16(%sp),-(%sp)\n"
    "\tjsr as_writer\n"
    "\tlea 16(%sp),%sp\n"
    "\trts\n"
    "\t.globl as_stock_writer\n"
    "as_stock_writer:\n"
    "\tlink.w %fp,#-324\n"
    "\tmovem.l %d2-%d7/%a2-%a5,(%sp)\n"
    "\tjmp 0x400915b4\n");
#endif

/* A project load or a new project: stock reads arr01-08 into the slots, so they hold book 1. */
void as_project_load(void)
{
    struct as_state *s = ready();
    ev(1);
    s->since_hook = 0;
    s->pending = -1;
    /* Not yet book 1: this is stock's first load job, which only checks the files; its second job, after the
     * new-project step (ev_project_new), puts arr01-08 in the slots (0x4008fd28). Until then the slots still hold the
     * book in use, so the module keeps it: a write in between must not save them as arr01-08. */
    s->loading = 1;
    s->reloaded_slots = 0;
    /* Where was the project left? Read here, in stock's load job on its card task, from the project being loaded (on
     * RELOAD, already the saved copy): ev_banks_loaded never came on the developer's MKI. Arranger mode goes into the
     * byte stock applies when the load ends (LOAD clears it as it starts), so stock itself turns the mode back on.
     * The book is switched to once stock has finished. */
    for (unsigned b = 0; b < BOOKS; ++b) s->last[b] = 0;
    s->page = 0;                                          /* another project's setlists: the page closes */
    s->sl_active = 0;                                     /* stock replaces the slots: a setlist playing ends */
    s->reread = 0;
    s->sl_req = -1; s->sl_go = 0; s->sl_end = 0;
    int b = read_place(s);
    read_lists(s);
    s->restore = (int8_t)(b > 0 ? b : -1);
    s->restore_ticks = 0;
    if (s->mode_on) wr8(ARR_MODE_WANTED, 1);
    s->ready = 1;
}

/* A new project: empty, on book 1; nothing of the last one is read. */
void as_project_new(void)
{
    struct as_state *s = ready();
    ev(0xe);
    /* Stock's job now resets the project and reads arr01-08 into the slots (or starts an empty one): book 1 from
     * here. The module's own return job can only queue behind this one. */
    s->book = 0;
    s->pending = -1;
    s->backup = 0;
    s->damaged = 0;
    s->ready = 1;
    int loading = s->loading && s->since_hook < 3600;     /* LOAD and RELOAD run this step too (the developer's MKI) */
    s->loading = 0;
    s->since_hook = 0;
    remember(s);
    if (loading) { s->reloaded_slots = 1; return; }       /* a load's pending return stays, and may go now */
    s->reloaded_slots = 0;
    s->restore = -1;
    s->mode_on = 0;
    for (unsigned b = 0; b < BOOKS; ++b) s->last[b] = 0;
    sl_clear(s);                                          /* a new project has no setlists */
    s->page = 0;
    s->sl_active = 0; s->sl_req = -1; s->sl_go = 0; s->sl_end = 0; s->reread = 0;
}

/* Stock has read the banks; it reads arr01-08 next, so the slots will hold book 1. Where the project was left is read
 * here, and the UI task goes back to it once stock is done (a switch as LEFT/RIGHT makes it). */
unsigned as_project_loaded(void)
{
    struct as_state *s = ready();
    ev(2);
    s->since_hook = 0;                                    /* the choices were read as the load began; the slots are */
    ++s->loads;                                           /* replaced only after the new-project step */
    remember(s);
    return 0;
}

/* SAVE PROJECT copied arr01-08 .work to .strd: arr09-arr128 too, those there are. */
unsigned as_project_stored(void)
{
    struct as_state *s = ready();
    ev(4);
    for (unsigned n = SLOTS + 1; n <= BOOKS * SLOTS; ++n) {
        arr_path(s->path, n, "work");
        arr_path(s->path2, n, "strd");
        copy_file(s);
    }
    place_path(s->path, "work");
    place_path(s->path2, "strd");
    copy_file(s);
    lists_path(s->path, "work");
    lists_path(s->path2, "strd");
    copy_file(s);
    return 0;
}

/* RELOAD PROJECT copied arr01-08 .strd back to .work: arr09-arr128 too; stock's loads then put book 1 in the slots. */
unsigned as_project_reloaded(void)
{
    struct as_state *s = ready();
    ev(3);
    s->since_hook = 0;
    for (unsigned n = SLOTS + 1; n <= BOOKS * SLOTS; ++n) {
        arr_path(s->path, n, "strd");
        arr_path(s->path2, n, "work");
        copy_file(s);
    }
    place_path(s->path, "strd");
    place_path(s->path2, "work");
    copy_file(s);
    lists_path(s->path, "strd");
    lists_path(s->path2, "work");
    int lists_saved = copy_file(s);
    s->loading = 1;                                       /* the slots change only at the new-project step */
    s->reloaded_slots = 0;
    /* Where it was saved: RELOAD's own hook, after the copy. On the developer's MKI a RELOAD was followed by stock's
     * new-project step (which keeps this) and sometimes by no ev_banks_load at all. */
    int b = read_place(s);
    if (lists_saved != 0) read_lists(s);                 /* a copy that failed: what the card holds */
    else { sl_clear(s); s->lists_dirty = 1; }             /* saved before it had setlists: none, as saved */
    s->restore = (int8_t)(b > 0 ? b : -1);
    s->restore_ticks = 0;
    if (s->mode_on) wr8(ARR_MODE_WANTED, 1);
    ++s->reloads;
    return 0;
}

/* Installed (or the set changed): which book do the slots hold? This copy's own memory, when the record still carries
 * the serial it last wrote (nothing else ran since); otherwise the record's, when the slots still match it. A build
 * whose memory survived another build's trial would otherwise trust a book the slots no longer hold, and its next
 * switch would save one book's arrangements as another's (the developer's MKI, 18 November 2026). */
unsigned as_modules_ready(void)
{
    struct as_state *s = ready();
    if (s->ready && rd32(RECORD) == REC_MAGIC && rd32(RECORD + 12) == s->seq) return 0;
    s->ready = 1;
    if (rd32(RECORD) == REC_MAGIC && rd8(RECORD + 4) < BOOKS && rd32(RECORD + 8) == slots_sum()) {
        s->book = (uint8_t)rd8(RECORD + 4);
        s->backup = (uint8_t)rd8(RECORD + 5);
        /* The book in the slots was read only in part: still write back only what is edited, after a reinstall or
         * after another build's trial (the owner's review, #1737). */
        s->damaged = (uint8_t)(rd8(RECORD + 6) == 1);
        /* A setlist was playing: slots 7 and 8 are its songs. Nothing of them is written; the book is read again. */
        if (rd8(RECORD + 7) == 1) s->reread = 1;
    } else { s->book = 0; ev(0xf); }
    remember(s);
    return 0;
}

/* ---------------------------------------------------------------- the UI task */
static int stopped(void)
{
    if (rd32(PLAYING)) return 0;
    for (unsigned i = 0; i < 16; ++i) if (rd8(0x80006500u + i) || rd8(0x80004f1eu + i * 84u)) return 0;
    return 1;
}

static char *put_text(char *t, const char *x) { while (*x) *t++ = *x++; return t; }
static char *put_num(char *t, unsigned n) { if (n >= 10) *t++ = (char)('0' + n / 10); *t++ = (char)('0' + n % 10); return t; }

/* The setlists in memory? After an install into an open project they are read by a card job first: post one (once)
 * and say so; 0 until it has run. */
static int lists_ready(struct as_state *s)
{
    if (!s->lists_unread) return 1;
    if (!stopped()) { message("STOP TO READ SETLISTS"); return 0; }   /* stock's card job would stop playback */
    if (!s->lists_req && s->pending < 0) {
        int rc = post_write();
        if (rc == -7) { s->lists_unread = 0; return 1; }      /* no project: nothing to read */
        if (rc >= 0) s->lists_req = 1;
    }
    message("READING SETLISTS");
    return 0;
}

/* CUE + YES in ARR > CHANGE: the arrangement under the cursor to the end of the open setlist, and say so. */
static void add_to_list(struct as_state *s)
{
    unsigned slot = rd32(LIST_CHOICE) & 7u, l = s->lists.open;
    unsigned at = sl_add(s, s->book, slot);
    char *t = s->msg;
    if (!at) { t = put_text(t, "LIST "); t = put_num(t, l + 1); t = put_text(t, " IS FULL"); }
    else {
        *t++ = 'B'; t = put_num(t, s->book + 1u); *t++ = ' '; t = put_num(t, slot + 1u);
        t = put_text(t, " ADDED TO LIST "); t = put_num(t, l + 1);
    }
    *t = 0;
    message(s->msg);
}

/* Two arrangements of the book in the slots change places: working and saved copies and their flags. */
static void swap_slots(unsigned a, unsigned b)
{
    for (unsigned k = 0; k < 2; ++k) {
        uint32_t pa = WORKING + (k * SLOTS + a) * BLOCK, pb = WORKING + (k * SLOTS + b) * BLOCK;
        for (uint32_t i = 0; i < BLOCK; ++i) { uint8_t x = rd8(pa + i); wr8(pa + i, rd8(pb + i)); wr8(pb + i, x); }
    }
    uint32_t f = WORKING + 2u * SLOTS * BLOCK;
    uint8_t x = rd8(f + a); wr8(f + a, rd8(f + b)); wr8(f + b, x);
}

/* CUE + LEFT/RIGHT in ARR > CHANGE: the arrangement under the cursor one place up or down in its book. Both are
 * marked changed, so the next sync writes them to their files; the setlists follow; the choice and cursor go with it. */
static void move_arr(struct as_state *s, int dir)
{
    if (!stopped()) { message("STOP TO MOVE"); return; }
    int a = (int)(rd32(LIST_CHOICE) & 7u), b = a + dir;
    if (b < 0 || b >= (int)SLOTS) return;
    swap_slots((unsigned)a, (unsigned)b);
    wr8(CHANGED, (uint8_t)(rd8(CHANGED) | 1u << a | 1u << b));
    sl_swapped(s, s->book, (unsigned)a, (unsigned)b);
    unsigned idx = rd8(INDEX) & 7u;
    if (idx == (unsigned)a || idx == (unsigned)b) select_arr(idx == (unsigned)a ? (unsigned)b : (unsigned)a);
    uint8_t *last = &s->last[s->book];
    if (*last == a) *last = (uint8_t)b; else if (*last == b) *last = (uint8_t)a;
    wr32(LIST_CHOICE, (uint32_t)b);
    wr32(LIST_CURSOR, (uint32_t)b - rd32(LIST_TOP));
    draw_change();
}

#define PAGE_ROWS 7u
#define NAME_EDIT 0x4007e664u        /* stock's name editor (len, title, buffer, 1, done(ok)): RENAME's (0x40049c88) */

/* A setlist's title: its name, or "SETLIST n" without one. */
static char *list_title(const struct as_state *s, char *t, unsigned l)
{
    const char *n = s->lists.name[l];
    unsigned k = 0;
    while (k < 15u && n[k]) ++k;
    while (k && n[k - 1] == ' ') --k;                       /* the editor pads with spaces */
    if (!k) { t = put_text(t, "SETLIST "); return put_num(t, l + 1u); }
    for (unsigned i = 0; i < k; ++i) *t++ = n[i];
    return t;
}

/* Stock's name editor is done with a setlist's name: kept with the next sync. */
void as_list_named(int ok)
{
    struct as_state *s = ready();
    if (ok) s->lists_dirty = 1;
}
#ifdef AS_HOST
void host_name_edit(char *buffer);
static void name_edit(char *buffer) { host_name_edit(buffer); }
#else
static void name_edit(char *buffer)
{
    static const char title[] = "SETLIST NAME";
    ((void (*)(int, const char *, char *, int, void (*)(int)))NAME_EDIT)(15, title, buffer, 1, as_list_named);
}
#endif

/* The Arranger menu's fourth item (CHAIN in stock): its YES opens the SETLSTS page (manifest.py's SymbolRef). */
void as_setlists_open(void)
{
    struct as_state *s = ready();
    s->page = 1;
    s->page_cur = 0;
    s->page_top = 0;
    if (s->lists_unread) { lists_ready(s); return; }     /* drawn as READING until the card job has read them */
    /* The cursor on the song playing, or else on the arrangement chosen now, when the setlist holds it. */
    unsigned l = s->lists.open, n = s->lists.count[l];
    if (s->sl_active && s->sl_list == l && s->sl_cur < n) s->page_cur = s->sl_cur;
    else for (unsigned i = 0; i < n; ++i)
        if (s->lists.e[l][i].book == s->book && s->lists.e[l][i].slot == (rd8(INDEX) & 7u)) { s->page_cur = (uint8_t)i; break; }
    if (s->page_cur >= PAGE_ROWS) s->page_top = (uint8_t)(s->page_cur - PAGE_ROWS + 1u);
}


/* FUNC + REC on the page: the song under the cursor copied, or with CUE held the whole setlist shown. */
static void page_copy(struct as_state *s, unsigned l, unsigned n)
{
    char *t = s->msg;
    if (!n) return;
    if (s->cue_down) {
        for (unsigned i = 0; i < n; ++i) entry_copy(&s->clip[i], &s->lists.e[l][i]);
        s->clip_n = (uint8_t)n;
        t = put_text(t, "COPIED LIST "); t = put_num(t, l + 1u);
    } else {
        entry_copy(&s->clip[0], &s->lists.e[l][s->page_cur]);
        s->clip_n = 1;
        t = put_text(t, "COPIED SONG");
    }
    *t = 0;
    message(s->msg);
}

/* FUNC + STOP on the page: what was copied, after the cursor (at the top of an empty setlist), as far as it fits. */
static void page_paste(struct as_state *s, unsigned l, unsigned n)
{
    if (!s->clip_n) return;
    unsigned at = n ? s->page_cur + 1u : 0u, room = LIST_LEN - n, k = s->clip_n < room ? s->clip_n : room;
    for (unsigned i = n; i-- > at;) entry_copy(&s->lists.e[l][i + k], &s->lists.e[l][i]);   /* make room */
    for (unsigned i = 0; i < k; ++i) entry_copy(&s->lists.e[l][at + i], &s->clip[i]);
    s->lists.count[l] = (uint8_t)(n + k);
    if (k) { s->lists_dirty = 1; s->page_cur = (uint8_t)(at + k - 1u); }
    if (k < s->clip_n) {
        char *t = put_text(s->msg, "LIST "); t = put_num(t, l + 1u); t = put_text(t, " IS FULL"); *t = 0;
        message(s->msg);
    }
}

/* Keys while the SETLSTS page is open: all but the transport, FUNC and CUE are the page's. */
static int page_key(struct as_state *s, int code, int pressed)
{
    if (code == K_FUNC) return 0;
    if (code == K_STOP && !s->page_stop && !(pressed && s->func_down)) return 0;    /* STOP stays stock's */
    if (code == K_STOP && !pressed) { s->page_stop = 0; return 1; }
    if (code == K_PLAY && !s->page_play && !(pressed && s->func_down)) return 0;  /* PLAY stays stock's */
    if (!pressed) { if (code == K_PLAY) s->page_play = 0; return 1; }
    unsigned l = s->lists.open, n = s->lists.count[l];
    if (s->lists_unread && code != K_NO) return 1;            /* still reading them */
    switch (code) {
    case K_NO: s->page = 0; break;
    case K_UP: if (s->page_cur) --s->page_cur; break;
    case K_DOWN: if (s->page_cur + 1u < n) ++s->page_cur; break;
    case K_LEFT: case K_RIGHT: {
        int dir = code == K_RIGHT ? 1 : -1;
        if (s->cue_down) s->page_cur = (uint8_t)sl_move(s, l, s->page_cur, dir);
        else if ((int)l + dir >= 0 && (int)l + dir < (int)LISTS) {
            s->lists.open = (uint8_t)((int)l + dir);
            s->lists_dirty = 1;
            s->page_cur = 0;
            s->page_top = 0;
        }
        break;
    }
    case K_REC: if (s->func_down) page_copy(s, l, n); break;
    case K_STOP:                                               /* FUNC + STOP: paste */
        if (s->page_stop) break;
        s->page_stop = 1;
        page_paste(s, l, n);
        break;
    case K_PLAY:                                               /* FUNC + PLAY: the song out of the setlist */
        if (s->page_play) break;
        s->page_play = 1;
        sl_remove(s, l, s->page_cur);
        if (s->page_cur && s->page_cur >= s->lists.count[l]) --s->page_cur;
        break;
    case K_YES:
        if (s->func_down) { s->page = 0; name_edit(s->lists.name[l]); break; }   /* FUNC + YES: its name */
        if (!n) break;                                         /* YES: the setlist from this song */
        if (!stopped()) { message("STOP TO START"); break; }
        if (s->sl_active) { s->sl_end = 1; s->sl_restart = (int8_t)s->page_cur; post_write(); s->page = 0; break; }
        {
            s->sl_req = (int8_t)s->page_cur;
            int rc = post_write();
            if (rc < 0) { s->sl_req = -1; message(rc == -7 ? "NO PROJECT" : "CARD BUSY"); break; }
            s->page = 0;
        }
        break;
    default: break;
    }
    if (s->page_cur < s->page_top) s->page_top = s->page_cur;
    if (s->page_cur >= s->page_top + PAGE_ROWS) s->page_top = (uint8_t)(s->page_cur - PAGE_ROWS + 1u);
    return 1;
}

/* YES in the playing-setlist window: the song under the cursor comes next, at once (into the free slot, chained); the
 * setlist goes on in order after it. */
static void peek_choose(struct as_state *s)
{
    unsigned next = s->sl_cur_slot == SL_A ? 1u : 0u, k = s->peek_cur;
    s->peek_next = (uint8_t)k;
    if (!s->sl_active || s->sl_end || k >= s->sl_n || s->song_at[next] == k) return;
    put_song(s, next, k);
    set_chain(next ? SL_B : SL_A);
}

int as_key(int code, int pressed)
{
    struct as_state *s = ready();
    if (code == K_FUNC) s->func_down = (uint8_t)!!pressed;
    if (code == K_PATTERN && pressed && s->func_down) { s->mode_key = 1; s->mode_key_ticks = 0; }  /* stock's again */
    if (code == K_CUE) {
        s->cue_down = (uint8_t)!!pressed;
        if (pressed) s->cue_main = rd32(KEYS + K_YES * 0x18u) == MAIN_YES;   /* before stock's CUE layer routes YES */
        if (!pressed) s->peek = 0;                            /* released: the window closes */
        return 0;
    }
    if (s->peek) {                                            /* the playing setlist shown */
        if (code == K_UP || code == K_DOWN) {                 /* the cursor only */
            if (pressed) {
                if (code == K_UP && s->peek_cur) --s->peek_cur;
                if (code == K_DOWN && s->peek_cur + 1u < s->sl_n) ++s->peek_cur;
            }
            return 1;
        }
        if (code == K_YES) {                                  /* a further YES: the '>' to the cursor */
            if (pressed && !s->cue_yes) peek_choose(s);
            if (!pressed) s->cue_yes = 0;                     /* the opening YES's release changes nothing */
            return 1;
        }
    }
    /* CUE + YES on the main screen while a setlist plays (stock does nothing with it there). */
    if (code == K_YES && pressed && s->cue_down && s->sl_active && !s->sl_go && !s->sl_end && !s->page && s->cue_main) {
        unsigned next = s->sl_cur_slot == SL_A ? 1u : 0u;
        s->peek = 1;
        s->cue_yes = 1;                                       /* this YES opens; its release is ours too */
        s->peek_cur = s->sl_cur;
        s->peek_next = s->song_at[next] < s->sl_n ? s->song_at[next] : 0xff;
        return 1;
    }
    if (s->page) return page_key(s, code, pressed);
    if (code == K_YES && (s->cue_yes || (s->cue_down && rd32(CHANGE_WINDOW)))) {
        if (!pressed) { s->cue_yes = 0; return 1; }           /* its release is ours too */
        if (s->cue_yes || s->pending >= 0) return 1;          /* a repeat, or a switch under way */
        s->cue_yes = 1;
        if (lists_ready(s)) add_to_list(s);
        return 1;
    }
    if (code != K_LEFT && code != K_RIGHT) return 0;
    if (!rd32(CHANGE_WINDOW)) return 0;                      /* only in ARR > CHANGE */
    if (!pressed) return 1;                                   /* its release is ours too */
    if (s->pending >= 0) return 1;                            /* a switch is under way */
    if (s->cue_down) { move_arr(s, code == K_RIGHT ? 1 : -1); return 1; }
    if (!stopped()) { message("STOP TO CHANGE BOOK"); return 1; }
    int to = (int)s->book + (code == K_RIGHT ? 1 : -1);
    if (to < 0 || to >= (int)BOOKS) return 1;
    s->pending = (int8_t)to;
    s->failed = 0;
    int rc = post_write();                                    /* stock: -7 no project, else busy or a save pending */
    if (rc < 0) { s->pending = -1; message(rc == -7 ? "NO PROJECT" : "CARD BUSY"); }
    return 1;
}

struct as_api { volatile uint32_t value; };

/* A failed switch said, the fingerprint kept fresh, and the diagnostic word: bit 0-3 the book (0 = book 1), 4-7 the
 * book a switch is loading (+1; 0 none), 9 failed, 10 backup of book 1 kept. */
void as_tick(void *api)
{
    struct as_state *s = ready();
    if (api) ((struct as_api *)api)->value = (uint32_t)s->book  /* the book, then the last seven events, newest lowest */
        | (evlog & 0x0fffffffu) << 4;
    /* Stock draws the list's names once, when it opens: after a switch, draw them again from the new book. */
    if (s->redraw && s->pending < 0) {
        /* The book's own last choice, as YES in the list makes it. Stock refuses it while a change of its own is
         * pending (0x800066a0; right after a RELOAD in arranger mode, on the developer's MKI): again next frame, for up
         * to two seconds, so the main screen does not keep the old book's name. */
        if (select_arr(s->last[s->book]) < 0 && ++s->select_tries < 120) { /* refused: again next frame */ }
        else {
            s->select_tries = 0;
            s->redraw = 0;
            /* After a load, stock's end of the load may already have put arranger mode on over book 1's slots (the
             * developer's MKI, RELOAD): the arranger and the main screen still show book 1. Apply the mode again as
             * stock does then, now over this book. Only for a return after a load, never for LEFT/RIGHT. */
            if (s->reapply && rd32(ARR_MODE)) arr_apply(1);
            s->reapply = 0;
            if (rd32(CHANGE_WINDOW)) {
                if (rd32(LIST_TOP) == 0) wr32(LIST_CURSOR, s->last[s->book]);  /* the cursor on it, as stock opens the list */
                draw_change();
            }
        }
    }
    /* After a project load: back to the book it was left on, once stock has finished loading (its job queue frees). */
    if (s->since_hook < 4000) ++s->since_hook;
    if (s->restore >= 0 && s->pending < 0) {
        if (!s->reloaded_slots || s->since_hook < 30) { }  /* only once stock has put arr01-08 in the slots */
        else if (stopped()) {
            s->pending = s->restore;
            s->reapply = 1;
            int rc = post_write();
            s->restore_rc = (int8_t)rc;
            ev(rc >= 0 ? 7u : 8u);
            if (rc >= 0) { s->restore = -1; s->reloaded_slots = 0; }
            else {
                s->pending = -1;
                s->reapply = 0;                          /* stock still busy (a reload reads its samples): again */
                if (rc == -7 || ++s->restore_ticks > 18000) s->restore = -1;  /* no project, or busy for 5 minutes */
            }
        }
    }
    /* The borrowed SYNC job ends with stock's NOW SAFE TO EJECT!: a switch of ours closes it as it opens (about a
     * second; any other message stays). */
    if (s->hush && s->pending < 0) {
        if (rd32(MESSAGE_UP)) { close_message(); s->hush = 0; }
        else if (++s->hush > 90) s->hush = 0;
    }
    /* Arranger mode as the musician leaves it. Stock's LOAD switches it off and saves that before it loads; only a
     * FUNC + PATTERN of the musician's own records it as off. */
    { uint8_t now = rd32(ARR_MODE) ? 1 : 0; if (now != s->mode_seen) { s->mode_seen = now; s->quiet = 0; ev(now ? 0xcu : 0xdu); } }
    if (rd32(ARR_MODE)) s->mode_pref = 1;
    else if (s->mode_key) { s->mode_pref = 0; s->mode_key = 0; }
    if (s->mode_key && ++s->mode_key_ticks > 60) s->mode_key = 0;
    /* After a load: stock's LOAD starts in pattern mode; a project left in arranger mode gets it back, once its book
     * and arrangement are chosen (RELOAD keeps the mode, and then this does nothing). */
    if (s->mode_on) {
        if (s->pending >= 0 || s->restore >= 0 || s->redraw || s->hush) s->quiet = 0;
        else if (++s->quiet >= 120) {                     /* two seconds of quiet: stock is done with the load */
            s->mode_on = 0;
            s->quiet = 0;
            if (s->mode_pref && !rd32(ARR_MODE)) arr_mode_on();
        }
    }
    /* A setlist: its first song chosen and the next chained, once loaded; then each time stock has taken the chain
     * (it moved to the other slot past the last row, and CHAIN went back to -1), the finished song's edits kept and
     * the song after next put into the freed slot and chained. Opening ARR > CHANGE, stopped, ends it. */
    if (s->sl_go && s->pending < 0 && select_arr(SL_A) >= 0) {
        s->sl_go = 0;
        if (s->song_at[1] != 0xff) set_chain(SL_B);
        if (!rd32(ARR_MODE)) arr_mode_on();
    }
    if (s->sl_active && !s->sl_go && !s->sl_end) {
        unsigned next = s->sl_cur_slot == SL_A ? 1u : 0u, next_slot = next ? SL_B : SL_A;
        /* Stock moved to the next song (its index is the next slot): the finished one's edits kept, the song after
         * it (or the end) into the freed slot. Decided from the index alone, before the chain is set again below. */
        if ((rd8(INDEX) & 7u) == next_slot && s->song_at[next] < s->sl_n) {
            unsigned done = next ^ 1u;
            take_back(s, done);
            s->sl_cur = s->song_at[next];                 /* the next one, or the one chosen with CUE + YES */
            s->sl_cur_slot = (uint8_t)next_slot;
            unsigned k = s->sl_cur + 1u;
            if (k < s->sl_n) put_song(s, done, k); else s->song_at[done] = 0xff;
            next = done;
            next_slot = done ? SL_B : SL_A;
            /* The window open over it follows: the '>' on the new next song. */
            if (s->peek) s->peek_next = s->song_at[next] < s->sl_n ? s->song_at[next] : 0xff;
        }
        /* The chain to the next slot, every frame: song 2 played with its chain gone on the developer's MKI (23
         * November 2026), and a chain cleared while a song plays would loop it instead of going on. */
        /* The last song has no chain: it ends as stock ends any arrangement, on its END OF ARR row. */
        if (s->song_at[next] == 0xff) { if (rd32(CHAIN) != 0xffffffffu) { wr32(CHAIN, 0xffffffffu); post_event(); } }
        else if (rd32(CHAIN) != next_slot) set_chain(next_slot);
        if (rd32(CHANGE_WINDOW) && stopped() && s->pending < 0) {
            s->sl_end = 1;
            if (post_write() < 0) s->sl_end = 0;            /* busy: again next frame */
        }
    }
    if (s->reread && s->pending < 0 && !s->reread_posted && stopped() && post_write() >= 0) s->reread_posted = 1;
    title_follow(s);
    if (s->failed) { s->failed = 0; message("BOOK FILE ERROR"); }
    if (s->book && s->pending < 0 && ++s->ticks >= 120) { s->ticks = 0; remember(s); }
}

/* ---------------------------------------------------------------- the screen */
/* The SETLSTS item's icon (the module's own, drawn as stock draws a menu icon, 0x400128a8): a sheet of three list
 * lines; selected, the second line highlighted. Left normal, right selected:
 * ...........................   ...........................
 * ....###################....   ....###################....
 * ....#.................#....   ....#.................#....
 * ....#.##..#########...#....   ....#.##..#########...#....
 * ....#.................#....   ....#.................#....
 * ....#.................#....   ....###################....
 * ....#.##..#########...#....   ....##..##.........####....
 * ....#.................#....   ....###################....
 * ....#.................#....   ....#.................#....
 * ....#.##..#########...#....   ....#.##..#########...#....
 * ....#.................#....   ....#.................#....
 * ....###################....   ....###################....
 * ...........................   ...........................
 * Each picture is 27 columns of one word, the top row in bit 31; its mask covers the 13 rows. */
static const uint32_t ICON_NORMAL[27] = {
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x7ff00000u, 0x40100000u,
    0x52500000u, 0x52500000u, 0x40100000u, 0x40100000u, 0x52500000u, 0x52500000u,
    0x52500000u, 0x52500000u, 0x52500000u, 0x52500000u, 0x52500000u, 0x52500000u,
    0x52500000u, 0x40100000u, 0x40100000u, 0x40100000u, 0x7ff00000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u,
};
static const uint32_t ICON_SELECTED[27] = {
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x7ff00000u, 0x47100000u,
    0x55500000u, 0x55500000u, 0x47100000u, 0x47100000u, 0x55500000u, 0x55500000u,
    0x55500000u, 0x55500000u, 0x55500000u, 0x55500000u, 0x55500000u, 0x55500000u,
    0x55500000u, 0x47100000u, 0x47100000u, 0x47100000u, 0x7ff00000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u,
};
static const uint32_t ICON_MASK[27] = {
    0xfff80000u, 0xfff80000u, 0xfff80000u, 0xfff80000u, 0xfff80000u, 0xfff80000u, 0xfff80000u, 0xfff80000u, 0xfff80000u,
    0xfff80000u, 0xfff80000u, 0xfff80000u, 0xfff80000u, 0xfff80000u, 0xfff80000u, 0xfff80000u, 0xfff80000u, 0xfff80000u,
    0xfff80000u, 0xfff80000u, 0xfff80000u, 0xfff80000u, 0xfff80000u, 0xfff80000u, 0xfff80000u, 0xfff80000u, 0xfff80000u,
};
/* As stock's entries hold it (0x400be5c2): width, height, 1, picture, mask; then the same for the selected one. The
 * menu entry's icon pointer is this (manifest.py's SymbolRef). */
struct icon_pic { uint32_t width, height, depth; const uint32_t *image, *mask; };   /* 20 bytes on the ColdFire */
const struct icon_pic as_setlists_icon[2] = {
    {27, 13, 1, ICON_NORMAL, ICON_MASK},
    {27, 13, 1, ICON_SELECTED, ICON_MASK},
};

/* 3x5 glyphs, row-major, 15 bits each (top row in bits 14-12): the module's own. 0-9, then B, O, K, L, I, S, T. */
static const uint16_t GLYPH[17] = {
    0x7b6f, 0x2c97, 0x73e7, 0x72cf, 0x5bc9, 0x79cf, 0x79ef, 0x7292, 0x7bef, 0x7bcf, 0x6bae, 0x2b6a, 0x5bad,
    0x4927, 0x7497, 0x388e, 0x7492,
};
#define G_B 10
#define G_O 11
#define G_K 12
#define G_L 13
#define G_I 14
#define G_S 15
#define G_T 16

#ifdef AS_HOST
void host_set(unsigned char *frame, int x, int y, int on);
int host_pixel(const unsigned char *frame, int x, int y);
void host_text(unsigned char *frame, int x, int top, const char *t);
static void put(unsigned char *frame, int x, int y, int on) { host_set(frame, x, y, on); }
static int get(const unsigned char *frame, int x, int y) { return host_pixel(frame, x, y); }
static void text(unsigned char *frame, int x, int top, const char *t) { host_text(frame, x, top, t); }
#else
static void put(unsigned char *frame, int x, int y, int on)
{
    unsigned char *b = frame + x * 8 + 7 - (y >> 3), m = (unsigned char)(1u << (y & 7));
    if (on) *b |= m; else *b &= (unsigned char)~m;
}
static int get(const unsigned char *frame, int x, int y) { return frame[x * 8 + 7 - (y >> 3)] >> (y & 7) & 1; }
/* Text in stock's own list font (0x400ba876), top row at `top`, by stock's text routine on a copy of its drawing
 * surface aimed at this frame (as pattern-sample-locks' native.c draws). */
static void text(unsigned char *frame, int x, int top, const char *t)
{
    uint32_t surface[7];
    for (unsigned i = 0; i < 7u; ++i) surface[i] = rd32(0x400bf10au + 4u * i);
    surface[3] = (uint32_t)(uintptr_t)frame;
    ((void (*)(uint32_t, void *, int, int, int, const char *))0x40012bd8u)(0x400ba876u, surface, x, 59 - top, -1, t);
}
#endif
static void invert(unsigned char *frame, int x0, int y0, int x1, int y1)
{
    for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) put(frame, x, y, !get(frame, x, y));
}

static void glyph(unsigned char *frame, int x, int y, unsigned g)
{
    uint16_t bits = GLYPH[g];
    for (int r = 0; r < 5; ++r) for (int k = 0; k < 3; ++k) if (bits & (1u << (14 - r * 3 - k))) put(frame, x + k, y + r, 0);
}

/* A four-letter word and a number (1-16) knocked out of a solid box at the screen's right edge, rows top to top + 8. */
static void label(unsigned char *frame, int top, const uint8_t word[4], unsigned n)
{
    int digits = n >= 10 ? 2 : 1;
    int dx = 124 - 4 * (digits - 1), bx = dx - 19;          /* right-aligned: the word, a space, the number */
    for (int y = top; y < top + 9; ++y) for (int x = bx - 2; x < 128; ++x) put(frame, x, y, 1);
    for (int i = 0; i < 4; ++i) glyph(frame, bx + 4 * i, top + 2, word[i]);
    if (digits == 2) glyph(frame, dx, top + 2, n / 10);
    glyph(frame, 124, top + 2, n % 10);
}

/* In ARR > CHANGE, over whatever stock drew there: "BOOK 12" at the top right, and "LIST 3" (the setlist CUE + YES
 * adds to) at the bottom right. */
/* The SETLSTS page, in a window where stock's lists open: "SETLIST 3" and its song count on an inverted title row,
 * then seven songs as "book.arrangement name" ("2.5 SONG CHARLIE"), the cursor's row inverted. */
static void draw_page(struct as_state *s, unsigned char *frame)
{
    for (int y = 2; y <= 62; ++y) for (int x = 23; x <= 103; ++x) put(frame, x, y, 0);
    for (int y = 2; y <= 61; ++y) { put(frame, 23, y, 1); put(frame, 102, y, 1); put(frame, 103, y + 1, 1); }
    for (int x = 23; x <= 102; ++x) { put(frame, x, 2, 1); put(frame, x, 61, 1); put(frame, x + 1, 62, 1); }
    if (s->lists_unread) { text(frame, 25, 4, "SETLISTS"); invert(frame, 24, 3, 101, 9); text(frame, 25, 12, "READING..."); return; }
    unsigned l = s->lists.open, n = s->lists.count[l];
    char line[24], *t = list_title(s, line, l);
    *t = 0;
    text(frame, 25, 4, line);
    t = put_num(line, n); t = put_text(t, "/16"); *t = 0;
    text(frame, 101 - 4 * (int)(t - line), 4, line);
    invert(frame, 24, 3, 101, 9);
    if (!n) { text(frame, 25, 12, "EMPTY"); return; }
    for (unsigned r = 0; r < PAGE_ROWS && s->page_top + r < n; ++r) {
        const struct sl_entry *e = &s->lists.e[l][s->page_top + r];
        t = put_num(line, e->book + 1u); *t++ = '.'; t = put_num(t, e->slot + 1u); *t++ = ' ';
        for (unsigned k = 0; k < 13u && e->name[k]; ++k) *t++ = e->name[k];
        *t = 0;
        int top = 12 + 7 * (int)r;
        text(frame, 25, top, line);
        if (s->page_top + r == s->page_cur) invert(frame, 24, top - 1, 101, top + 5);
    }
}

/* The playing setlist over the main screen while CUE is held: the cursor's row inverted, the song playing marked '*',
 * the one to come next '>'. Seven rows, kept so the cursor is in view. */
static void draw_peek(struct as_state *s, unsigned char *frame)
{
    for (int y = 2; y <= 62; ++y) for (int x = 23; x <= 103; ++x) put(frame, x, y, 0);
    for (int y = 2; y <= 61; ++y) { put(frame, 23, y, 1); put(frame, 102, y, 1); put(frame, 103, y + 1, 1); }
    for (int x = 23; x <= 102; ++x) { put(frame, x, 2, 1); put(frame, x, 61, 1); put(frame, x + 1, 62, 1); }
    unsigned n = s->sl_n, cur = s->sl_cur, nx = s->peek_next, cs = s->peek_cur;
    char line[24], *t = list_title(s, line, s->sl_list);
    *t = 0;
    text(frame, 25, 4, line);
    t = put_num(line, cur + 1u); *t++ = '/'; t = put_num(t, n); *t = 0;
    text(frame, 101 - 4 * (int)(t - line), 4, line);
    invert(frame, 24, 3, 101, 9);
    unsigned top = cs >= PAGE_ROWS ? cs - PAGE_ROWS + 1u : 0;
    for (unsigned r = 0; r < PAGE_ROWS && top + r < n; ++r) {
        unsigned k = top + r;
        const struct sl_entry *e = &s->sl_e[k];
        t = line;
        *t++ = k == nx ? '>' : k == cur ? '*' : ' ';
        t = put_num(t, e->book + 1u); *t++ = '.'; t = put_num(t, e->slot + 1u); *t++ = ' ';
        for (unsigned i = 0; i < 12u && e->name[i]; ++i) *t++ = e->name[i];
        *t = 0;
        int y = 12 + 7 * (int)r;
        text(frame, 25, y, line);
        if (k == cs) invert(frame, 24, y - 1, 101, y + 5);
    }
}

/* While a setlist plays, the main screen's left column shows it instead of the Part and the pattern (no arrangement row is
 * covered): the Part box (x 11-40, white on black) its name over two lines of 7 (broken at a space where it can; "LIST n"
 * without one), and the pattern line under it (x 10-41) the setlist sign (a vertical line and three rules) and the
 * song's place ("2/6"). */
static void draw_mark(struct as_state *s, unsigned char *frame)
{
    char a[8], b[8], *t;
    const char *n = s->lists.name[s->sl_list];
    unsigned k = 0;
    while (k < 15u && n[k]) ++k;
    while (k && n[k - 1] == ' ') --k;
    a[0] = b[0] = 0;
    if (!k) { t = put_text(a, "LIST "); t = put_num(t, s->sl_list + 1u); *t = 0; }
    else {
        unsigned cut = k <= 7u ? k : 7u;
        if (k > 7u) for (unsigned i = 7u; i > 0; --i)        /* at a space, when the rest then fits the second line */
            if (n[i] == ' ') { if (k - i - 1u <= 7u) cut = i; break; }
        unsigned i = 0;
        for (; i < cut; ++i) a[i] = n[i];
        a[i] = 0;
        unsigned from = cut;
        while (from < k && n[from] == ' ') ++from;
        for (i = 0; i < 7u && from + i < k; ++i) b[i] = n[from + i];
        b[i] = 0;
    }
    for (int y = 33; y <= 46; ++y) for (int x = 11; x <= 40; ++x) put(frame, x, y, 0);
    text(frame, 12, 34, a);
    if (b[0]) text(frame, 12, 41, b);
    invert(frame, 11, 33, 40, 46);
    char line[8];
    t = put_num(line, s->sl_cur + 1u);
    *t++ = '/';
    t = put_num(t, s->sl_n);
    *t = 0;
    for (int y = 48; y <= 56; ++y) for (int x = 10; x <= 41; ++x) put(frame, x, y, 0);
    for (int y = 50; y <= 54; ++y) put(frame, 12, y, 1);                             /* the sign's vertical line */
    for (int r = 0; r < 5; r += 2) for (int x = 14; x <= 16; ++x) put(frame, x, 50 + r, 1);   /* and its rules */
    text(frame, 42 - 4 * (int)(t - line), 50, line);
}

void as_draw(unsigned char *frame)
{
    struct as_state *s = ready();
    if (s->peek) { draw_peek(s, frame); return; }
    if (s->sl_active && !s->sl_go && !s->page && !rd32(CHANGE_WINDOW) && rd32(KEYS + K_YES * 0x18u) == MAIN_YES)
        draw_mark(s, frame);
    if (s->page) { draw_page(s, frame); return; }
    if (!rd32(CHANGE_WINDOW)) return;
    static const uint8_t book[4] = {G_B, G_O, G_O, G_K}, list[4] = {G_L, G_I, G_S, G_T};
    label(frame, 0, book, s->book + 1u);
    label(frame, 55, list, s->lists.open + 1u);
}
