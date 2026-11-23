/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Andrey Sidorkin
 *
 * SONGBOOK on the host: songbook.c (built with -DAS_HOST) against simulated battery RAM and simulated stock
 * routines: the per-file arrangement writer and reader (a file holds one slot's working and saved blocks and all 8
 * flags, as stock's do), the file copy, the project job's writer, clear and the message box. verify.py runs it. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int as_key(int code, int pressed);
void as_tick(void *api);
void as_draw(unsigned char *frame);
int as_writer(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4);
void as_setlists_open(void);
void as_project_load(void);
void as_project_new(void);
unsigned as_project_loaded(void);
unsigned as_project_stored(void);
unsigned as_project_reloaded(void);
unsigned as_modules_ready(void);
void as_host_reinstall(void);
void as_host_keep_memory(void);
void as_host_back_memory(void);

#define LEFT 0x34
#define RIGHT 0x21
#define CUE 0x2a
#define YES 0x31
#define NO 0x32
#define UP 0x33
#define DOWN 0x20
#define PLAY 0x28
#define FUNC 0x2d
#define REC 0x29
#define STOP 0x27
#define BLOCK 5652u
#define SLOTS 8u
#define W(n) (4u + (n) * BLOCK)                  /* offsets into cs1 */
#define S(n) (4u + (SLOTS + (n)) * BLOCK)
#define FLAGS (4u + 2u * SLOTS * BLOCK)
#define CHG 0x1614cu

static uint8_t cs1[0x16200];             /* 0x10000000 .. : the slots, their flags, the changed mask */
static uint8_t rec[20];                  /* 0x100f8f10 */
static uint8_t misc[0x2000];             /* 0x80004f00 .. */
static uint32_t change_window, playing, yes_handler;
static uint8_t chosen;               /* 0x100b14d1: the arrangement chosen */
static uint8_t arr_mode;             /* 0x460d1aec's low byte: arranger mode */
static uint8_t mode_wanted;          /* 0x80000010: the mode stock applies when a load ends */
static uint8_t msg_up;               /* 0x460d1e70's low byte: a message box is open */
static int closes, events;
static char chain_text[15];
static int shared_writes;
void host_post_event(void) { ++events; }
static uint8_t list[16];             /* 0x400c0ba0: the CHANGE list's first row shown, then its cursor */
static int failures, checks;

uint8_t host_rd8(uint32_t a)
{
    if (a >= 0x10000000u && a < 0x10000000u + sizeof cs1) return cs1[a - 0x10000000u];
    if (a >= 0x100f8f10u && a <= 0x100f8f23u) return rec[a - 0x100f8f10u];
    if (a >= 0x400c0b40u && a < 0x400c0b44u) return (uint8_t)(change_window >> (8 * (3 - (a - 0x400c0b40u))));
    if (a >= 0x800065b8u && a < 0x800065bcu) return (uint8_t)(playing >> (8 * (3 - (a - 0x800065b8u))));
    if (a >= 0x80004f00u && a < 0x80004f00u + sizeof misc) return misc[a - 0x80004f00u];
    if (a == 0x100b14d1u) return chosen;
    if (a == 0x460d1aefu) return arr_mode;
    if (a >= 0x460d1aecu && a < 0x460d1aefu) return 0;
    if (a == 0x460d1e73u) return msg_up;
    if (a >= 0x460d1e70u && a < 0x460d1e73u) return 0;
    if (a >= 0x400c0ba0u && a < 0x400c0bb0u) return list[a - 0x400c0ba0u];
    if (a >= 0x46c7d8deu + 0x31u * 0x18u && a < 0x46c7d8deu + 0x31u * 0x18u + 4u)   /* YES's routed handler */
        return (uint8_t)(yes_handler >> (8 * (3 - (a - (0x46c7d8deu + 0x31u * 0x18u)))));
    fprintf(stderr, "read of 0x%08x\n", a);
    return 0;
}
void host_wr8(uint32_t a, uint8_t v)
{
    if (a >= 0x10000000u && a < 0x10000000u + sizeof cs1) { cs1[a - 0x10000000u] = v; return; }
    if (a >= 0x100f8f10u && a <= 0x100f8f23u) { rec[a - 0x100f8f10u] = v; return; }
    if (a == 0x80000010u) { mode_wanted = v; return; }
    if (a >= 0x400c0ba0u && a < 0x400c0bb0u) { list[a - 0x400c0ba0u] = v; return; }
    if (a >= 0x80006694u && a < 0x80006698u) { misc[a - 0x80004f00u] = v; return; }   /* stock's CHAIN */
    if (a >= 0x400b4652u && a < 0x400b465cu) { chain_text[a - 0x400b4652u] = (char)v; return; }   /* the chain row's text */
    if (a >= 0x400b465cu && a < 0x400b4660u) { ++shared_writes; return; }   /* " %d": stock's shared format, never ours */
    fprintf(stderr, "write of 0x%08x\n", a);
}

/* ---- stock's arrangement files: one slot's two blocks and all 8 flags */
struct file { char path[64]; uint8_t work[BLOCK], saved[BLOCK], flags[SLOTS]; int used; };
static struct file files[300];
static int writes, reads, fail_read;
static const char *fail_read_path;      /* one file that will not read, NULL none */
static int fail_bak_writes_after = -1;  /* backup writes that succeed before the rest fail, -1 never */
static struct file *find(const char *path)
{
    for (int i = 0; i < 300; ++i) if (files[i].used && !strcmp(files[i].path, path)) return &files[i];
    return 0;
}
static struct file *create(const char *path)
{
    struct file *f = find(path);
    for (int i = 0; i < 300 && !f; ++i) if (!files[i].used) { f = &files[i]; f->used = 1; snprintf(f->path, sizeof f->path, "%s", path); }
    return f;
}
static void files_remove(const char *path) { struct file *f = find(path); if (f) f->used = 0; }
static int file_count(void) { int n = 0; for (int i = 0; i < 300; ++i) n += files[i].used; return n; }
int host_arr_write(const char *path, unsigned slot)
{
    if (fail_bak_writes_after >= 0 && strstr(path, ".bak")) {
        if (fail_bak_writes_after == 0) return -1;
        --fail_bak_writes_after;
    }
    struct file *f = create(path);
    memcpy(f->work, &cs1[W(slot)], BLOCK); memcpy(f->saved, &cs1[S(slot)], BLOCK); memcpy(f->flags, &cs1[FLAGS], SLOTS);
    ++writes;
    return 0;
}
static const char *fail_open_path;      /* one file whose opens fail fail_opens more times while it stays there */
static int fail_opens, fail_exists;     /* fail_exists: the exists check fails too (answers "not there") */
int host_open_read(const char *path)
{
    if (fail_open_path && !strcmp(path, fail_open_path) && fail_opens > 0) { --fail_opens; return -1; }
    return find(path) ? 0 : -1;
}
int host_exists(const char *path)
{
    if (fail_exists && fail_open_path && !strcmp(path, fail_open_path)) return 0;
    return find(path) != 0;
}
int host_read_slot(const char *path, unsigned slot)
{
    struct file *f = find(path);
    if (!f) return 0;
    if (fail_read || (fail_read_path && !strcmp(path, fail_read_path))) return -1;
    memcpy(&cs1[W(slot)], f->work, BLOCK); memcpy(&cs1[S(slot)], f->saved, BLOCK); memcpy(&cs1[FLAGS], f->flags, SLOTS);
    cs1[CHG] &= (uint8_t)~(1u << slot);
    ++reads;
    return 1;
}
int host_put(const char *path, const void *src, unsigned n) { struct file *f = create(path); memcpy(f->work, src, n); return 1; }
int host_get(const char *path, void *dst, unsigned n) { struct file *f = find(path); if (!f) return 0; memcpy(dst, f->work, n); return 1; }
int host_copy(const char *to, const char *from)
{
    struct file *f = find(from);
    if (!f) return 0;
    struct file *t = create(to);
    memcpy(t->work, f->work, BLOCK); memcpy(t->saved, f->saved, BLOCK); memcpy(t->flags, f->flags, SLOTS);
    return 1;
}

/* ---- the rest of stock */
static int posted, post_refuse, stock_writes, clears;
static char last_message[32];
int host_post_write(void) { if (post_refuse) return post_refuse == 7 ? -7 : -3; ++posted; return 1; }
const char *host_projdir(void) { return "/SET/PROJ"; }
void host_clear_arr(unsigned slot) { ++clears; memset(&cs1[W(slot)], 0, BLOCK); memset(&cs1[S(slot)], 0, BLOCK); cs1[FLAGS + slot] = 0; }
void host_message(const char *t) { snprintf(last_message, sizeof last_message, "%s", t); }
static int list_draws;
void host_draw_change(void) { ++list_draws; }
void host_arr_mode_on(void) { arr_mode = 1; }
int host_close_message(void) { msg_up = 0; ++closes; return 0; }
static int select_busy;                         /* frames stock refuses select_arr (a change of its own pending) */
static int applies;
void host_arr_apply(int on) { ++applies; arr_mode = (uint8_t)!!on; }
int host_select_arr(unsigned n) { if (select_busy) { --select_busy; return -1; } chosen = (uint8_t)n; return 0; }
/* Stock's writer: each changed slot (or one with no file yet) to arr0N.work. */
int host_stock_writer(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    (void)a1; (void)a3; (void)a4;
    char p[64];
    /* As stock's writer (0x40091680): a slot whose bit is in the mask, or whose file is missing, is written, and its
     * bit in 0x1001614c is then SET (0x40091748): that byte says "may differ from the project's saved copy". */
    for (unsigned n = 0; n < SLOTS; ++n) {
        snprintf(p, sizeof p, "/SET/PROJ/arr%02u.work", n + 1);
        if ((a2 >> n & 1) || !find(p)) { host_arr_write(p, n); cs1[CHG] |= (uint8_t)(1u << n); }
    }
    ++stock_writes;
    return 0;
}
static unsigned char lcd[1024];
/* Stock's text routine, as the module calls it: what was drawn where, in order. */
static struct { int x, top; char t[32]; } texts[16];
static int ntexts;
void host_text(unsigned char *frame, int x, int top, const char *s)
{
    (void)frame;
    if (ntexts < 16) { texts[ntexts].x = x; texts[ntexts].top = top; snprintf(texts[ntexts].t, sizeof texts[ntexts].t, "%s", s); ++ntexts; }
}
static const char *text_at(int top) { for (int i = 0; i < ntexts; ++i) if (texts[i].top == top) return texts[i].t; return ""; }
static uint32_t chain(void) { const uint8_t *c = &misc[0x80006694u - 0x80004f00u]; return (uint32_t)c[0] << 24 | c[1] << 16 | c[2] << 8 | c[3]; }
/* The arranger past its last row: stock takes the CHAIN (0x4004a7e2) */
static void stock_chain(void) { if (chain() != 0xffffffffu) { chosen = (uint8_t)chain(); memset(&misc[0x80006694u - 0x80004f00u], 0xff, 4); } }
static void page_draw(void) { memset(lcd, 0, sizeof lcd); ntexts = 0; as_draw(lcd); }
void host_set(unsigned char *frame, int x, int y, int on) { unsigned char *b = frame + x * 8 + 7 - (y >> 3), m = (unsigned char)(1u << (y & 7)); if (on) *b |= m; else *b &= (unsigned char)~m; }
static int lit(int x, int y) { return lcd[x * 8 + 7 - (y >> 3)] >> (y & 7) & 1; }
int host_pixel(const unsigned char *frame, int x, int y) { return frame[x * 8 + 7 - (y >> 3)] >> (y & 7) & 1; }
/* The main screen's arrangement row as stock draws it: a black bar, the icon's three white lines, then the name. */
static void main_screen(void)
{
    memset(lcd, 0, sizeof lcd);
    for (int x = 42; x < 128; ++x) for (int y = 32; y <= 38; ++y) host_set(lcd, x, y, 1);
    for (int x = 45; x <= 49; ++x) for (int y = 33; y <= 37; y += 2) host_set(lcd, x, y, 0);
}

static void check(const char *name, int ok)
{
    ++checks;
    if (!ok) { ++failures; printf("[FAIL] %s\n", name); }
    else printf("[PASS] %s\n", name);
}

/* The 8 slots marked with one byte, so a book's content can be told apart; flags too. */
static void mark(uint8_t m)
{
    for (unsigned i = 4; i < FLAGS; ++i) cs1[i] = (uint8_t)(m + i % 7);
    for (unsigned n = 0; n < SLOTS; ++n) cs1[FLAGS + n] = (uint8_t)((m + n) & 1);
}
static int marked(uint8_t m)
{
    for (unsigned i = 4; i < FLAGS; ++i) if (cs1[i] != (uint8_t)(m + i % 7)) return 0;
    for (unsigned n = 0; n < SLOTS; ++n) if (cs1[FLAGS + n] != (uint8_t)((m + n) & 1)) return 0;
    return 1;
}
static void put_arr01_08(void)
{
    char p[64];
    for (unsigned n = 0; n < SLOTS; ++n) { snprintf(p, sizeof p, "/SET/PROJ/arr%02u.work", n + 1); host_arr_write(p, n); }
}
static int tap(int code) { int t = as_key(code, 1); as_key(code, 0); return t; }
static void job(void) { as_writer(0x100b14f0u, 0xff, 0, 0); }   /* the card task runs the posted job */
static unsigned diag(void) { struct { volatile uint32_t value; } api = {0}; as_tick((void *)&api); return api.value; }
static unsigned book(void) { return diag() & 15; }
/* ARR > CHANGE's cursor on arrangement n (all 8 shown: the first row is 1) */
static void choose(unsigned n) { memset(list, 0, sizeof list); list[7] = (uint8_t)n; list[11] = (uint8_t)n; }
/* arrlists.work as written: setlist l's song count, and its entry i (book, slot, name) */
static int lists_count(unsigned l) { struct file *f = find("/SET/PROJ/arrlists.work"); return f ? f->work[8 + l] : -1; }
static const uint8_t *entry(unsigned l, unsigned i) { return &find("/SET/PROJ/arrlists.work")->work[24 + (l * 16 + i) * 18]; }
static void settle(void) { for (int i = 0; i < 125; ++i) as_tick(0); }   /* stock's load is over */

int main(void)
{
    as_modules_ready();
    check("installed with no record: book 1", book() == 0);

    mark(0x10);                                    /* book 1's songs, already on the card */
    put_arr01_08();
    change_window = 0;
    check("LEFT/RIGHT outside ARR > CHANGE are stock's", tap(RIGHT) == 0 && tap(LEFT) == 0);
    change_window = 0x46c7d384u;
    playing = 1;
    check("while playing, RIGHT in ARR > CHANGE is taken but changes nothing", tap(RIGHT) == 1 && posted == 0 && !strcmp(last_message, "STOP TO CHANGE BOOK"));
    playing = 0;
    check("LEFT at book 1 changes nothing", tap(LEFT) == 1 && posted == 0);
    check("RIGHT posts stock's write job", tap(RIGHT) == 1 && posted == 1);
    check("a second press while it runs is ignored", tap(RIGHT) == 1 && posted == 1);
    job();
    check("the job wrote book 1 through stock's writer", stock_writes == 1);
    check("leaving book 1 kept its backup, arr01.bak .. arr08.bak", find("/SET/PROJ/arr01.bak") && find("/SET/PROJ/arr08.bak"));
    check("a book never used starts as 8 cleared arrangements", clears == 8);
    as_tick(0);
    check("the open list is drawn again with the new book's names, once", list_draws == 1 && (as_tick(0), list_draws == 1));
    check("a book never visited starts on arrangement 1", chosen == 0);
    msg_up = 1; as_tick(0);
    check("the job's NOW SAFE TO EJECT! is closed as it opens", msg_up == 0 && closes == 1);
    msg_up = 1; as_tick(0);
    check("only that one: a later message stays", msg_up == 1);
    msg_up = 0;
    check("now on book 2, nothing changed, nothing written as arr09 yet", book() == 1 && cs1[CHG] == 0 && !find("/SET/PROJ/arr09.work"));

    mark(0x20); cs1[CHG] = 0x04;                   /* book 2's songs, one changed */
    int w = writes;
    job();
    check("a write on book 2 writes all 8 of arr09-arr16, not arr01-08", stock_writes == 1 && writes == w + 8 && find("/SET/PROJ/arr09.work") && find("/SET/PROJ/arr16.work") && cs1[CHG] == 0);
    w = writes;
    job();
    check("the SYNC job asks for all 8: all 8 written again, as stock does", writes == w + 8);
    w = writes;
    as_writer(0x100b14f0u, 0, 0, 0);
    check("a job asking for none, with nothing changed: nothing written", writes == w);
    /* the list's FUNC + PLAY: an arrangement cleared without being marked changed */
    host_clear_arr(4); cs1[CHG] = 0;
    tap(LEFT); job(); as_tick(0); tap(RIGHT); job(); as_tick(0);
    check("a clear the list did not mark changed stays cleared after leaving the book", book() == 1 && cs1[W(4) + 30] == 0 && cs1[W(3) + 30] != 0);
    mark(0x20); job();                             /* book 2 as before, for the checks below */

    tap(RIGHT); job();                             /* to book 3, new */
    mark(0x30); cs1[CHG] = 1;
    tap(LEFT); job();                              /* back to book 2: book 3 written first */
    check("back on book 2: its songs and flags are in the slots", book() == 1 && marked(0x20) && cs1[CHG] == 0);
    check("book 3 went to arr17-arr24", find("/SET/PROJ/arr17.work") && find("/SET/PROJ/arr24.work"));
    tap(RIGHT); job();
    check("book 3's songs come back", book() == 2 && marked(0x30));
    chosen = 5; tap(LEFT); job(); as_tick(0);                       /* arrangement 6 chosen in book 3, then to book 2 */
    check("book 2 comes back on its own last choice, not book 3's", book() == 1 && chosen == 0);
    chosen = 2; tap(RIGHT); job(); as_tick(0);
    check("and book 3 on arrangement 6 again", book() == 2 && chosen == 5);
    check("with the list's cursor on it", list[7] == 5 && list[4] == 0);
    tap(LEFT); job(); tap(LEFT); job();
    check("back on book 1: its songs from the backup", book() == 0 && marked(0x10));
    int sw = stock_writes;
    check("arriving at book 1 marks all 8 changed, so stock rewrites arr01-08", cs1[CHG] == 0xff);
    job();
    check("then stock's writer writes them", stock_writes == sw + 1 && find("/SET/PROJ/arr03.work")->work[100] == cs1[W(2) + 100]);

    /* arr01-08 overwritten while the module was away: book 1 comes back from the backup and repairs them */
    tap(RIGHT); job();                             /* to book 2; the backup is fresh */
    mark(0x66);
    put_arr01_08();
    mark(0x20);
    tap(LEFT); job();
    check("damaged arr01-08: book 1 still comes back from its backup", book() == 0 && marked(0x10));
    job();
    { struct file *f = find("/SET/PROJ/arr03.work"); check("and stock's next write repairs arr01-08", f && f->work[100] == cs1[W(2) + 100]); }

    post_refuse = 1;
    tap(RIGHT);
    check("a refused job leaves the book as it is", book() == 0 && !strcmp(last_message, "CARD BUSY"));
    post_refuse = 7;
    tap(RIGHT);
    check("with no project loaded it says so", book() == 0 && !strcmp(last_message, "NO PROJECT"));
    post_refuse = 0;

    tap(RIGHT); job();                             /* book 2 */
    fail_read = 1;
    tap(LEFT); job();
    check("a file that will not read is reported", (as_tick(0), !strcmp(last_message, "BOOK FILE ERROR")));
    fail_read = 0;
    { uint8_t keep4 = find("/SET/PROJ/arr04.work")->work[9];
      int w = writes, sw = stock_writes; job();      /* a sync on book 1, read only in part, nothing edited */
      check("a sync on book 1 read only in part writes none of arr01-08 (stock's writer would write all 8)", writes == w && stock_writes == sw && find("/SET/PROJ/arr04.work")->work[9] == keep4);
    }
    tap(RIGHT); job(); tap(LEFT); job();
    check("nothing marked changed after it, so arr01-08 stay as they are", cs1[CHG] == 0 && find("/SET/PROJ/arr04.work")->work[9] == (uint8_t)(0x10 + (W(3) + 9) % 7));
    tap(RIGHT); job(); tap(LEFT); job();
    check("and book 1 comes back from arr01-08, the backup left alone", book() == 0 && marked(0x10));

    /* the owner's review (#1737): a backup file missing, unreadable, or written only in part */
    tap(RIGHT); job(); as_tick(0);                 /* book 2; book 1's backup is fresh */
    mark(0x20); cs1[CHG] = 0;
    files_remove("/SET/PROJ/arr03.bak");           /* the owner's case: one backup file missing */
    { uint8_t keep3 = find("/SET/PROJ/arr03.work")->work[100];
      tap(LEFT); job(); as_tick(0);
      check("a missing arr03.bak: slot 3 comes from arr03.work, unmarked", book() == 0 && cs1[W(2) + 100] == keep3 && !(cs1[CHG] & 0x04));
      check("the slots that read from their backup are marked, so arr01-08 are repaired", (cs1[CHG] & 0xfb) == 0xfb);
      job();
      check("and the next save leaves arr03.work as it was, not empty", find("/SET/PROJ/arr03.work")->work[100] == keep3); }
    tap(RIGHT); job(); as_tick(0);                 /* book 2 again; a full backup is written */
    mark(0x20); cs1[CHG] = 0;
    fail_read_path = "/SET/PROJ/arr05.bak";        /* one backup file there but not readable */
    { uint8_t keep5 = find("/SET/PROJ/arr05.work")->work[100];
      tap(LEFT); job(); as_tick(0);
      check("an unreadable arr05.bak: slot 5 comes from arr05.work, unmarked", book() == 0 && cs1[W(4) + 100] == keep5 && !(cs1[CHG] & 0x10));
      job();
      check("and the next save leaves arr05.work as it was", find("/SET/PROJ/arr05.work")->work[100] == keep5); }
    fail_read_path = 0;
    /* a backup write that fails half way when leaving book 1 is never read back */
    mark(0x10); cs1[CHG] = 0xff; job();          /* book 1 as it should be, written to arr01-08 */
    fail_bak_writes_after = 3;
    tap(RIGHT); job(); as_tick(0);
    fail_bak_writes_after = -1;
    mark(0x20);
    { uint8_t want = find("/SET/PROJ/arr07.work")->work[100];
      find("/SET/PROJ/arr07.bak")->work[100] = (uint8_t)(want + 1);  /* the stale backup differs from arr07.work */
      tap(LEFT); job(); as_tick(0);
      check("after a partial backup, book 1 comes from arr01-08, nothing marked", book() == 0 && cs1[W(6) + 100] == want && cs1[CHG] == 0); }

    /* the owner's review (#1737): an existing arrangement file that fails to open once is not "no such file" */
    tap(RIGHT); job(); as_tick(0);                 /* book 2 */
    mark(0x20); cs1[CHG] = 0xff; job();            /* book 2's songs saved to arr09-16 */
    { uint8_t keep11 = find("/SET/PROJ/arr11.work")->work[100];
      tap(RIGHT); job(); as_tick(0);               /* book 3 */
      fail_open_path = "/SET/PROJ/arr11.work"; fail_opens = 1;
      last_message[0] = 0;
      tap(LEFT); job(); as_tick(0);                /* back to book 2: arr11.work does not open this once */
      check("arr11.work there but not opened: reported as a book file error", book() == 1 && !strcmp(last_message, "BOOK FILE ERROR"));
      as_host_reinstall(); as_modules_ready();      /* the owner's review: the module reloads before the next sync */
      check("after a reinstall it still knows book 2 was read only in part", book() == 1);
      int w0 = writes; job();                      /* the next sync, nothing edited */
      check("the next sync writes none of book 2's files", writes == w0);
      check("and arr11.work keeps its saved arrangement", find("/SET/PROJ/arr11.work")->work[100] == keep11);
      cs1[W(0) + 100] ^= 0x5a; cs1[CHG] = 0x01; job();   /* an edit of slot 1 on that book */
      check("an edit on that book is still saved, to arr09.work only", find("/SET/PROJ/arr09.work")->work[100] == cs1[W(0) + 100] && find("/SET/PROJ/arr11.work")->work[100] == keep11);
      fail_open_path = 0; fail_opens = 0;
      /* the same after Undo: this copy comes back with its memory from before the failed read (another build ran) */
      tap(RIGHT); job(); as_tick(0);              /* book 3 */
      as_host_keep_memory();
      fail_open_path = "/SET/PROJ/arr11.work"; fail_opens = 1;
      tap(LEFT); job(); as_tick(0);                /* the failed read happens in another build's trial */
      fail_open_path = 0; fail_opens = 0;
      as_host_back_memory(); as_modules_ready();
      { int w1 = writes; job();
        check("back after Undo: the next sync still writes nothing of book 2", book() == 1 && writes == w1 && find("/SET/PROJ/arr11.work")->work[100] == keep11); }
      tap(RIGHT); job(); tap(LEFT); job(); as_tick(0);
      check("opened again later, arr11.work reads into slot 3", book() == 1 && cs1[W(2) + 100] == keep11);
      tap(RIGHT); job();
      fail_open_path = "/SET/PROJ/arr11.work"; fail_opens = 1; fail_exists = 1;
      tap(LEFT); job(); as_tick(0);                /* the open and the exists check fail once, the second open works */
      check("a failed open and a failed exists check: the second open reads it", book() == 1 && cs1[W(2) + 100] == keep11 && cs1[CHG] == 0);
      fail_open_path = 0; fail_opens = 0; fail_exists = 0; }
    mark(0x20); cs1[CHG] = 0xff; job();            /* book 2 saved as before the edit, for the cases below */
    tap(LEFT); job(); as_tick(0);                  /* back to book 1 */

    /* reinstalls and power cycles */
    tap(RIGHT); job();
    check("on book 2", book() == 1 && marked(0x20));
    as_host_reinstall(); as_modules_ready();
    check("reinstalled without a project load: the slots still hold book 2, and it knows", book() == 1);
    mark(0x10);                                    /* a power cycle: stock read arr01-08 (book 1) into the slots */
    as_host_reinstall(); as_modules_ready();
    check("after a power cycle the slots hold book 1, and it knows", book() == 0);
    as_modules_ready();
    check("a second ev_modules_ready changes nothing", book() == 0);
    tap(RIGHT); job();
    check("from there, book 2's own songs come back", book() == 1 && marked(0x20));

    as_project_load(); as_project_loaded();       /* stock's first job: it only checks the files */
    check("while stock's first load job runs, the slots still hold book 2, and it knows", book() == 1 && marked(0x20));
    { int sw0 = stock_writes; cs1[CHG] = 1; job();   /* a write between the two jobs (the developer's book 1 loss) */
      check("a write between the two jobs saves book 2 to its own files, never arr01-08", stock_writes == sw0 && book() == 1); }
    as_project_new(); mark(0x10);                 /* the second: the new-project step, then arr01-08 into the slots */
    check("a project load: book 1", book() == 0);

    /* a kept build on book 2; a trial of another build switches to book 1; the kept build comes back with its memory */
    tap(RIGHT); job();
    check("the kept build on book 2", book() == 1 && marked(0x20));
    as_host_keep_memory();
    as_host_reinstall(); as_modules_ready();       /* the trial build */
    check("the trial build sees book 2", book() == 1);
    tap(LEFT); job();
    check("and switches to book 1", book() == 0 && marked(0x10));
    as_host_back_memory(); as_modules_ready();     /* rolled back: the kept build, its memory saying book 2 */
    check("the kept build does not trust its old memory: book 1", book() == 0);
    w = writes;
    cs1[CHG] = 1; job();
    check("so a write goes to arr01-08, not book 1's songs into arr09-arr16", stock_writes > 0 && find("/SET/PROJ/arr09.work")->work[50] == (uint8_t)(0x20 + (W(0) + 50) % 7));
    as_modules_ready();
    check("with nothing else run since, its own memory stands", book() == 0);

    /* SAVE PROJECT and RELOAD PROJECT */
    tap(RIGHT); job();
    int before = file_count();
    as_project_stored();
    check("SAVE PROJECT copies arr09-arr24 and arrbook to .strd, and nothing past them", find("/SET/PROJ/arr09.strd") && find("/SET/PROJ/arr24.strd") && !find("/SET/PROJ/arr25.strd") && find("/SET/PROJ/arrbook.strd") && file_count() == before + 17);
    mark(0x55); cs1[CHG] = 1; job();               /* book 2 edited and written after the save */
    as_project_reloaded(); as_project_new(); mark(0x10);
    check("RELOAD PROJECT leaves the slots on book 1", book() == 0);
    tap(RIGHT); job();
    check("book 2 is as saved, not as edited after", book() == 1 && marked(0x20));


    /* a project load comes back to the book and arrangement it was left on */
    while (book() != 2) { tap(book() < 2 ? RIGHT : LEFT); job(); as_tick(0); }
    chosen = 4; job();                             /* book 3, arrangement 5, SYNC */
    as_project_load(); as_project_loaded();
    { int p1 = posted; for (int i = 0; i < 300; ++i) as_tick(0);
      check("no return switch between stock's two load jobs, however long the gap", posted == p1 && book() == 2); }
    as_project_new(); mark(0x10); chosen = 1;      /* LOAD's new-project step, then arr01-08 */
    check("after stock's new-project step: book 1, as stock loaded it", book() == 0);
    settle();
    post_refuse = 3; as_tick(0); as_tick(0);
    check("while stock is still loading, it waits", book() == 0 && cs1[CHG] == 0);
    post_refuse = 0; as_tick(0); job(); as_tick(0);
    check("then it goes back to book 3, on arrangement 5", book() == 2 && marked(0x30) && chosen == 4);
    check("left in pattern mode: it stays in pattern mode", arr_mode == 0);
    arr_mode = 1; as_tick(0); job();               /* arranger mode on, SYNC */
    mode_wanted = 0; arr_mode = 0; as_tick(0);     /* LOAD switches it off and clears the byte as it starts ... */
    job();                                         /* ... and saves the project before loading it */
    as_project_load();
    check("left in arranger mode: during the load, the byte stock applies at its end says arranger", mode_wanted == 1);
    as_project_new(); mark(0x10); arr_mode = mode_wanted;  /* stock's end of the load applies it */
    settle(); job(); as_tick(0); as_tick(0);
    for (int i = 0; i < 130; ++i) as_tick(0);     /* two seconds of quiet */
    check("after LOAD: arranger mode, on book 3", arr_mode == 1 && book() == 2);
    as_key(0x2d, 1); as_key(0x2e, 1); as_key(0x2e, 0); as_key(0x2d, 0); arr_mode = 0; as_tick(0);  /* FUNC + PATTERN: off */
    job();                                         /* SYNC in pattern mode */
    mode_wanted = 0; as_tick(0); job();            /* LOAD: off, save, load */
    as_project_load();
    check("switched off by the musician: LOAD leaves pattern mode", mode_wanted == 0);
    as_project_new(); mark(0x10); settle(); job(); for (int i = 0; i < 130; ++i) as_tick(0);
    check("and nothing turns it on", arr_mode == 0 && book() == 2);
    /* RELOAD PROJECT: where it was saved, not where it was left after */
    as_project_stored();                           /* saved on book 3, arrangement 5 */
    tap(LEFT); job(); as_tick(0); chosen = 6; job();  /* book 2, arrangement 7, SYNC */
    as_project_reloaded(); as_project_load(); as_project_new(); mark(0x10);   /* no ev_banks_loaded after a reload */
    settle(); job(); as_tick(0);
    check("RELOAD PROJECT comes back to where it was saved: book 3, arrangement 5", book() == 2 && chosen == 4);
    /* right after a RELOAD in arranger mode, stock refuses the choice for a few frames */
    as_project_stored();
    tap(LEFT); job(); as_tick(0); chosen = 6; job();
    as_project_reloaded(); as_project_new(); mark(0x10); settle(); job();
    select_busy = 5; chosen = 1;
    for (int i = 0; i < 10; ++i) as_tick(0);
    check("a refused choice is tried again until stock takes it: book 3, arrangement 5", book() == 2 && chosen == 4 && select_busy == 0);
    /* RELOAD in arranger mode: stock's end of the load turned it on over book 1; the return applies it again */
    as_project_stored();
    tap(LEFT); job(); as_tick(0); chosen = 6; job();
    { int a0 = applies;
      arr_mode = 1; as_project_reloaded(); as_project_new(); mark(0x10); settle(); job();
      for (int i = 0; i < 3; ++i) as_tick(0);
      check("after RELOAD in arranger mode the return applies arranger mode again, once", applies == a0 + 1 && arr_mode == 1 && book() == 2);
      tap(LEFT); job(); for (int i = 0; i < 3; ++i) as_tick(0);
      check("a LEFT/RIGHT switch never does", applies == a0 + 1);
      tap(RIGHT); job(); for (int i = 0; i < 3; ++i) as_tick(0);
      arr_mode = 0; }
    /* RELOAD as the unit raised it the second time: the reload, stock's new-project step, and no ev_banks_load */
    as_project_stored();                           /* saved on book 3, arrangement 5 */
    tap(LEFT); job(); as_tick(0); chosen = 6; job();
    as_project_reloaded(); as_project_new(); mark(0x10);
    settle(); job(); as_tick(0);
    check("RELOAD without ev_banks_load still comes back: book 3, arrangement 5", book() == 2 && chosen == 4);
    as_project_load(); as_project_loaded(); as_project_new(); mark(0x10); settle(); job(); as_tick(0);
    /* saved on book 1: a load stays there, with no job */
    while (book() != 0) { tap(LEFT); job(); as_tick(0); }
    job();
    int p0 = posted;
    as_project_load(); as_project_new(); mark(0x10); settle(); job(); as_tick(0);
    check("left on book 1: it stays on book 1, with no job", book() == 0 && posted == p0 && marked(0x10));
    tap(RIGHT); job(); as_tick(0);                 /* book 2 again for the label checks */

    /* the label in ARR > CHANGE */
    memset(lcd, 0, sizeof lcd);
    as_draw(lcd);
    check("BOOK 2 knocked out of a solid box at the top right", lit(103, 0) && !lit(102, 0) && lit(127, 8) && !lit(105, 2) && !lit(124, 2) && !lit(126, 2) && lit(120, 2));
    for (int i = 0; i < 10; ++i) { tap(RIGHT); job(); }
    memset(lcd, 0, sizeof lcd);
    as_draw(lcd);
    check("book 12: two digits, 1 then 2, the box wider", book() == 11 && lit(99, 0) && !lit(101, 2) && lit(120, 2) && !lit(121, 2) && !lit(120, 3) && !lit(124, 2) && !lit(126, 2));
    for (int i = 0; i < 4; ++i) { tap(RIGHT); job(); }
    check("book 16 is the last", book() == 15 && tap(RIGHT) == 1 && book() == 15);
    mark(0x77); cs1[CHG] = 1; job();
    check("book 16 is arr121-arr128", find("/SET/PROJ/arr121.work") && find("/SET/PROJ/arr128.work"));
    memset(lcd, 0xff, sizeof lcd);
    as_draw(lcd);
    check("drawn the same over a lit background", lit(99, 0) && !lit(101, 2));
    change_window = 0;
    memset(lcd, 0, sizeof lcd);
    as_draw(lcd);
    check("nothing drawn outside ARR > CHANGE", !lit(99, 0) && !lit(127, 8));
    main_screen(); as_draw(lcd);
    check("the main screen's mode icon is left alone", lit(44, 33) && !lit(45, 33) && lit(45, 34));
    int d = list_draws;
    post_refuse = 0; change_window = 0x46c7d384u; tap(LEFT); change_window = 0; job(); as_tick(0);
    check("a switch that ends with the list closed draws nothing", list_draws == d);

    /* setlists: CUE + YES adds, CUE + LEFT/RIGHT moves an arrangement in its book, arrlists.work with the project */
    as_project_new(); mark(0x10); cs1[CHG] = 0; chosen = 0;   /* a new project: book 1, no setlists */
    files_remove("/SET/PROJ/arrlists.work"); files_remove("/SET/PROJ/arrlists.strd");
    for (unsigned n = 0; n < SLOTS; ++n) { char nm[17]; snprintf(nm, sizeof nm, "SONG %c", 'A' + n); memset(&cs1[W(n)], 0, 16); memcpy(&cs1[W(n)], nm, strlen(nm)); }
    change_window = 0x46c7d384u;
    choose(2);
    as_key(CUE, 1);
    check("CUE + YES in ARR > CHANGE is taken", tap(YES) == 1);
    as_key(CUE, 0);
    check("and says what it added where", !strcmp(last_message, "B1 3 ADDED TO LIST 1"));
    check("YES alone stays stock's", tap(YES) == 0);
    { struct file *f = find("/SET/PROJ/arrlists.work"); check("nothing written before the next sync", !f); }
    job();
    { struct file *f = find("/SET/PROJ/arrlists.work");
      check("the next sync writes arrlists.work: one song, book 1 arrangement 3, its name", f && !memcmp(f->work, "ASSL", 4) && f->work[8] == 1 && f->work[24] == 0 && f->work[25] == 2 && !memcmp(&f->work[26], "SONG C", 6)); }
    choose(5);
    as_key(CUE, 1); as_key(YES, 1); as_key(YES, 1); as_key(YES, 0); as_key(CUE, 0);   /* a held YES repeats */
    job();
    check("a held YES adds once", lists_count(0) == 2 && !strcmp(last_message, "B1 6 ADDED TO LIST 1"));
    memcpy(&cs1[W(2)], "INTRO", 6); job();
    check("a renamed song's new name is in the setlist after the next sync", !memcmp(entry(0, 0) + 2, "INTRO", 6));
    /* moving: arrangement 3 (in the setlist, chosen) one place down */
    chosen = 2; choose(2);
    uint8_t a3 = cs1[W(2) + 100], a4 = cs1[W(3) + 100], s3 = cs1[S(2) + 100], f3 = cs1[FLAGS + 2], f4 = cs1[FLAGS + 3];
    int q0 = posted;
    as_key(CUE, 1);
    check("CUE + RIGHT in ARR > CHANGE is taken", tap(RIGHT) == 1);
    as_key(CUE, 0);
    check("arrangements 3 and 4 changed places, working and saved copies and flags", cs1[W(3) + 100] == a3 && cs1[W(2) + 100] == a4 && cs1[S(3) + 100] == s3 && cs1[FLAGS + 3] == f3 && cs1[FLAGS + 2] == f4);
    check("both are marked changed, so the next sync writes them", (cs1[CHG] & 0x0c) == 0x0c && posted == q0);
    check("the choice, the list's cursor and the chosen arrangement go with it", list[11] == 3 && list[7] == 3 && chosen == 3);
    job();
    check("the setlist still points at the same song, now arrangement 4", entry(0, 0)[1] == 3 && !memcmp(entry(0, 0) + 2, "INTRO", 6));
    check("and arr03/arr04 hold the swapped arrangements", find("/SET/PROJ/arr04.work")->work[100] == a3 && find("/SET/PROJ/arr03.work")->work[100] == a4);
    choose(0); cs1[CHG] = 0;                        /* the sync before set its bits, as stock's writer does */
    as_key(CUE, 1); tap(LEFT); as_key(CUE, 0);
    check("nothing moves above arrangement 1", list[11] == 0 && cs1[CHG] == 0);
    playing = 1; choose(3); cs1[CHG] = 0;
    as_key(CUE, 1); tap(LEFT); as_key(CUE, 0);
    playing = 0;
    check("while playing it says so and moves nothing", !strcmp(last_message, "STOP TO MOVE") && cs1[CHG] == 0 && cs1[W(3) + 100] == a3);
    p0 = posted;
    as_key(CUE, 1); tap(RIGHT); as_key(CUE, 0);
    check("CUE + RIGHT never switches the book", book() == 0 && posted == p0);
    as_key(CUE, 1); tap(LEFT); as_key(CUE, 0); job();          /* back as they were */
    /* a full setlist */
    for (int i = 0; i < 14; ++i) { as_key(CUE, 1); tap(YES); as_key(CUE, 0); }
    job();
    check("16 songs fill a setlist", lists_count(0) == 16);
    as_key(CUE, 1); tap(YES); as_key(CUE, 0); job();
    check("a 17th is refused, and it says so", lists_count(0) == 16 && !strcmp(last_message, "LIST 1 IS FULL"));
    /* SAVE, RELOAD, LOAD, a new project */
    as_project_stored();
    check("SAVE PROJECT copies arrlists.work to arrlists.strd", find("/SET/PROJ/arrlists.strd") && find("/SET/PROJ/arrlists.strd")->work[8] == 16);
    find("/SET/PROJ/arrlists.work")->work[8] = 3;                /* edited since */
    as_project_reloaded(); as_project_new(); job();
    as_key(CUE, 1); tap(YES); as_key(CUE, 0); job();
    check("RELOAD PROJECT brings the saved setlists back", lists_count(0) == 16 && !strcmp(last_message, "LIST 1 IS FULL"));
    as_project_new(); job();
    as_key(CUE, 1); tap(YES); as_key(CUE, 0); job();
    check("a new project starts with empty setlists", lists_count(0) == 1);
    as_project_stored();
    find("/SET/PROJ/arrlists.work")->work[8] = 5;
    as_project_load(); as_project_new(); job();
    as_key(CUE, 1); tap(YES); as_key(CUE, 0); job();
    check("a project load reads arrlists.work", lists_count(0) == 6);
    files_remove("/SET/PROJ/arrlists.strd");
    as_project_reloaded(); as_project_new(); job();
    check("RELOAD of a project saved before it had setlists: none, written so", lists_count(0) == 0);
    find("/SET/PROJ/arrlists.work")->work[0] = 'X';
    as_project_load(); as_project_new(); job();
    as_key(CUE, 1); tap(YES); as_key(CUE, 0); job();
    check("an arrlists.work that is not ours reads as no setlists", lists_count(0) == 1);
    memset(lcd, 0, sizeof lcd);
    as_draw(lcd);
    check("LIST 1 knocked out of a solid box at the bottom right", lit(105, 55) && !lit(102, 55) && lit(127, 63) && !lit(105, 57) && !lit(105, 61) && lit(106, 57) && !lit(124, 58) && lit(123, 57));
    check("BOOK 1 still at the top right", lit(103, 0) && !lit(105, 2));
    change_window = 0;
    as_key(CUE, 1);
    check("CUE + YES outside ARR > CHANGE stays stock's", tap(YES) == 0);
    as_key(CUE, 0);

    /* the SETLSTS page (the Arranger menu's CHAIN item): setlist 1 holds one song here (INTRO, now arrangement 4) */
    change_window = 0x46c7d384u;
    for (unsigned n = 0; n < 3; ++n) { choose(n == 0 ? 0 : n == 1 ? 4 : 7); as_key(CUE, 1); tap(YES); as_key(CUE, 0); }
    change_window = 0;
    job();
    check("four songs in setlist 1 for the page checks", lists_count(0) == 4);
    page_draw();
    check("nothing of the page before it is opened", ntexts == 0);
    as_setlists_open();
    page_draw();
    check("the page: SETLIST 1 and its count on the title row", !strcmp(text_at(4), "SETLIST 1") && !strcmp(texts[1].t, "4/16"));
    check("its songs as book.arrangement and name", !strcmp(text_at(12), "1.4 INTRO") && !strcmp(text_at(19), "1.1 SONG A") && !strcmp(text_at(26), "1.5 SONG E") && !strcmp(text_at(33), "1.8 SONG H"));
    check("the title row inverted, the first song under the cursor too", lit(60, 3) && lit(100, 11) && !lit(100, 18));
    check("UP/DOWN are the page's; PLAY stays stock's", tap(DOWN) == 1 && tap(UP) == 1 && tap(PLAY) == 0);
    tap(DOWN); tap(DOWN); page_draw();
    check("DOWN moves the cursor", lit(100, 25) && !lit(100, 11));
    as_key(CUE, 1); tap(LEFT); as_key(CUE, 0); job();
    check("CUE + LEFT moves the song up, and the cursor with it", entry(0, 1)[1] == 4 && entry(0, 2)[1] == 0 && (page_draw(), lit(100, 18)));
    as_key(FUNC, 1);
    check("FUNC + PLAY is the page's", tap(PLAY) == 1);
    as_key(FUNC, 0); job();
    check("and takes the song out of the setlist, the arrangement left in its book", lists_count(0) == 3 && entry(0, 1)[1] == 0 && !memcmp(&cs1[W(4)], "SONG E", 6));
    for (int i = 0; i < 5; ++i) tap(DOWN);
    page_draw();
    check("the cursor stops at the last song", lit(100, 25) && !lit(100, 32));
    tap(UP); tap(UP); tap(DOWN);                     /* the cursor on song 2 of 3 */
    as_key(FUNC, 1); tap(REC); as_key(FUNC, 0);
    check("FUNC + REC copies the song under the cursor", !strcmp(last_message, "COPIED SONG"));
    check("STOP alone stays the transport's", tap(STOP) == 0);
    as_key(FUNC, 1);
    check("FUNC + STOP is the page's", tap(STOP) == 1);
    as_key(FUNC, 0); job();
    check("and pastes it after the cursor: four songs, the copy third", lists_count(0) == 4 && entry(0, 2)[0] == entry(0, 1)[0] && entry(0, 2)[1] == entry(0, 1)[1]);
    as_key(FUNC, 1); tap(PLAY); as_key(FUNC, 0); job();   /* the copy out again */
    check("FUNC + PLAY takes the pasted copy out again", lists_count(0) == 3);
    as_key(CUE, 1); as_key(FUNC, 1); tap(REC); as_key(FUNC, 0); as_key(CUE, 0);
    check("CUE + FUNC + REC copies the whole setlist", !strcmp(last_message, "COPIED LIST 1"));
    tap(RIGHT); as_key(FUNC, 1); tap(STOP); as_key(FUNC, 0); job();
    check("FUNC + STOP on setlist 2 pastes all three", lists_count(1) == 3 && entry(1, 0)[1] == entry(0, 0)[1] && entry(1, 2)[1] == entry(0, 2)[1]);
    for (int i = 0; i < 3; ++i) { as_key(FUNC, 1); tap(PLAY); as_key(FUNC, 0); }   /* setlist 2 empty again */
    job(); tap(LEFT);
    tap(RIGHT); page_draw();
    check("RIGHT shows setlist 2, empty", !strcmp(text_at(4), "SETLIST 2") && !strcmp(text_at(12), "EMPTY"));
    tap(NO); page_draw();
    check("NO closes the page", ntexts == 0);
    change_window = 0x46c7d384u; choose(1);
    as_key(CUE, 1); tap(YES); as_key(CUE, 0);
    check("and CUE + YES now adds to setlist 2", !strcmp(last_message, "B1 2 ADDED TO LIST 2"));
    memset(lcd, 0, sizeof lcd); as_draw(lcd);
    check("LIST 2 shown in ARR > CHANGE", !lit(124, 57) && !lit(126, 57));
    change_window = 0; job();
    for (int i = 0; i < 9; ++i) { change_window = 0x46c7d384u; choose(i % 8); as_key(CUE, 1); tap(YES); as_key(CUE, 0); }
    change_window = 0; job();
    as_setlists_open(); for (int i = 0; i < 9; ++i) tap(DOWN); page_draw();
    check("a long setlist scrolls: the cursor's song on the last row", lit(100, 54) && !lit(100, 47) && !strcmp(text_at(54), "1.1 SONG A") && !strcmp(text_at(12), "1.3 SONG D"));
    /* installed into an open project: its setlists are read by a card job before they are shown or added to */
    as_host_reinstall(); as_modules_ready();
    { int pp = posted; change_window = 0x46c7d384u; choose(1);
      as_key(CUE, 1); tap(YES); as_key(CUE, 0); change_window = 0;
      check("after an install, CUE + YES first reads the setlists: a card job, READING SETLISTS, nothing added", posted == pp + 1 && !strcmp(last_message, "READING SETLISTS"));
      as_setlists_open(); page_draw();
      check("the page meanwhile says READING", !strcmp(text_at(12), "READING...") && posted == pp + 1);
      job(); page_draw();
      check("once read, the page shows setlist 2 with its ten songs", !strcmp(text_at(4), "SETLIST 2") && !strcmp(texts[1].t, "10/16"));
      tap(NO); }
    as_project_load(); page_draw();
    check("a project load closes the page", ntexts == 0);
    as_project_new(); job();

    /* playing a setlist: B2 3, B1 2, B1 5 (one song from another book's file, two from the slots) */
    memset(&misc[0x80006694u - 0x80004f00u], 0xff, 4);
    files_remove("/SET/PROJ/arrlists.work");
    as_project_new();                                 /* a new project, not a load: no setlists */
    mark(0x10); cs1[CHG] = 0xff; job();               /* book 1, saved */
    change_window = 0x46c7d384u;
    tap(RIGHT); job(); as_tick(0);
    mark(0x20); cs1[CHG] = 0xff; job();               /* book 2, saved as arr09-16 */
    choose(2); as_key(CUE, 1); tap(YES); as_key(CUE, 0);
    tap(LEFT); job(); as_tick(0);                     /* back to book 1 */
    choose(1); as_key(CUE, 1); tap(YES); as_key(CUE, 0);
    choose(4); as_key(CUE, 1); tap(YES); as_key(CUE, 0);
    change_window = 0; job();
    check("the setlist: B2 3, B1 2, B1 5", book() == 0 && lists_count(0) == 3 && entry(0, 0)[0] == 1 && entry(0, 0)[1] == 2 && entry(0, 2)[1] == 4);
    uint8_t s0 = find("/SET/PROJ/arr11.work")->work[100], s1 = cs1[W(1) + 100], s2 = cs1[W(4) + 100], b7 = cs1[W(6) + 100], b8 = cs1[W(7) + 100];
    uint8_t saved2 = find("/SET/PROJ/arr02.work")->saved[100];
    chosen = 4; as_setlists_open(); page_draw();
    check("the page opened stopped: the cursor on the arrangement chosen now (B1 5, the third song)", lit(100, 25) && !lit(100, 11));
    tap(NO); chosen = 0;
    playing = 1; as_setlists_open(); tap(UP); tap(UP); tap(YES);
    check("YES while playing: it says so and starts nothing", !strcmp(last_message, "STOP TO START") && (page_draw(), ntexts > 0));
    playing = 0;
    int pp = posted; tap(YES);
    check("YES on the first song posts the card job and closes the page", posted == pp + 1 && (page_draw(), ntexts == 0));
    job();
    check("the first two songs in slots 7 and 8: one from book 2's file, one from book 1's slot", cs1[W(6) + 100] == s0 && cs1[W(7) + 100] == s1);
    as_tick(0);
    check("the first song chosen, the second chained, arranger mode on", chosen == 6 && chain() == 7 && arr_mode == 1);
    stock_chain(); as_tick(0);
    check("past the first song: the third into the freed slot 7, chained", chosen == 7 && cs1[W(6) + 100] == s2 && chain() == 6);
    as_setlists_open(); page_draw();
    check("the page opened while it plays: the cursor on the song playing (the second)", lit(100, 18) && !lit(100, 11));
    tap(NO);
    memset(&misc[0x80006694u - 0x80004f00u], 0xff, 4); as_tick(0);   /* something clears the chain mid-song */
    { int e0 = events;
      memset(&misc[0x80006694u - 0x80004f00u], 0xff, 4); as_tick(0);
      check("and stock's arranger event is posted, so the rows are drawn with it", chain() == 6 && events == e0 + 1);
      check("the chain row names the next song (its first 8 characters), ended within 10 bytes", chain_text[0] == '>' && !memcmp(&chain_text[1], &cs1[W(6)], 8) && chain_text[9] == 0); }
    check("a chain cleared while a song plays is set again the next frame", chain() == 6 && cs1[W(6) + 100] == s2);
    cs1[W(7) + 200] ^= 0xff;                         /* the second song edited while it plays */
    uint8_t edited = cs1[W(7) + 200];
    { int w = writes; job();
      check("a save while the setlist plays writes none of the book's files", writes == w && find("/SET/PROJ/arr07.work")->work[100] == b7); }
    stock_chain(); as_tick(0);
    check("past the second song: the last one plays, nothing chained (it ends as stock ends any arrangement)", chosen == 6 && chain() == 0xffffffffu);
    { int e0 = events; memset(&misc[0x80006694u - 0x80004f00u], 0, 4); as_tick(0);
      check("a chain that appears on the last song is taken away again, the rows drawn again", chain() == 0xffffffffu && events == e0 + 1); }
    playing = 0; as_tick(0);
    check("stopped at its end, the last song stays chosen", chosen == 6 && cs1[W(6) + 100] == s2);
    change_window = 0x46c7d384u; pp = posted; as_tick(0);
    check("opening ARR > CHANGE, stopped, ends the setlist with a card job", posted == pp + 1);
    job(); as_tick(0);
    check("the edited song saved to its own file, arr02.work", find("/SET/PROJ/arr02.work")->work[200] == edited);
    check("its ARR SAVE copy kept", find("/SET/PROJ/arr02.work")->saved[100] == saved2);
    check("the book back in its slots: slots 7 and 8 its own again, the edit in slot 2", cs1[W(6) + 100] == b7 && cs1[W(7) + 100] == b8 && cs1[W(1) + 200] == edited && cs1[CHG] == 0);
    check("songs not edited are not written", find("/SET/PROJ/arr11.work")->work[100] == s0 && chain() == 0xffffffffu);
    change_window = 0;
    /* installed again while a setlist plays: slots 7 and 8 hold its songs; nothing of them is written, the book read */
    as_setlists_open(); tap(UP); tap(UP); tap(YES); job(); as_tick(0);
    check("a setlist playing again: its songs in slots 7 and 8, the record says so", cs1[W(6) + 100] == s0 && rec[7] == 1);
    as_host_reinstall(); as_modules_ready();
    { int w = writes, sw = stock_writes, pp = posted; as_tick(0);
      check("installed again: a card job is posted to read the book back", posted == pp + 1);
      job(); as_tick(0);
      check("and it reads the book, slots 7 and 8 its own again, writing nothing", writes == w && stock_writes == sw && cs1[W(6) + 100] == b7 && cs1[W(7) + 100] == b8 && rec[7] == 0); }
    { int sw = stock_writes; cs1[CHG] = 1; job();
      check("after it the book is written as usual", stock_writes == sw + 1); }

    /* a song whose file does not read: the setlist does not start, the book stays */
    fail_read_path = "/SET/PROJ/arr11.work";
    as_setlists_open(); tap(YES); job(); as_tick(0);
    check("a song file that does not read: reported, the book as it was", !strcmp(last_message, "BOOK FILE ERROR") && cs1[W(6) + 100] == b7);
    { int sw = stock_writes; cs1[CHG] = 1; job();
      check("and no setlist: the next save writes the book again", stock_writes == sw + 1); }
    fail_read_path = 0;
    /* starting from the last song: nothing chained */
    as_setlists_open(); tap(DOWN); tap(DOWN); tap(YES); job(); as_tick(0);
    check("from the last song: it alone, nothing chained", chosen == 6 && cs1[W(6) + 100] == s2 && chain() == 0xffffffffu);
    { int sw = stock_writes; job();
      check("while it plays, the next save leaves the book's files alone", stock_writes == sw); }
    /* CUE + YES on the main screen: the setlist playing; UP/DOWN the cursor, a further YES the '>' to it, CUE released applies it */
    change_window = 0x46c7d384u; as_tick(0); job(); change_window = 0; as_tick(0);   /* the last run ended */
    as_setlists_open(); tap(UP); tap(UP); tap(YES); job(); as_tick(0);              /* again from the first song */
    check("a setlist from the first song: song 2 next", chosen == 6 && chain() == 7 && cs1[W(7) + 100] == s1);
    yes_handler = 0x40058f34u;                       /* the editor's: not the main screen */
    as_key(CUE, 1);
    check("CUE + YES in the editor is not the setlist's", tap(YES) == 0);
    as_key(CUE, 0);
    yes_handler = 0x4005e4c8u;                       /* the main screen's */
    as_key(CUE, 1);
    check("CUE + YES on the main screen opens the playing setlist", tap(YES) == 1);
    page_draw();
    check("its title; the cursor on the song playing; the next marked >", !strcmp(text_at(4), "SETLIST 1") && !strcmp(texts[1].t, "1/3") && lit(100, 11) && text_at(19)[0] == '>');
    check("the opening YES moved nothing", text_at(19)[0] == '>');
    tap(DOWN); tap(DOWN); page_draw();
    check("UP/DOWN move the cursor only: the third row inverted, the > still on the second, * on the first", lit(100, 25) && !lit(100, 11) && text_at(19)[0] == '>' && text_at(12)[0] == '*' && text_at(26)[0] == ' ');
    check("YES is the window's", tap(YES) == 1);
    page_draw();
    check("and moves the > to the cursor", text_at(26)[0] == '>' && text_at(19)[0] == ' ');
    as_key(CUE, 0); page_draw();
    check("releasing CUE closes it and puts the third song next, chained", ntexts == 0 && cs1[W(7) + 100] == s2 && chain() == 7);
    stock_chain(); as_tick(0);
    check("past the first song the third plays, and it is the last: nothing chained", chosen == 7 && chain() == 0xffffffffu);
    /* the window open across a song change: the * and the > follow, unless a song was chosen with YES */
    change_window = 0x46c7d384u; as_tick(0); job(); change_window = 0; as_tick(0);
    as_setlists_open(); tap(UP); tap(UP); tap(YES); job(); as_tick(0);              /* from the first song */
    as_key(CUE, 1); tap(YES);                         /* open, nothing chosen */
    stock_chain(); as_tick(0); page_draw();           /* song 2 starts while it is open */
    check("open across a song change: * on song 2, > on song 3", text_at(19)[0] == '*' && text_at(26)[0] == '>' && text_at(12)[0] == ' ' && !strcmp(texts[1].t, "2/3"));
    as_key(CUE, 0);
    check("released with nothing chosen: song 3 stays next", chain() == 6 && cs1[W(6) + 100] == s2);
    change_window = 0x46c7d384u; as_tick(0); job(); change_window = 0; as_tick(0);
    as_setlists_open(); tap(UP); tap(UP); tap(YES); job(); as_tick(0);              /* from the first song again */
    as_key(CUE, 1); tap(YES); tap(DOWN); tap(DOWN); tap(YES);   /* song 3 chosen to follow song 1 */
    stock_chain(); as_tick(0); page_draw();           /* but song 2 starts before CUE is released */
    check("a chosen song stays marked through the change", text_at(26)[0] == '>' && text_at(19)[0] == '*');
    as_key(CUE, 0);
    check("and follows the song now playing when CUE is released", chain() == 6 && cs1[W(6) + 100] == s2);
    yes_handler = 0;

    as_project_load(); as_project_new(); mark(0x10); as_tick(0);
    { int sw = stock_writes; cs1[CHG] = 1; job();
      check("a project load ends it: the next save writes the book again", stock_writes == sw + 1); }

    check("stock's shared number format after the chain text (0x400b465d) was never written", shared_writes == 0);
    printf("%d checks, %d failed\n", checks, failures);
    return failures != 0;
}
