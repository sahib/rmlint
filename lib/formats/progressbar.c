/*
 *  This file is part of rmlint.
 *
 *  rmlint is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  rmlint is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with rmlint.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *
 *  - Christopher <sahib> Pahl 2010-2017 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2017 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

#include "../formats.h"
#include "../utilities.h"

#include <glib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <sys/ioctl.h>

typedef struct RmFmtHandlerProgress {
    /* must be first */
    RmFmtHandler parent;

    /* Current progress value */
    gdouble percent;

    /* Colorstripe offset of the progressbar */
    int stripe_offset;

    /* Text printed on the right side */
    char text_buf[1024];
    guint32 text_len;

    /* Config keys: */
    guint32 update_interval;
    guint8 use_unicode_glyphs;
    bool plain;

    /* Bookkeeping */
    RmFmtProgressState last_state;
    struct winsize terminal;
    GTimer *timer;

    /* Estimated Time of Arrival calculation */
    RmRunningMean read_diff_mean;
    RmRunningMean eta_mean;
    RmOff last_shred_bytes_remaining;
} RmFmtHandlerProgress;

static void rm_fmt_progress_format_preprocess(RmSession *session, char *buf,
                                              size_t buf_len, FILE *out) {
    if(session->offsets_read > 0) {
        g_snprintf(buf, buf_len, "fiemap: %s+%" LLU "%s %s-%" LLU "%s %s#%" LLU "%s",
                   MAYBE_GREEN(out, session), session->offsets_read,
                   MAYBE_RESET(out, session), MAYBE_RED(out, session),
                   session->offset_fails, MAYBE_RESET(out, session),
                   MAYBE_BLUE(out, session), session->total_filtered_files,
                   MAYBE_RESET(out, session));
    } else {
        g_snprintf(buf, buf_len, "%s %s%" LLU "%s", _("reduces files to"),
                   MAYBE_GREEN(out, session), session->total_filtered_files,
                   MAYBE_RESET(out, session));
    }
}

static gdouble rm_fmt_progress_calculated_eta(
        RmSession *session,
        RmFmtHandlerProgress *self,
        gdouble elapsed_sec,
        RmFmtProgressState state) {
    if(self->last_shred_bytes_remaining == 0) {
        self->last_shred_bytes_remaining = session->shred_bytes_remaining;
        return -1.0;
    }

    if(state != RM_PROGRESS_STATE_SHREDDER) {
        return -1.0;
    }

    RmOff last_diff = self->last_shred_bytes_remaining - session->shred_bytes_remaining;
    self->last_shred_bytes_remaining = session->shred_bytes_remaining;

    rm_running_mean_add(&self->read_diff_mean, last_diff);

    gdouble avg_bytes_read = rm_running_mean_get(&self->read_diff_mean);
    gdouble throughput = avg_bytes_read / elapsed_sec;

    gdouble eta_sec = 0;
    if(throughput != 0.0) {
        eta_sec = session->shred_bytes_remaining / throughput;
    }

    rm_running_mean_add(&self->eta_mean, eta_sec);
    return rm_running_mean_get(&self->eta_mean);
}

