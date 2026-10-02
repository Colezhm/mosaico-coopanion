// SPDX-License-Identifier: MIT
#include "coop_subtitle.h"
#include <stdbool.h>
#include <string.h>
/* Three lines of 18 full-width glyphs (432 px at 22 px). ASCII uses a
 * conservative 2/3 cell. Explicit line breaks avoid GSP's centered-label path.
 *
 * Typography, all without dropping or reordering a byte (pages concatenate
 * back to the input):
 * - closing punctuation may hang one glyph past the edge instead of starting
 *   a line (kinsoku);
 * - ASCII words wrap at the last space on the line;
 * - a page that would otherwise cut a sentence ends after the page's last
 *   sentence end, once at least one full line is filled; failing that, after a
 *   clause mark that leaves its last line at least a third full. */
#define LINE_CELLS 54
#define HANG_CELLS 3
#define PAGE_LINES 3
#define MIN_CLAUSE_CELLS (LINE_CELLS / 3)

static size_t glyph_bytes(const unsigned char *p)
{
    size_t n = *p >= 0xf0 ? 4 : *p >= 0xe0 ? 3 : *p >= 0xc0 ? 2 : 1;
    for (size_t k = 1; k < n; k++)
        if (!p[k] || (p[k] & 0xc0) != 0x80)
            return 1;
    return n;
}
static bool glyph_in(const unsigned char *p, size_t n, const char *const set[], size_t count)
{
    for (size_t i = 0; i < count; i++)
        if (strlen(set[i]) == n && !memcmp(p, set[i], n))
            return true;
    return false;
}
static bool closing(const unsigned char *p, size_t n)
{
    static const char *const set[] = {"，", "。", "！", "？", "、", "；", "：", "）", "」", "』",
                                      "》", "”", "’", "…", ",", ".", "!", "?", ";", ":", ")"};
    return glyph_in(p, n, set, sizeof(set) / sizeof(set[0]));
}
static bool sentence_end(const unsigned char *p, size_t n)
{
    static const char *const set[] = {"。", "！", "？", "…", "!", "?"};
    return glyph_in(p, n, set, sizeof(set) / sizeof(set[0]));
}
static bool clause_end(const unsigned char *p, size_t n)
{
    static const char *const set[] = {"，", "；", "：", "、", ",", ";"};
    return glyph_in(p, n, set, sizeof(set) / sizeof(set[0]));
}
static bool word_glyph(const unsigned char *p)
{
    return *p < 128 && *p != ' ' && *p != '\n';
}

typedef struct {
    unsigned page, line, width;
    size_t used;
    unsigned wanted;
    char *out;
    /* Resume points after break candidates. Sentence/clause marks span the
     * page; spaces only the current line. */
    const unsigned char *sentence, *clause, *space;
    size_t sentence_used, clause_used, space_used;
    unsigned page_cells;
} layout_t;

static void put(layout_t *l, const unsigned char *p, size_t n)
{
    if (l->out && l->page == l->wanted && l->used + n < COOP_SUBTITLE_PAGE_BYTES) {
        memcpy(l->out + l->used, p, n);
        l->used += n;
    }
}
static void new_line(layout_t *l)
{
    l->width = 0;
    l->space = l->clause = NULL;
    if (++l->line == PAGE_LINES) {
        l->page++;
        l->line = 0;
        l->sentence = NULL;
        l->page_cells = 0;
    } else if (l->out && l->page == l->wanted && l->used + 1 < COOP_SUBTITLE_PAGE_BYTES)
        l->out[l->used++] = '\n';
}
static void rewind_to(layout_t *l, size_t used)
{
    if (l->out && l->page == l->wanted)
        l->used = used;
}
static void new_page(layout_t *l, size_t used)
{
    rewind_to(l, used);
    l->line = PAGE_LINES - 1;
    new_line(l);
}
static unsigned layout(const char *text, unsigned wanted, char *out)
{
    layout_t l = {.wanted = wanted, .out = out};
    if (out)
        out[0] = 0;
    const unsigned char *begin = (const unsigned char *)text;
    const unsigned char *p = begin;
    while (p && *p) {
        if (*p == '\n') {
            new_line(&l);
            p++;
            continue;
        }
        size_t n = glyph_bytes(p);
        unsigned cells = *p < 128 ? 2 : 3;
        unsigned fit_cells = cells;
        bool whole_word = word_glyph(p) && (p == begin || !word_glyph(p - 1));
        if (whole_word) {
            unsigned width = 0;
            for (const unsigned char *q = p; *q && word_glyph(q) && width <= LINE_CELLS; q++)
                width += 2;
            whole_word = width <= LINE_CELLS;
            if (whole_word)
                fit_cells = width;
        }
        if (l.width + fit_cells > LINE_CELLS) {
            bool hang = *p == ' ' || (closing(p, n) && l.width + cells <= LINE_CELLS + HANG_CELLS);
            if (!hang) {
                if (l.line == PAGE_LINES - 1 && l.sentence) {
                    p = l.sentence;
                    new_page(&l, l.sentence_used);
                    continue;
                }
                if (l.line == PAGE_LINES - 1 && l.clause) {
                    p = l.clause;
                    new_page(&l, l.clause_used);
                    continue;
                }
                if (!whole_word && word_glyph(p) && l.space) {
                    rewind_to(&l, l.space_used);
                    p = l.space;
                    new_line(&l);
                    continue;
                }
                new_line(&l);
            }
        }
        put(&l, p, n);
        l.width += cells;
        l.page_cells += cells;
        size_t here = l.out && l.page == l.wanted ? l.used : 0;
        if (*p == ' ') {
            l.space = p + n;
            l.space_used = here;
        }
        if (sentence_end(p, n) && l.page_cells >= LINE_CELLS) {
            l.sentence = p + n;
            l.sentence_used = here;
        }
        if (l.line == PAGE_LINES - 1 && clause_end(p, n) && l.width >= MIN_CLAUSE_CELLS) {
            l.clause = p + n;
            l.clause_used = here;
        }
        p += n;
    }
    if (out)
        out[l.used] = 0;
    /* A trailing explicit/automatic break must not create an empty last page. */
    return l.page + (l.line > 0 || l.width > 0 || l.page == 0 ? 1 : 0);
}
unsigned coop_subtitle_pages(const char *text)
{
    return layout(text, 0, NULL);
}
void coop_subtitle_page(const char *text, unsigned page, char out[COOP_SUBTITLE_PAGE_BYTES])
{
    layout(text, page, out);
}
