#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../lib/formats.h"

static const char *mock_text = "";
static gboolean mock_fill_buffer = FALSE;

static const char *mock_gettext(const char *msgid) {
    (void)msgid;
    return mock_text;
}

static gint mock_g_snprintf(gchar *string, gulong n, const gchar *format, ...) {
    if(mock_fill_buffer) {
        memset(string, 'A', n);
        return (gint)n;
    }

    va_list args;
    va_start(args, format);
    gint result = vsnprintf(string, n, format, args);
    va_end(args);
    return result;
}

#undef _
#define _(s) mock_gettext(s)

#undef g_snprintf
#define g_snprintf mock_g_snprintf

/* Include the implementation so the static rm_fmt_progress_format_text() can
 * be tested directly. The macros above keep the test independent of gettext
 * and allow a controlled, non-NUL-terminated buffer for the bound-check case.
 */
#include "../../lib/formats/progressbar.c"

volatile int rm_session_abort_count = 0;

void rm_session_acknowledge_abort(gint abort_count) {
    (void)abort_count;
}

char *rm_format_elapsed_time(gdouble elapsed_sec, int sec_precision) {
    (void)elapsed_sec;
    (void)sec_precision;
    return g_strdup("0s");
}

void rm_util_size_to_human_readable(RmOff num, char *in, gsize len) {
    (void)num;
    if(len > 0) {
        in[0] = '\0';
    }
}

void rm_running_mean_init(RmRunningMean *m, int max_values) {
    (void)m;
    (void)max_values;
}

void rm_running_mean_add(RmRunningMean *m, gdouble value) {
    (void)m;
    (void)value;
}

gdouble rm_running_mean_get(RmRunningMean *m) {
    (void)m;
    return 0.0;
}

void rm_running_mean_unref(RmRunningMean *m) {
    (void)m;
}

const char *rm_fmt_get_config_value(RmFmtTable *self, const char *formatter,
                                    const char *key) {
    (void)self;
    (void)formatter;
    (void)key;
    return NULL;
}