static void rm_fmt_progress_format_text(
        RmSession *session,
        RmFmtHandlerProgress *self,
        int max_len,
        gdouble elapsed_sec,
        FILE *out
    ) {
    /* This is very ugly, but more or less required since we need to translate
     * the text to different languages and still determine the right textlength.
     */

    char num_buf[32] = {0};
    char preproc_buf[128] = {0};
    memset(num_buf, 0, sizeof(num_buf));
    memset(preproc_buf, 0, sizeof(preproc_buf));

    switch(self->last_state) {
    case RM_PROGRESS_STATE_TRAVERSE:
        self->percent = 2.0;
        self->text_len = g_snprintf(
            self->text_buf, sizeof(self->text_buf), "%s (%s%d%s %s / %s%d%s + %s%d%s %s)",
            _("Traversing"), MAYBE_GREEN(out, session), session->total_files,
            MAYBE_RESET(out, session), _("usable files"), MAYBE_RED(out, session),
            session->ignored_files, MAYBE_RESET(out, session), MAYBE_RED(out, session),
            session->ignored_folders, MAYBE_RESET(out, session),
            _("ignored files / folders"));
        break;
    case RM_PROGRESS_STATE_PREPROCESS:
        self->percent = 2.0;
        rm_fmt_progress_format_preprocess(session, preproc_buf, sizeof(preproc_buf), out);
        self->text_len = g_snprintf(
            self->text_buf, sizeof(self->text_buf), "%s (%s / %s %s%" LLU "%s %s)",
            _("Preprocessing"), preproc_buf, _("found"), MAYBE_RED(out, session),
            session->other_lint_cnt, MAYBE_RESET(out, session), _("other lint"));
        break;
    case RM_PROGRESS_STATE_SHREDDER:
        self->percent = 1.0 - ((gdouble)session->shred_bytes_remaining /
                               (gdouble)session->shred_bytes_after_preprocess);

        gdouble eta_sec = rm_fmt_progress_calculated_eta(session, self, elapsed_sec, self->last_state);
        char *eta_info = NULL;
        if(eta_sec >= 0) {
            eta_info = rm_format_elapsed_time(eta_sec, 0);
        }

        rm_util_size_to_human_readable(
                session->shred_bytes_remaining, num_buf,
                sizeof(num_buf)
        );

        self->text_len = g_snprintf(
            self->text_buf, sizeof(self->text_buf),
                "%s (%s%" LLU "%s %s %s%" LLU "%s %s; %s%s%s %s %s%" LLU "%s %s, ETA: %s%s%s)",
                    _("Matching"), MAYBE_RED(out, session), session->dup_counter,
                    MAYBE_RESET(out, session), _("dupes of"), MAYBE_YELLOW(out, session),
                    session->dup_group_counter, MAYBE_RESET(out, session), _("originals"),
                    MAYBE_GREEN(out, session), num_buf, MAYBE_RESET(out, session),
                    _("to scan in"), MAYBE_GREEN(out, session), session->shred_files_remaining,
                    MAYBE_RESET(out, session), _("files"),
                    MAYBE_GREEN(out, session), (eta_info) ? eta_info : "0s", MAYBE_RESET(out, session)
            );

        g_free(eta_info);
        break;
    case RM_PROGRESS_STATE_MERGE:
        self->percent = 1.0;
        self->text_len = g_snprintf(self->text_buf, sizeof(self->text_buf),
                                    _("Merging files into directories (stand by...)"));
        break;
    case RM_PROGRESS_STATE_INIT:
    case RM_PROGRESS_STATE_PRE_SHUTDOWN:
    case RM_PROGRESS_STATE_SUMMARY:
    default:
        self->percent = 0;
        memset(self->text_buf, 0, sizeof(self->text_buf));
        break;
    }

    /* Support unicode messages - tranlsated text might contain some. */
    self->text_len = g_utf8_strlen(self->text_buf, self->text_len);

    /* Get rid of colors to get the correct length of the text. This is
     * necessary to correctly guess the length of the displayed text in cells.
     */
    int text_iter = 0;
    for(char *iter = &self->text_buf[0]; *iter; iter++) {
        if(*iter == '\x1b') {
            char *jump = strchr(iter, 'm');
            if(jump != NULL) {
                self->text_len -= jump - iter + 1;
                iter = jump;
                continue;
            }
        }

        if(text_iter >= max_len) {
            *iter = 0;
            self->text_len = text_iter;
            break;
        }

        text_iter++;
    }
}

static void rm_fmt_progress_print_text(RmFmtHandlerProgress *self, int width, FILE *out) {
    if(self->text_len < (unsigned)width) {
        for(guint32 i = 0; i < width - self->text_len; ++i) {
            fprintf(out, " ");
        }
    }

    fprintf(out, "%s", self->text_buf);
}

typedef enum RmProgressBarGlyph {
    PROGRESS_ARROW,
    PROGRESS_TICK_LOW,
    PROGRESS_TICK_HIGH,
    PROGRESS_TICK_SPACE,
    PROGRESS_EMPTY,
    PROGRESS_FULL,
    PROGRESS_LEFT_BRACKET,
    PROGRESS_RIGHT_BRACKET
} RmProgressBarGlyph;

static const char *PROGRESS_FANCY_UNICODE_TABLE[] = {[PROGRESS_ARROW] = "➤",
                                                     [PROGRESS_TICK_LOW] = "□",
                                                     [PROGRESS_TICK_HIGH] = "▢",
                                                     [PROGRESS_TICK_SPACE] = " ",
                                                     [PROGRESS_EMPTY] = "⌿",
                                                     [PROGRESS_FULL] = "—",
                                                     [PROGRESS_LEFT_BRACKET] = "⦃",
                                                     [PROGRESS_RIGHT_BRACKET] = "⦄"};

