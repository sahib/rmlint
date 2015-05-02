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
 *  - Christopher <sahib> Pahl 2010-2014 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2014 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

#include "../formats.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <sys/ioctl.h>

typedef struct RmFmtHandlerProgress {
    /* must be first */
    RmFmtHandler parent;

    /* user data */
    gdouble percent;
    gdouble last_unknown_pos;
    RmOff total_lint_bytes;

    char text_buf[1024];
    guint32 text_len;
    guint32 update_counter;
    guint32 update_interval;
    guint8 use_unicode_glyphs;

    bool plain;

    RmFmtProgressState last_state;
    struct winsize terminal;
} RmFmtHandlerProgress;

static void rm_fmt_progress_format_preprocess(RmSession *session, char *buf, size_t buf_len, FILE *out) {
    if(session->offsets_read > 0) {
        // TODO: Translate.
        g_snprintf(
            buf, buf_len, "fiemap: %s+%" LLU "%s %s-%" LLU "%s %s#%" LLU "%s",
            MAYBE_GREEN(out, session), session->offsets_read, MAYBE_RESET(out, session),
            MAYBE_RED(out, session), session->offset_fails, MAYBE_RESET(out, session),
            MAYBE_BLUE(out, session), session->total_filtered_files, MAYBE_RESET(out, session)
        );
    } else {
        g_snprintf(
            buf, buf_len, "%s %s%" LLU "%s",
            _("reduces files to"), MAYBE_GREEN(out, session),
            session->total_filtered_files, MAYBE_RESET(out, session)
        );
    }
}

static void rm_fmt_progress_format_text(RmSession *session, RmFmtHandlerProgress *self,
                                        int max_len, FILE *out) {
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
        self->text_len =
            g_snprintf(self->text_buf, sizeof(self->text_buf),
                       "%s (%s / %s %s%" LLU "%s %s)", _("Preprocessing"),
                       preproc_buf,
                       _("found"), MAYBE_RED(out, session), session->other_lint_cnt,
                       MAYBE_RESET(out, session), _("other lint"));
        break;
    case RM_PROGRESS_STATE_SHREDDER:
        self->percent = 1.0 - ((gdouble)session->shred_bytes_remaining /
                               (gdouble)session->shred_bytes_after_preprocess);
        rm_util_size_to_human_readable(session->shred_bytes_remaining, num_buf,
                                       sizeof(num_buf));
        self->text_len = g_snprintf(
            self->text_buf, sizeof(self->text_buf),
            "%s (%s%" LLU "%s %s %s%" LLU "%s %s; %s%s%s %s %s%" LLU "%s %s)",
            _("Matching"), MAYBE_RED(out, session), session->dup_counter,
            MAYBE_RESET(out, session), _("dupes of"), MAYBE_YELLOW(out, session),
            session->dup_group_counter, MAYBE_RESET(out, session), _("originals"),
            MAYBE_GREEN(out, session), num_buf, MAYBE_RESET(out, session),
            _("to scan in"), MAYBE_GREEN(out, session), session->shred_files_remaining,
            MAYBE_RESET(out, session), _("files"));
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

    /* Get rid of colors */
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
    bool is_unknown = self->percent > 1.1;

    rm_fmt_progressbar_print_glyph(out, session, self, PROGRESS_LEFT_BRACKET, RED);

    for(int i = 0; i < width - 2; ++i) {
        if(i < cells) {
            if(is_unknown) {
                if((int)self->last_unknown_pos % 4 == i % 4) {
                    rm_fmt_progressbar_print_glyph(out, session, self, PROGRESS_TICK_LOW,
                                                   BLUE);
                } else if((int)self->last_unknown_pos % 2 == i % 2) {
                    rm_fmt_progressbar_print_glyph(out, session, self, PROGRESS_TICK_HIGH,
                                                   YELLOW);
                } else {
                    rm_fmt_progressbar_print_glyph(out, session, self,
                                                   PROGRESS_TICK_SPACE, GREEN);
                }
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

    self->last_unknown_pos = fmod(self->last_unknown_pos + 0.005, width - 2);
}

static void rm_fmt_prog(RmSession *session,
                        RmFmtHandler *parent,
                        FILE *out,
                        RmFmtProgressState state) {
    RmFmtHandlerProgress *self = (RmFmtHandlerProgress *)parent;
    if(state == RM_PROGRESS_STATE_SUMMARY) {
        return;
    }

    if(state == RM_PROGRESS_STATE_INIT) {
        /* Do initializiation here */
        const char *update_interval_str =
            rm_fmt_get_config_value(session->formats, "progressbar", "update_interval");

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
            self->update_interval = 50;
        }

        self->last_unknown_pos = 0;
        self->total_lint_bytes = 1;

        fprintf(out, "\e[?25l"); /* Hide the cursor */
        fflush(out);
        return;
    }

    if(state == RM_PROGRESS_STATE_PRE_SHUTDOWN || rm_session_was_aborted(session)) {
        fprintf(out, "\e[?25h"); /* show the cursor */
        fflush(out);

        if(rm_session_was_aborted(session)) {
            return;
        }
    }

    if(state == RM_PROGRESS_STATE_SHREDDER) {
        self->total_lint_bytes =
            MAX(self->total_lint_bytes, session->shred_bytes_remaining);
    }

    if(self->last_state != state && self->last_state != RM_PROGRESS_STATE_INIT) {
        self->percent = 1.05;
        if(state != RM_PROGRESS_STATE_PRE_SHUTDOWN) {
            rm_fmt_progress_print_bar(session, self, self->terminal.ws_col * 0.3, out);
            fprintf(out, "\n");
        }
        self->update_counter = 0;
    }

    if(state == RM_PROGRESS_STATE_TRAVERSE && session->traverse_finished) {
        self->update_counter = 0;
    }

    if(state == RM_PROGRESS_STATE_SHREDDER && session->shredder_finished) {
        self->update_counter = 0;
    }

    if(ioctl(fileno(out), TIOCGWINSZ, &self->terminal) != 0) {
        rm_log_warning_line(_("Cannot figure out terminal width."));
    }

    self->last_state = state;

    if(self->update_counter++ % self->update_interval == 0) {
        int text_width = MAX(self->terminal.ws_col * 0.7 - 1, 0);
        rm_fmt_progress_format_text(session, self, text_width, out);
        if(state == RM_PROGRESS_STATE_PRE_SHUTDOWN) {
            /* do not overwrite last messages */
            self->percent = 1.05;
            text_width = 0;
        }

        rm_fmt_progress_print_bar(session, self, self->terminal.ws_col * 0.3, out);
        rm_fmt_progress_print_text(self, text_width, out);
        fprintf(out, "%s\r", MAYBE_RESET(out, session));
    }

    if(state == RM_PROGRESS_STATE_PRE_SHUTDOWN) {
        fprintf(out, "\n\n");
    }
}

static RmFmtHandlerProgress PROGRESS_HANDLER_IMPL = {
    /* Initialize parent */
    .parent = {
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
    .update_counter = 0,
    .use_unicode_glyphs = true,
    .plain = true,
    .last_state = RM_PROGRESS_STATE_INIT};

RmFmtHandler *PROGRESS_HANDLER = (RmFmtHandler *)&PROGRESS_HANDLER_IMPL;