#define CHECK(cond) do { \
    if(!(cond)) { \
        g_printerr("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        exit(EXIT_FAILURE); \
    } \
} while(0)

/* Returns TRUE if `text` is already in the given normalization form. The
 * multilingual samples below rely on exact NFC/NFD byte sequences; this check
 * makes the test fail loudly if an editor or tool normalizes the source file.
 */
static gboolean is_normalized(const char *text, GNormalizeMode mode) {
    char *norm = g_utf8_normalize(text, -1, mode);
    gboolean same = norm != NULL && strcmp(norm, text) == 0;
    g_free(norm);
    return same;
}

static void check_format(const char *text, int max_len,
                         guint32 expected_width, const char *expected_text) {
    RmCfg cfg = {0};
    RmSession session = {0};
    RmFmtHandlerProgress self = {0};

    session.cfg = &cfg;
    self.last_state = RM_PROGRESS_STATE_MERGE;
    mock_text = text;
    mock_fill_buffer = FALSE;

    rm_fmt_progress_format_text(&session, &self, max_len, stderr);

    CHECK(self.text_len == expected_width);
    CHECK(strcmp(self.text_buf, expected_text) == 0);
}

static void check_traverse_color(void) {
    RmCfg cfg = {0};
    RmSession session = {0};
    RmFmtHandlerProgress self = {0};

    cfg.with_color = TRUE;
    cfg.with_stdout_color = TRUE;
    session.cfg = &cfg;
    self.last_state = RM_PROGRESS_STATE_TRAVERSE;
    mock_text = "X";
    mock_fill_buffer = FALSE;

    rm_fmt_progress_format_text(&session, &self, 100, stdout);

    CHECK(self.text_len == 17);
    CHECK(strstr(self.text_buf, "\x1b[32;01m") != NULL);
    CHECK(strstr(self.text_buf, "\x1b[31;01m") != NULL);
    CHECK(strstr(self.text_buf, "\x1b[0m") != NULL);
}

static void check_buffer_bounds(void) {
    RmCfg cfg = {0};
    RmSession session = {0};
    RmFmtHandlerProgress self = {0};

    session.cfg = &cfg;
    self.last_state = RM_PROGRESS_STATE_MERGE;
    mock_text = "unused";
    /* Fill the whole buffer with valid ASCII and no NUL terminator, so the
     * loop has to stop via the buffer-bound check instead of the NUL check.
     */
    mock_fill_buffer = TRUE;

    rm_fmt_progress_format_text(&session, &self, 1024, stderr);

    CHECK(self.text_len == 1024);
    CHECK(self.text_buf[0] == 'A');
    CHECK(self.text_buf[1023] == 'A');

    mock_fill_buffer = FALSE;
}

/* Real translation samples for the progress states, measured with the same
 * GLib rules as the implementation.
 *
 * All strings are written as UTF-8 literals. NFC and NFD spellings look the
 * same in an editor, so every input whose normalization form matters is
 * guarded with is_normalized(): if an editor or tool normalizes this file, the
 * test fails instead of silently testing the wrong form.
 */
static void check_multilingual_samples(void) {
    /* Simplified Chinese */
    check_format("正在遍历", 20, 8, "正在遍历");
    check_format("正在预处理", 20, 10, "正在预处理");

    /* Traditional Chinese */
    check_format("正在遍歷", 20, 8, "正在遍歷");
    check_format("正在預處理", 20, 10, "正在預處理");
    check_format("當閱", 20, 4, "當閱");
    check_format("當前處理", 20, 8, "當前處理");

    /* Japanese */
    check_format("走査中", 20, 6, "走査中");
    check_format("前処理中", 20, 8, "前処理中");

    /* Korean: Hangul syllables are wide (2 cells), the space is 1 cell. */
    check_format("순회 중", 20, 7, "순회 중");
    check_format("전처리 중", 20, 9, "전처리 중");

    /* Vietnamese, precomposed (NFC). U+0110 has no decomposition. */
    CHECK(is_normalized("Đang duyệt", G_NORMALIZE_NFC));
    check_format("Đang duyệt", 20, 10, "Đang duyệt");
    CHECK(is_normalized("Đang tiền xử lý", G_NORMALIZE_NFC));
    check_format("Đang tiền xử lý", 20, 15,
                 "Đang tiền xử lý");

    /* Vietnamese, decomposed (NFD): combining marks count as 0 cells.
     * "duyệt" is e + U+0323 + U+0302; "tiền xử lý" is e + U+0302 + U+0300,
     * u + U+031B + U+0309 and y + U+0301.
     */
    CHECK(is_normalized("Đang duyệt", G_NORMALIZE_NFD));
    check_format("Đang duyệt", 20, 10, "Đang duyệt");
    CHECK(is_normalized("Đang tiền xử lý", G_NORMALIZE_NFD));
    check_format("Đang tiền xử lý", 20, 15,
                 "Đang tiền xử lý");

    /* Thai: U+0E31 is zero-width, U+0E33 (SARA AM) counts as 1 cell. */
    check_format("กำลังสำรวจ", 20, 9, "กำลังสำรวจ");
    check_format("กำลังประมวลผลล่วงหน้า", 30, 18, "กำลังประมวลผลล่วงหน้า");

    /* French. U+00E9 is East Asian "Ambiguous" width, and
     * g_unichar_iswide_cjk() counts it as 2 cells, so the precomposed form is
     * one cell wider than the decomposed form. This asserts the GLib semantics
     * used by the implementation, not the width on a Western terminal (13).
     */
    CHECK(is_normalized("Prétraitement", G_NORMALIZE_NFC)); /* U+00E9 */
    check_format("Prétraitement", 20, 14, "Prétraitement");
    CHECK(is_normalized("Prétraitement", G_NORMALIZE_NFD)); /* e + U+0301 */
    check_format("Prétraitement", 20, 13, "Prétraitement");

    /* Truncation: a wide character that does not fit is dropped. */
    check_format("正在預處理", 5, 4, "正在");
    check_format("순회 중", 3, 2, "순");
    check_format("전처리 중", 5, 4, "전처");

    /* Truncation keeps zero-width characters attached to the last base. */
    check_format("กำลังสำรวจ", 5, 5, "กำลังส");
    CHECK(is_normalized("Đang duyệt", G_NORMALIZE_NFD));
    check_format("Đang duyệt", 9, 9, "Đang duyệ");
    check_format("Đang duyệt", 4, 4, "Đang");
    CHECK(is_normalized("Đang duyệt", G_NORMALIZE_NFC));
    check_format("Đang duyệt", 9, 9, "Đang duyệ");

    /* Truncation with the ambiguous-width U+00E9 in both forms. */
    CHECK(is_normalized("Prétraitement", G_NORMALIZE_NFC));
    check_format("Prétraitement", 3, 2, "Pr");
    CHECK(is_normalized("Prétraitement", G_NORMALIZE_NFD));
    check_format("Prétraitement", 3, 3, "Pré");
}

int main(void) {
    check_format("hello", 10, 5, "hello");
    check_format("hello", 3, 3, "hel");
    check_format("正在遍历", 10, 8, "正在遍历");
    check_format("abc正在", 10, 7, "abc正在");
    check_format("a正", 2, 1, "a");
    CHECK(is_normalized("é", G_NORMALIZE_NFD)); /* e + U+0301 */
    check_format("é", 10, 1, "é");
    /* U+200B ZERO WIDTH SPACE is kept as an escape: it is invisible. */
    check_format("a\u200bb", 10, 2, "a\u200bb");
    CHECK(is_normalized("á", G_NORMALIZE_NFD)); /* a + U+0301 */
    check_format("á", 1, 1, "á");
    check_format("\x1b[32mabc\x1b[0m", 10, 3, "\x1b[32mabc\x1b[0m");
    check_format("\x1b[32mabc\x1b[0m", 2, 2, "\x1b[32mab");
    check_format("\x1b[0m", 10, 0, "\x1b[0m");
    check_format("\x1b[32ma", 0, 0, "\x1b[32m");
    check_format("a", 0, 0, "");

    check_multilingual_samples();
    check_traverse_color();
    check_buffer_bounds();

    return EXIT_SUCCESS;
}