static const char *PROGRESS_FANCY_ASCII_TABLE[] = {[PROGRESS_ARROW] = ">",
                                                   [PROGRESS_TICK_LOW] = "o",
                                                   [PROGRESS_TICK_HIGH] = "O",
                                                   [PROGRESS_TICK_SPACE] = " ",
                                                   [PROGRESS_EMPTY] = "/",
                                                   [PROGRESS_FULL] = "_",
                                                   [PROGRESS_LEFT_BRACKET] = "{",
                                                   [PROGRESS_RIGHT_BRACKET] = "}"};

static const char *PROGRESS_PLAIN_UNICODE_TABLE[] = {[PROGRESS_ARROW] = "▒",
                                                     [PROGRESS_TICK_LOW] = "░",
                                                     [PROGRESS_TICK_HIGH] = "▒",
                                                     [PROGRESS_TICK_SPACE] = "░",
                                                     [PROGRESS_EMPTY] = "░",
                                                     [PROGRESS_FULL] = "▓",
                                                     [PROGRESS_LEFT_BRACKET] = "▕",
                                                     [PROGRESS_RIGHT_BRACKET] = "▏"};

static const char *PROGRESS_PLAIN_ASCII_TABLE[] = {[PROGRESS_ARROW] = "_",
                                                   [PROGRESS_TICK_LOW] = " ",
                                                   [PROGRESS_TICK_HIGH] = "_",
                                                   [PROGRESS_TICK_SPACE] = " ",
                                                   [PROGRESS_EMPTY] = "\\",
                                                   [PROGRESS_FULL] = "/",
                                                   [PROGRESS_LEFT_BRACKET] = "|",
                                                   [PROGRESS_RIGHT_BRACKET] = "|"};

static const char *rm_fmt_progressbar_get_glyph(RmFmtHandlerProgress *self,
                                                RmProgressBarGlyph type) {
    if(self->plain && self->use_unicode_glyphs) {
        return PROGRESS_PLAIN_UNICODE_TABLE[type];
    } else if(self->plain) {
        return PROGRESS_PLAIN_ASCII_TABLE[type];
    } else if(self->use_unicode_glyphs) {
        return PROGRESS_FANCY_UNICODE_TABLE[type];
    } else {
        return PROGRESS_FANCY_ASCII_TABLE[type];
    }
}

static void rm_fmt_progressbar_print_glyph(FILE *out, RmSession *session,
                                           RmFmtHandlerProgress *self,
                                           RmProgressBarGlyph type, const char *color) {
    fprintf(out, "%s%s%s", MAYBE_COLOR(out, session, color),
            rm_fmt_progressbar_get_glyph(self, type), MAYBE_COLOR(out, session, RESET));
}

static void rm_fmt_progress_print_bar(RmSession *session, RmFmtHandlerProgress *self,
                                      int width, FILE *out) {
    int cells = width * self->percent;

    /* true when we do not know when 100% is reached.
     * Show a moving something in this case.
     * */

    rm_fmt_progressbar_print_glyph(out, session, self, PROGRESS_LEFT_BRACKET, RED);

    bool is_unknown = self->percent > 1.1;
    if(g_timer_elapsed(self->timer, NULL) * 1000.0 >= self->update_interval) {
        self->stripe_offset++;
    }

    for(int i = 0; i < width - 2; ++i) {
        if(i < cells) {
            if(is_unknown) {
                static const int glyphs[3] = {PROGRESS_TICK_LOW, PROGRESS_TICK_HIGH,
                                              PROGRESS_TICK_SPACE};
                static const char *colors[3] = {
                    BLUE, BLUE, GREEN,
                };

                int index = (i + self->stripe_offset) % 3;
                rm_fmt_progressbar_print_glyph(out, session, self, glyphs[index],
                                               colors[index]);
            } else {
                const char *color = (self->percent > 1.01) ? BLUE : GREEN;
                RmProgressBarGlyph glyph =
                    (self->percent > 1.01) ? PROGRESS_EMPTY : PROGRESS_FULL;
                rm_fmt_progressbar_print_glyph(out, session, self, glyph, color);
            }
        } else if(i == cells) {
            rm_fmt_progressbar_print_glyph(out, session, self, PROGRESS_ARROW, YELLOW);
        } else {
            rm_fmt_progressbar_print_glyph(out, session, self, PROGRESS_EMPTY, BLUE);
        }
    }

    rm_fmt_progressbar_print_glyph(out, session, self, PROGRESS_RIGHT_BRACKET, RED);
}

static void rm_fmt_prog(RmSession *session,
                        RmFmtHandler *parent,
                        FILE *out,
                        RmFmtProgressState state) {
    RmFmtHandlerProgress *self = (RmFmtHandlerProgress *)parent;

    bool force_draw = false;

    if(self->timer == NULL) {
        self->timer = g_timer_new();
        force_draw = true;
    }

    if(state == RM_PROGRESS_STATE_SUMMARY) {
        return;
    }

    if(session->cfg->replay) {
        /* Makes not much sense to print a progressbar with --replay */
        return;
    }

    if(state == RM_PROGRESS_STATE_INIT) {
        /* Do initializiation here */
        const char *update_interval_str =
            rm_fmt_get_config_value(session->formats, "progressbar", "update_interval");

        rm_running_mean_init(&self->read_diff_mean, 10);
        rm_running_mean_init(&self->eta_mean, 50);
        self->last_shred_bytes_remaining = 0;

        self->plain = true;
        if(rm_fmt_get_config_value(session->formats, "progressbar", "fancy") != NULL) {
            self->plain = false;
        }

        self->use_unicode_glyphs = true;
        if(rm_fmt_get_config_value(session->formats, "progressbar", "ascii") != NULL) {
            self->use_unicode_glyphs = false;
        }

        if(update_interval_str) {
            self->update_interval = g_ascii_strtoull(update_interval_str, NULL, 10);
        }

        if(self->update_interval == 0) {
            self->update_interval = 50; /* milliseconds */
        }

        fprintf(out, "\e[?25l"); /* Hide the cursor */
        fflush(out);
        return;
    }

    if(state == RM_PROGRESS_STATE_PRE_SHUTDOWN || rm_session_was_aborted()) {
        fprintf(out, "\e[?25h"); /* show the cursor */
        fflush(out);

        if(rm_session_was_aborted()) {
            return;
        }
    }

    if(self->last_state != state && self->last_state != RM_PROGRESS_STATE_INIT) {
        self->percent = 1.05;
        if(state != RM_PROGRESS_STATE_PRE_SHUTDOWN) {
            rm_fmt_progress_print_bar(session, self, self->terminal.ws_col * 0.3, out);
            fprintf(out, "\n");
        }
        g_timer_start(self->timer);
        force_draw = true;
    }

    /* Restart the time after these stages finished */
    if((state == RM_PROGRESS_STATE_TRAVERSE && session->traverse_finished) ||
       (state == RM_PROGRESS_STATE_SHREDDER && session->shredder_finished)) {
        g_timer_start(self->timer);
        force_draw = true;
    }

    /* Try to get terminal width, might fail on some terminals. */
    ioctl(fileno(out), TIOCGWINSZ, &self->terminal);
    self->last_state = state;

    // g_printerr(".\n");

    gdouble elapsed_sec = g_timer_elapsed(self->timer, NULL);
    if(force_draw || elapsed_sec * 1000.0 >= self->update_interval) {
        /* Max. 70% (-1 char) are allowed for the text */
        int text_width = MAX(self->terminal.ws_col * 0.7 - 1, 0);

        rm_fmt_progress_format_text(session, self, text_width, elapsed_sec, out);
        if(state == RM_PROGRESS_STATE_PRE_SHUTDOWN) {
            /* do not overwrite last messages */
            self->percent = 1.05;
            text_width = 0;
        }

        rm_fmt_progress_print_bar(session, self, self->terminal.ws_col * 0.3, out);
        rm_fmt_progress_print_text(self, text_width, out);

        fprintf(out, "%s\r", MAYBE_RESET(out, session));

        g_timer_start(self->timer);
    }

    if(state == RM_PROGRESS_STATE_PRE_SHUTDOWN) {
        fprintf(out, "\n\n");
        g_timer_destroy(self->timer);
        rm_running_mean_unref(&self->read_diff_mean);
        rm_running_mean_unref(&self->eta_mean);
    }
}

static RmFmtHandlerProgress PROGRESS_HANDLER_IMPL = {
    /* Initialize parent */
    .parent =
        {
            .size = sizeof(PROGRESS_HANDLER_IMPL),
            .name = "progressbar",
            .head = NULL,
            .elem = NULL,
            .prog = rm_fmt_prog,
            .foot = NULL,
            .valid_keys = {"update_interval", "ascii", "fancy", NULL},
        },

    /* Initialize own stuff */
    .percent = 0.0f,
    .text_len = 0,
    .text_buf = {0},
    .timer = NULL,
    .use_unicode_glyphs = true,
    .plain = true,
    .stripe_offset = 0,
    .last_state = RM_PROGRESS_STATE_INIT};

RmFmtHandler *PROGRESS_HANDLER = (RmFmtHandler *)&PROGRESS_HANDLER_IMPL;
