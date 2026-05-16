#include "app_notepad.h"
#include "sv_desktop_internal.h"
#include "sv_gfx.h"
#include "string.h"
#include "fs.h"
#include "keyboard.h"
#include "app_control_panel.h"
#include "timer.h"

extern int ui_show_scrollbar_grid;

static const char icon[24][25] = {
    "........................",
    "...BBBBBBBBBBBBB........",
    "...BWWWWWWWWWWWBB.......",
    "...BWWWWWWWWWWWBWB......",
    "...BWWWBWBBBWBBBWWB.....",
    "...BWWWWWWWWWWWBWWWB....",
    "...BWWBBBWBWBBWBBBBBB...",
    "...BWWWWWWWWWWWWWWWWB...",
    "...BWWBBWBBBBWBWBBWWB...",
    "...BWWWWWWWWWWWWWWWWB...",
    "...BWWBBBBWBWBBBWWWWB...",
    "...BWWWWWWWWWWWWWWWWB...",
    "...BWWWBBWBWBBBBWBWWB...",
    "...BWWWWWWWWWWWWWWWWB...",
    "...BWWBBBWBBBWBBWWWWB...",
    "...BWWWWWWWWWWWWWWWWB...",
    "...BWWBWBBBBWBBBBBWWB...",
    "...BWWWWWWWWWWWWWWWWB...",
    "...BWWBBBWBBWBBBWBWWB...",
    "...BWWWWWWWWWWWWWWWWB...",
    "...BWWWWWWWWWWWWWWWWB...",
    "...BBBBBBBBBBBBBBBBBB...",
    "........................",
    "........................"
};

#define NOTEPAD_BUF_SIZE 4096
#define NOTEPAD_LINE_LEN 80
#define NOTEPAD_UNDO_SIZE 1024
#define NOTEPAD_CLIPBOARD_SIZE 1024
char notepad_buf[MAX_WIN][NOTEPAD_BUF_SIZE];
int  notepad_buf_len[MAX_WIN];
int  notepad_cursor_pos[MAX_WIN];
static int  notepad_cursor_x[MAX_WIN];
static int  notepad_cursor_y[MAX_WIN];
int  notepad_scroll_y[MAX_WIN];
int  notepad_total_lines[MAX_WIN];
int notepad_scrollbar_dragging = -1;
int notepad_scrollbar_drag_start_y = 0;
int notepad_scrollbar_drag_start_scroll = 0;
int notepad_manual_scroll_timer = 0;

char notepad_undo_buf[MAX_WIN][NOTEPAD_UNDO_SIZE];
int notepad_undo_len[MAX_WIN];
static int notepad_undo_cursor[MAX_WIN];
static int notepad_has_undo[MAX_WIN];
static int notepad_has_redo[MAX_WIN];

char notepad_clipboard[NOTEPAD_CLIPBOARD_SIZE];
int notepad_clipboard_len = 0;

int notepad_has_file[MAX_WIN];
static u32 notepad_file_dir_cluster[MAX_WIN];
static char notepad_file_name[MAX_WIN][FS_MAX_NAME];

int notepad_find_has_pattern[MAX_WIN];
char notepad_find_pattern[MAX_WIN][64];
int notepad_find_from_pos[MAX_WIN];

int notepad_sel_start[MAX_WIN];
int notepad_sel_end[MAX_WIN];
static int notepad_has_selection[MAX_WIN];
int notepad_sel_dragging = -1;
static int notepad_sel_drag_start_pos[MAX_WIN];

static char notepad_bar_msg[MAX_WIN][44];
static int  notepad_zebra[MAX_WIN];

static const char *notepad_ctx_labels[] = { "Cut", "Copy", "Paste", "Clear", "Select All", "Redo" };
#define NOTEPAD_CTX_ITEMS 6

static int notepad_ctx_menu_open = 0;
int notepad_ctx_menu_x = 0;
int notepad_ctx_menu_y = 0;
int notepad_ctx_menu_hover = -1;

void notepad_draw_context_menu(void) {
    int i, w = 70, h;
    if (!notepad_ctx_menu_open) return;
    h = NOTEPAD_CTX_ITEMS * 11 + 6;

    if (notepad_ctx_menu_x + w > VGA13_WIDTH) notepad_ctx_menu_x = VGA13_WIDTH - w;
    if (notepad_ctx_menu_y + h > VGA13_HEIGHT) notepad_ctx_menu_y = VGA13_HEIGHT - h;

    vga13_fill_rect(notepad_ctx_menu_x + 1, notepad_ctx_menu_y + h, w, 1, PAL_DARK_GRAY);
    vga13_fill_rect(notepad_ctx_menu_x + w, notepad_ctx_menu_y + 1, 1, h, PAL_DARK_GRAY);
    vga13_fill_rect(notepad_ctx_menu_x, notepad_ctx_menu_y, w, h, VGA13_WHITE);

    hline_px(notepad_ctx_menu_x, notepad_ctx_menu_x + w - 1, notepad_ctx_menu_y, VGA13_BLACK);
    hline_px(notepad_ctx_menu_x, notepad_ctx_menu_x + w - 1, notepad_ctx_menu_y + h - 1, VGA13_BLACK);
    vline_px(notepad_ctx_menu_x, notepad_ctx_menu_y, notepad_ctx_menu_y + h - 1, VGA13_BLACK);
    vline_px(notepad_ctx_menu_x + w - 1, notepad_ctx_menu_y, notepad_ctx_menu_y + h - 1, VGA13_BLACK);

    for (i = 0; i < NOTEPAD_CTX_ITEMS; i++) {
        int iy = notepad_ctx_menu_y + 2 + i * 11;
        int inv = (i == notepad_ctx_menu_hover);
        if (inv) vga13_fill_rect(notepad_ctx_menu_x + 2, iy - 1, w - 4, 10, VGA13_BLACK);
        vga13_draw_string(notepad_ctx_menu_x + 8, iy, notepad_ctx_labels[i], VGA13_BLACK, VGA13_WHITE, inv);
    }
}

static int notepad_mouse_to_pos(int id, int mx, int my, int cx, int cy, int cw) {
    int char_width = 6;
    int scrollbar_w = 11;
    int content_w = cw - scrollbar_w - 2;
    int chars_per_line = (content_w - 4) / char_width;
    int line = (my - cy - 4) / 8 + notepad_scroll_y[id];
    int col = (mx - cx - 4) / char_width;

    if (col < 0) col = 0;
    if (col > chars_per_line) col = chars_per_line;
    if (line < 0) line = 0;

    int buf_pos = 0;
    int current_line = 0;
    int col_in_line = 0;

    while (buf_pos < notepad_buf_len[id]) {
        if (notepad_buf[id][buf_pos] == '\n') {
            if (current_line == line) {

                return buf_pos;
            }
            current_line++;
            col_in_line = 0;
        } else {
            if (current_line == line && col_in_line == col) {
                return buf_pos;
            }
            col_in_line++;
            if (col_in_line >= chars_per_line) {
                if (current_line == line) {
                    return buf_pos + 1;
                }
                current_line++;
                col_in_line = 0;
            }
        }
        buf_pos++;
    }

    return notepad_buf_len[id];
}

void handle_notepad_click(int id, int mx, int my, int cx, int cy, int cw, int button) {
    int pos;

    if (id < 0 || id >= MAX_WIN) return;

    if (notepad_ctx_menu_open) {
        notepad_ctx_menu_open = 0;
        desktop_needs_full_blit = 1;
        if (button == 2) return;
    }

    if (button == 0) {
        pos = notepad_mouse_to_pos(id, mx, my, cx, cy, cw);

        if (notepad_sel_dragging < 0) {

            notepad_sel_dragging = id;
            notepad_sel_drag_start_pos[id] = pos;
            notepad_sel_start[id] = pos;
            notepad_sel_end[id] = pos;
            notepad_has_selection[id] = (pos < notepad_buf_len[id]);
            notepad_cursor_pos[id] = pos;
            desktop_needs_full_blit = 1;
        }
    } else if (button == 2) {
        notepad_ctx_menu_x = mx;
        notepad_ctx_menu_y = my;
        notepad_ctx_menu_open = 1;
        notepad_ctx_menu_hover = -1;
        desktop_needs_full_blit = 1;
    }
}

void handle_notepad_drag(int id, int mx, int my, int cx, int cy, int cw) {
    int pos;
    int start_pos;

    if (id < 0 || id >= MAX_WIN) return;
    if (notepad_sel_dragging != id) return;

    pos = notepad_mouse_to_pos(id, mx, my, cx, cy, cw);
    start_pos = notepad_sel_drag_start_pos[id];

    if (pos < start_pos) {
        notepad_sel_start[id] = pos;
        notepad_sel_end[id] = start_pos;
    } else {
        notepad_sel_start[id] = start_pos;
        notepad_sel_end[id] = pos;
    }

    notepad_has_selection[id] = (notepad_sel_start[id] < notepad_sel_end[id]);
    notepad_cursor_pos[id] = pos;
    desktop_needs_full_blit = 1;
}

void notepad_end_drag(void) {
    notepad_sel_dragging = -1;
}

static void notepad_save_undo(int id) {
    int i;
    if (id < 0 || id >= MAX_WIN) return;

    for (i = 0; i < notepad_buf_len[id] && i < NOTEPAD_UNDO_SIZE - 1; i++) {
        notepad_undo_buf[id][i] = notepad_buf[id][i];
    }
    notepad_undo_len[id] = notepad_buf_len[id];
    notepad_undo_cursor[id] = notepad_cursor_pos[id];
    notepad_has_undo[id] = 1;
}

void notepad_undo(int id) {
    int i;
    char temp_buf[NOTEPAD_BUF_SIZE];
    int temp_len, temp_cursor;
    if (id < 0 || id >= MAX_WIN || !notepad_has_undo[id]) return;

    for (i = 0; i < notepad_buf_len[id] && i < NOTEPAD_BUF_SIZE; i++) {
        temp_buf[i] = notepad_buf[id][i];
    }
    temp_len = notepad_buf_len[id];
    temp_cursor = notepad_cursor_pos[id];

    for (i = 0; i < notepad_undo_len[id] && i < NOTEPAD_BUF_SIZE; i++) {
        notepad_buf[id][i] = notepad_undo_buf[id][i];
    }
    notepad_buf_len[id] = notepad_undo_len[id];
    notepad_cursor_pos[id] = notepad_undo_cursor[id];

    for (i = 0; i < temp_len && i < NOTEPAD_UNDO_SIZE - 1; i++) {
        notepad_undo_buf[id][i] = temp_buf[i];
    }
    notepad_undo_len[id] = temp_len;
    notepad_undo_cursor[id] = temp_cursor;
    notepad_has_redo[id] = 1;
    notepad_has_undo[id] = 0;
    notepad_has_selection[id] = 0;
    desktop_needs_full_blit = 1;
}

void notepad_redo(int id) {
    int i;
    char temp_buf[NOTEPAD_BUF_SIZE];
    int temp_len, temp_cursor;
    if (id < 0 || id >= MAX_WIN || !notepad_has_redo[id]) return;

    for (i = 0; i < notepad_buf_len[id] && i < NOTEPAD_BUF_SIZE; i++) {
        temp_buf[i] = notepad_buf[id][i];
    }
    temp_len = notepad_buf_len[id];
    temp_cursor = notepad_cursor_pos[id];

    for (i = 0; i < notepad_undo_len[id] && i < NOTEPAD_BUF_SIZE; i++) {
        notepad_buf[id][i] = notepad_undo_buf[id][i];
    }
    notepad_buf_len[id] = notepad_undo_len[id];
    notepad_cursor_pos[id] = notepad_undo_cursor[id];

    for (i = 0; i < temp_len && i < NOTEPAD_UNDO_SIZE - 1; i++) {
        notepad_undo_buf[id][i] = temp_buf[i];
    }
    notepad_undo_len[id] = temp_len;
    notepad_undo_cursor[id] = temp_cursor;
    notepad_has_undo[id] = 1;
    notepad_has_redo[id] = 0;
    notepad_has_selection[id] = 0;
    desktop_needs_full_blit = 1;
}

void notepad_select_all(int id) {
    if (id < 0 || id >= MAX_WIN) return;
    notepad_sel_start[id] = 0;
    notepad_sel_end[id] = notepad_buf_len[id];
    notepad_has_selection[id] = 1;
    desktop_needs_full_blit = 1;
}

void notepad_copy(int id) {
    int i, sel_start, sel_end;
    if (id < 0 || id >= MAX_WIN) return;

    if (notepad_has_selection[id]) {
        sel_start = notepad_sel_start[id];
        sel_end = notepad_sel_end[id];
    } else {
        sel_start = 0;
        sel_end = notepad_buf_len[id];
    }

    notepad_clipboard_len = 0;
    for (i = sel_start; i < sel_end && i < notepad_buf_len[id] && notepad_clipboard_len < NOTEPAD_CLIPBOARD_SIZE - 1; i++) {
        notepad_clipboard[notepad_clipboard_len++] = notepad_buf[id][i];
    }
    notepad_clipboard[notepad_clipboard_len] = 0;
}

void notepad_paste(int id) {
    int i;
    if (id < 0 || id >= MAX_WIN || notepad_clipboard_len <= 0) return;

    notepad_save_undo(id);

    if (notepad_cursor_pos[id] > notepad_buf_len[id]) notepad_cursor_pos[id] = notepad_buf_len[id];

    if (notepad_buf_len[id] + notepad_clipboard_len >= NOTEPAD_BUF_SIZE) {

        int max_paste = NOTEPAD_BUF_SIZE - notepad_buf_len[id] - 1;
        if (max_paste < 0) max_paste = 0;
        notepad_clipboard_len = max_paste;
    }

    for (i = notepad_buf_len[id] + notepad_clipboard_len; i >= notepad_cursor_pos[id] + notepad_clipboard_len; i--) {
        notepad_buf[id][i] = notepad_buf[id][i - notepad_clipboard_len];
    }

    for (i = 0; i < notepad_clipboard_len; i++) {
        notepad_buf[id][notepad_cursor_pos[id] + i] = notepad_clipboard[i];
    }
    notepad_buf_len[id] += notepad_clipboard_len;
    notepad_buf[id][notepad_buf_len[id]] = 0;
    notepad_cursor_pos[id] += notepad_clipboard_len;
    notepad_has_selection[id] = 0;
    desktop_needs_full_blit = 1;
}

void notepad_clear(int id) {
    if (id < 0 || id >= MAX_WIN) return;
    notepad_save_undo(id);
    notepad_buf_len[id] = 0;
    notepad_buf[id][0] = 0;
    notepad_cursor_pos[id] = 0;
    notepad_has_selection[id] = 0;
    desktop_needs_full_blit = 1;
}

void notepad_cut(int id) {
    notepad_copy(id);
    notepad_clear(id);
}

void notepad_set_file_association(int id, u32 dir_cluster, const char *name) {
    if (id < 0 || id >= MAX_WIN) return;
    if (!name || !name[0]) {
        notepad_has_file[id] = 0;
        return;
    }
    notepad_has_file[id] = 1;
    notepad_file_dir_cluster[id] = dir_cluster;
    str_cpy(notepad_file_name[id], name, FS_MAX_NAME);
}

void notepad_write_buffer_to_file(int id, const char *name83) {
    if (id < 0 || id >= MAX_WIN) return;
    if (!name83 || !name83[0]) return;

    int fd = fs_open(name83);
    if (fd < 0) {
        fd = fs_create(name83);
    }
    if (fd < 0) return;

    (void)fs_write(fd, notepad_buf[id], (u32)notepad_buf_len[id]);
}

void notepad_save(int id) {
    if (id < 0 || id >= MAX_WIN || wins[id].app != APP_notepad) return;
    if (!notepad_has_file[id]) {

        const char *title = wins[id].title;
        if (title && title[0] && kstrcmp(title, "Notepad") != 0) notepad_set_file_association(id, fs_root_dir_cluster(), title);
        else notepad_set_file_association(id, fs_root_dir_cluster(), "NOTES.TXT");
    }
    notepad_write_buffer_to_file(id, notepad_file_name[id]);
}

void notepad_save_as(int id) {
    if (id < 0 || id >= MAX_WIN || wins[id].app != APP_notepad) return;
    const char *title = wins[id].title;
    if (!title || !title[0] || kstrcmp(title, "Notepad") == 0) {
        title = "NOTES.TXT";
    }
    notepad_set_file_association(id, notepad_has_file[id] ? notepad_file_dir_cluster[id] : fs_root_dir_cluster(), title);
    notepad_write_buffer_to_file(id, notepad_file_name[id]);
}

static int notepad_is_word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c == '_');
}

static void notepad_extract_token(int id, char *out, int out_cap) {
    if (!out || out_cap <= 1 || id < 0 || id >= MAX_WIN) return;
    out[0] = 0;
    if (notepad_buf_len[id] <= 0) return;

    int pos = notepad_cursor_pos[id];
    if (pos < 0) pos = 0;
    if (pos > notepad_buf_len[id]) pos = notepad_buf_len[id];

    int idx = (pos > 0) ? (pos - 1) : 0;
    if (idx < 0 || idx >= notepad_buf_len[id]) return;
    if (!notepad_is_word_char(notepad_buf[id][idx])) return;

    int start = idx;
    while (start > 0 && notepad_is_word_char(notepad_buf[id][start - 1])) start--;
    int end = idx + 1;
    while (end < notepad_buf_len[id] && notepad_is_word_char(notepad_buf[id][end])) end++;

    int len = end - start;
    if (len >= out_cap) len = out_cap - 1;
    for (int i = 0; i < len; i++) out[i] = notepad_buf[id][start + i];
    out[len] = 0;
}

static int notepad_find_pattern_idx(int id, const char *pattern, int start_pos, int wrap) {
    int pat_len = str_len(pattern);
    if (pat_len <= 0) return -1;
    int max_start = notepad_buf_len[id] - pat_len;
    if (max_start < 0) return -1;

    for (int i = start_pos; i <= max_start; i++) {
        int j;
        for (j = 0; j < pat_len; j++) {
            if (notepad_buf[id][i + j] != pattern[j]) break;
        }
        if (j == pat_len) return i;
    }

    if (wrap) {
        for (int i = 0; i < start_pos; i++) {
            if (i > max_start) break;
            int j;
            for (j = 0; j < pat_len; j++) {
                if (notepad_buf[id][i + j] != pattern[j]) break;
            }
            if (j == pat_len) return i;
        }
    }
    return -1;
}

void notepad_find_next(int id, int reset_pattern) {
    if (id < 0 || id >= MAX_WIN || wins[id].app != APP_notepad) return;

    char token[64];
    if (reset_pattern || !notepad_find_has_pattern[id]) {
        notepad_extract_token(id, token, sizeof(token));
        if (!token[0]) return;
        notepad_find_has_pattern[id] = 1;
        str_cpy(notepad_find_pattern[id], token, 64);
        notepad_find_from_pos[id] = notepad_cursor_pos[id];
    }

    int start = notepad_find_from_pos[id];
    if (start < 0) start = 0;
    if (start > notepad_buf_len[id]) start = notepad_buf_len[id];

    int match = notepad_find_pattern_idx(id, notepad_find_pattern[id], start, 1);
    if (match < 0) return;

    int pat_len = str_len(notepad_find_pattern[id]);
    notepad_sel_start[id] = match;
    notepad_sel_end[id] = match + pat_len;
    notepad_has_selection[id] = 1;
    notepad_cursor_pos[id] = notepad_sel_end[id];
    notepad_find_from_pos[id] = notepad_sel_end[id];
    desktop_needs_full_blit = 1;
}

int notepad_hit_context_menu(int mx, int my) {
    int w = 70, h;
    if (!notepad_ctx_menu_open) return -1;
    h = NOTEPAD_CTX_ITEMS * 11 + 6;
    if (mx < notepad_ctx_menu_x || mx >= notepad_ctx_menu_x + w ||
        my < notepad_ctx_menu_y || my >= notepad_ctx_menu_y + h) return -1;
    return (my - notepad_ctx_menu_y - 3) / 11;
}

void notepad_context_menu_click(int mx, int my, int id) {
    int item = notepad_hit_context_menu(mx, my);
    if (item >= 0 && item < NOTEPAD_CTX_ITEMS && id >= 0 && id < MAX_WIN) {
        if (item == 0) {
            notepad_cut(id);
        } else if (item == 1) {
            notepad_copy(id);
        } else if (item == 2) {
            notepad_paste(id);
        } else if (item == 3) {
            notepad_clear(id);
        } else if (item == 4) {
            notepad_select_all(id);
        } else if (item == 5) {
            notepad_redo(id);
        }
    }
    notepad_ctx_menu_open = 0;
    notepad_ctx_menu_hover = -1;
    desktop_needs_full_blit = 1;
}

void notepad_ctx_open(int x, int y) {
    notepad_ctx_menu_x = x; notepad_ctx_menu_y = y;
    notepad_ctx_menu_open = 1; notepad_ctx_menu_hover = -1;
}
void notepad_ctx_close(void) { notepad_ctx_menu_open = 0; notepad_ctx_menu_hover = -1; }
int  notepad_ctx_is_open(void) { return notepad_ctx_menu_open; }
void notepad_ctx_hover_update(int mx, int my) { notepad_ctx_menu_hover = notepad_hit_context_menu(mx, my); }

void app_notepad_draw_client(int id, int cx, int cy, int cw, int ch) {
    int y;
    int scrollbar_w = 11;
    int content_w = cw - scrollbar_w - 2;
    int content_h = ch - 4;
    int visible_lines = content_h / 8;
    int char_width = 6;
    int chars_per_line = (content_w - 4) / char_width;
    int scroll_x = cx + cw - scrollbar_w - 1;
    int scroll_y = cy + 2;
    int scroll_h = content_h;
    int thumb_h, thumb_y;
    int buf_pos = 0;
    int current_line = 0;

    if (id < 0 || id >= MAX_WIN) return;

    vga13_fill_rect(cx, cy, cw - scrollbar_w - 1, ch, VGA13_WHITE);

    y = cy + 4;
    buf_pos = 0;
    current_line = 0;
    while (buf_pos < notepad_buf_len[id] && y + 7 < cy + ch - 2) {
        int line_len = 0;
        char line_buf[128];
        int line_start_pos = buf_pos;

        while (buf_pos < notepad_buf_len[id] &&
               notepad_buf[id][buf_pos] != '\n' &&
               line_len < chars_per_line &&
               line_len < 127) {
            line_buf[line_len++] = notepad_buf[id][buf_pos++];
        }
        line_buf[line_len] = 0;

        if (buf_pos < notepad_buf_len[id] && notepad_buf[id][buf_pos] == '\n') {
            buf_pos++;
        }

        if (current_line >= notepad_scroll_y[id]) {
            if (notepad_zebra[id] && (current_line & 1)) {
                vga13_fill_rect(cx + 2, y - 1, content_w - 4, 9, PAL_LIGHT_GRAY);
            }
            int sel_start = notepad_sel_start[id];
            int sel_end = notepad_sel_end[id];
            int line_end_pos = line_start_pos + line_len;

            if (notepad_has_selection[id] && sel_start < line_end_pos && sel_end > line_start_pos) {

                int sel_in_line_start = (sel_start > line_start_pos) ? sel_start - line_start_pos : 0;
                int sel_in_line_end = (sel_end < line_end_pos) ? sel_end - line_start_pos : line_len;

                if (sel_in_line_start > 0) {
                    char before_sel[128];
                    int i;
                    for (i = 0; i < sel_in_line_start && i < line_len; i++) {
                        before_sel[i] = line_buf[i];
                    }
                    before_sel[i] = 0;
                    vga13_draw_string(cx + 4, y, before_sel, VGA13_BLACK, VGA13_WHITE, 0);
                }

                if (sel_in_line_end > sel_in_line_start) {
                    char selected[128];
                    int i, j = 0;
                    for (i = sel_in_line_start; i < sel_in_line_end && i < line_len; i++) {
                        selected[j++] = line_buf[i];
                    }
                    selected[j] = 0;

                    vga13_fill_rect(cx + 4 + sel_in_line_start * char_width, y - 1,
                                   j * char_width, 9, VGA13_BLACK);
                    vga13_draw_string(cx + 4 + sel_in_line_start * char_width, y,
                                     selected, VGA13_WHITE, VGA13_BLACK, 0);
                }

                if (sel_in_line_end < line_len) {
                    char after_sel[128];
                    int i, j = 0;
                    for (i = sel_in_line_end; i < line_len; i++) {
                        after_sel[j++] = line_buf[i];
                    }
                    after_sel[j] = 0;
                    vga13_draw_string(cx + 4 + sel_in_line_end * char_width, y,
                                     after_sel, VGA13_BLACK, VGA13_WHITE, 0);
                }
            } else {

                vga13_draw_string(cx + 4, y, line_buf, VGA13_BLACK, VGA13_WHITE, 0);
            }
            y += 8;
        }
        current_line++;
    }

    if (notepad_bar_msg[id][0]) {
        int bw = content_w - 4;
        if (bw < 20) bw = 20;
        vga13_fill_rect(cx + 2, cy + ch - 11, bw, 9, PAL_LIGHT_GRAY);
        vga13_draw_string(cx + 4, cy + ch - 10, notepad_bar_msg[id], VGA13_BLACK, PAL_LIGHT_GRAY, 0);
    }

    vga13_fill_rect(scroll_x, scroll_y, scrollbar_w, scroll_h, PAL_LIGHT_GRAY);

    if (ui_show_scrollbar_grid) {

        for (int gx = scroll_x + 2; gx <= scroll_x + scrollbar_w - 3; gx += 2) {
            vline_px(gx, scroll_y + 2, scroll_y + scroll_h - 3, PAL_DARK_GRAY);
        }
        for (int gy = scroll_y + 2; gy <= scroll_y + scroll_h - 3; gy += 4) {
            hline_px(scroll_x + 2, scroll_x + scrollbar_w - 3, gy, PAL_DARK_GRAY);
        }
    }

    hline_px(scroll_x, scroll_x + scrollbar_w - 1, scroll_y, VGA13_BLACK);
    hline_px(scroll_x, scroll_x + scrollbar_w - 1, scroll_y + scroll_h - 1, VGA13_BLACK);
    vline_px(scroll_x, scroll_y, scroll_y + scroll_h - 1, VGA13_BLACK);
    vline_px(scroll_x + scrollbar_w - 1, scroll_y, scroll_y + scroll_h - 1, VGA13_BLACK);

    vga13_fill_rect(scroll_x + 1, scroll_y + 1, scrollbar_w - 2, 10, VGA13_WHITE);
    hline_px(scroll_x + 1, scroll_x + scrollbar_w - 2, scroll_y + 1, VGA13_BLACK);
    hline_px(scroll_x + 1, scroll_x + scrollbar_w - 2, scroll_y + 10, VGA13_BLACK);
    vline_px(scroll_x + 1, scroll_y + 1, scroll_y + 10, VGA13_BLACK);
    vline_px(scroll_x + scrollbar_w - 2, scroll_y + 1, scroll_y + 10, VGA13_BLACK);
    vga13_draw_string(scroll_x + 3, scroll_y + 2, "\x1E", VGA13_BLACK, VGA13_WHITE, 0);

    vga13_fill_rect(scroll_x + 1, scroll_y + scroll_h - 11, scrollbar_w - 2, 10, VGA13_WHITE);
    hline_px(scroll_x + 1, scroll_x + scrollbar_w - 2, scroll_y + scroll_h - 11, VGA13_BLACK);
    hline_px(scroll_x + 1, scroll_x + scrollbar_w - 2, scroll_y + scroll_h - 1, VGA13_BLACK);
    vline_px(scroll_x + 1, scroll_y + scroll_h - 11, scroll_y + scroll_h - 1, VGA13_BLACK);
    vline_px(scroll_x + scrollbar_w - 2, scroll_y + scroll_h - 11, scroll_y + scroll_h - 1, VGA13_BLACK);
    vga13_draw_string(scroll_x + 3, scroll_y + scroll_h - 10, "\x1F", VGA13_BLACK, VGA13_WHITE, 0);

    {
        int total_lines = 1;
        int p;
        int line_len = 0;
        for (p = 0; p < notepad_buf_len[id]; p++) {
            if (notepad_buf[id][p] == '\n') {
                total_lines++;
                line_len = 0;
            } else {
                line_len++;
                if (line_len >= chars_per_line) {
                    total_lines++;
                    line_len = 0;
                }
            }
        }
        notepad_total_lines[id] = total_lines;

        if (total_lines <= visible_lines) {
            thumb_h = scroll_h - 26;
            thumb_y = scroll_y + 13;
        } else {
            thumb_h = (visible_lines * (scroll_h - 26)) / total_lines;
            if (thumb_h < 20) thumb_h = 20;
            thumb_y = scroll_y + 13 + (notepad_scroll_y[id] * (scroll_h - 26 - thumb_h)) /
                      (total_lines - visible_lines);
        }
    }

    vga13_fill_rect(scroll_x + 2, thumb_y, scrollbar_w - 4, thumb_h, PAL_LIGHT_GRAY);
    hline_px(scroll_x + 2, scroll_x + scrollbar_w - 3, thumb_y, VGA13_BLACK);
    hline_px(scroll_x + 2, scroll_x + scrollbar_w - 3, thumb_y + thumb_h - 1, VGA13_BLACK);
    vline_px(scroll_x + 2, thumb_y, thumb_y + thumb_h - 1, VGA13_BLACK);
    vline_px(scroll_x + scrollbar_w - 3, thumb_y, thumb_y + thumb_h - 1, VGA13_BLACK);
    hline_px(scroll_x + 3, scroll_x + scrollbar_w - 4, thumb_y + 1, VGA13_WHITE);
    vline_px(scroll_x + 3, thumb_y + 1, thumb_y + thumb_h - 2, VGA13_WHITE);

    if (win_top_id() == id) {
        int cursor_line = 0;
        int cursor_col = 0;
        int line_start = 0;
        int chars_per_line = (content_w - 4) / char_width;
        int p = 0;

        while (p < notepad_cursor_pos[id] && p < notepad_buf_len[id]) {

            if (notepad_buf[id][p] == '\n') {
                cursor_line++;
                cursor_col = 0;
                line_start = p + 1;
            } else {

                int col_in_line = p - line_start;
                if (col_in_line >= chars_per_line) {
                    cursor_line++;
                    cursor_col = 0;
                    line_start = p;
                } else {
                    cursor_col = col_in_line + 1;
                }
            }
            p++;
        }

        if (notepad_cursor_pos[id] == 0 ||
            (notepad_cursor_pos[id] > 0 && notepad_buf[id][notepad_cursor_pos[id] - 1] == '\n')) {
            cursor_col = 0;
        }

        notepad_cursor_y[id] = cursor_line;
        notepad_cursor_x[id] = cursor_col;

        if (cursor_line >= notepad_scroll_y[id] &&
            cursor_line < notepad_scroll_y[id] + visible_lines) {
            int cursor_screen_y = cy + 4 + (cursor_line - notepad_scroll_y[id]) * 8;
            int cursor_screen_x = cx + 4 + cursor_col * char_width;
            {
                u32 phase = ui_caret_blink_fast ? 40u : 120u;
                if (((u32)timer_ticks() / phase) & 1u)
                    vline_px(cursor_screen_x, cursor_screen_y, cursor_screen_y + 7, VGA13_BLACK);
            }
        }

        if (notepad_scrollbar_dragging < 0 && notepad_manual_scroll_timer == 0) {
            if (cursor_line < notepad_scroll_y[id]) {
                notepad_scroll_y[id] = cursor_line;
                desktop_needs_full_blit = 1;
            } else if (cursor_line >= notepad_scroll_y[id] + visible_lines) {
                notepad_scroll_y[id] = cursor_line - visible_lines + 1;
                desktop_needs_full_blit = 1;
            }
        }
    }
}

void handle_notepad_scrollbar_click(int id, int mx, int my) {
    int cx, cy, cw, ch;
    int scrollbar_w = 11;
    int content_h;
    int thumb_y;
    int visible_lines, total_lines;
    int thumb_h;
    int click_in_track;
    int scroll_x, scroll_y, scroll_h;

    if (id < 0 || id >= MAX_WIN || wins[id].app != APP_notepad) return;

    cx = wins[id].x + 3;
    cy = wins[id].y + SVS_TITLE_H + 1;
    cw = wins[id].w - 6;
    ch = wins[id].h - SVS_TITLE_H - 3;
    content_h = ch - 4;
    visible_lines = content_h / 8;
    scroll_x = cx + cw - scrollbar_w - 1;
    scroll_y = cy + 2;
    scroll_h = content_h;

    total_lines = notepad_total_lines[id];
    if (total_lines <= 1) {
        int p;
        total_lines = 1;
        for (p = 0; p < notepad_buf_len[id]; p++) {
            if (notepad_buf[id][p] == '\n') total_lines++;
        }
        notepad_total_lines[id] = total_lines;
    }

    if (total_lines <= visible_lines) {
        thumb_h = scroll_h - 26;
        thumb_y = scroll_y + 13;
    } else {
        thumb_h = (visible_lines * (scroll_h - 26)) / total_lines;
        if (thumb_h < 20) thumb_h = 20;
        thumb_y = scroll_y + 13 + (notepad_scroll_y[id] * (scroll_h - 26 - thumb_h)) /
                  (total_lines - visible_lines);
    }

    if (mx < cx || mx >= cx + cw || my < cy || my >= cy + ch) {
        return;
    }

    if (mx < scroll_x || mx >= scroll_x + scrollbar_w ||
        my < scroll_y || my >= scroll_y + scroll_h) {
        return;
    }

    if (my >= scroll_y + 1 && my <= scroll_y + 10) {
        if (notepad_scroll_y[id] > 0) {
            notepad_scroll_y[id]--;
            desktop_needs_full_blit = 1;
        }
        notepad_scrollbar_dragging = -1;
        notepad_manual_scroll_timer = 30;
        return;
    }

    if (my >= scroll_y + scroll_h - 11 && my <= scroll_y + scroll_h - 1) {
        if (notepad_scroll_y[id] < total_lines - visible_lines) {
            notepad_scroll_y[id]++;
            desktop_needs_full_blit = 1;
        }
        notepad_scrollbar_dragging = -1;
        notepad_manual_scroll_timer = 30;
        return;
    }

    click_in_track = (my > scroll_y + 10 && my < scroll_y + scroll_h - 11);

    if (click_in_track && total_lines > visible_lines) {

        if (my < thumb_y) {

            notepad_scroll_y[id] -= visible_lines;
            if (notepad_scroll_y[id] < 0) notepad_scroll_y[id] = 0;
            desktop_needs_full_blit = 1;
            notepad_manual_scroll_timer = 5;
        } else if (my >= thumb_y + thumb_h) {

            notepad_scroll_y[id] += visible_lines;
            if (notepad_scroll_y[id] > total_lines - visible_lines)
                notepad_scroll_y[id] = total_lines - visible_lines;
            desktop_needs_full_blit = 1;
            notepad_manual_scroll_timer = 5;
        } else {

            notepad_scrollbar_dragging = id;
            notepad_scrollbar_drag_start_y = my;
            notepad_scrollbar_drag_start_scroll = notepad_scroll_y[id];
            desktop_needs_full_blit = 1;
        }
    }
}

void handle_notepad_key(int id, int key) {
    int i, pos;
    if (id < 0 || id >= MAX_WIN || wins[id].app != APP_notepad) return;

    if (key != 0) notepad_bar_msg[id][0] = 0;
    if (key == 0x03) {
        notepad_copy(id);
        return;
    }
    if (key == 0x18) {
        notepad_cut(id);
        return;
    }
    if (key == 0x16) {
        notepad_paste(id);
        return;
    }
    if (key == 0x01) {
        notepad_select_all(id);
        return;
    }
    if (key == 0x1A) {
        notepad_undo(id);
        return;
    }
    if (key == 0x19) {
        notepad_redo(id);
        return;
    }

    if (key == KEY_BACKSP) {
        pos = notepad_cursor_pos[id];
        if (pos > 0 && pos <= notepad_buf_len[id]) {
            notepad_save_undo(id);

            for (i = pos - 1; i < notepad_buf_len[id] - 1; i++) {
                notepad_buf[id][i] = notepad_buf[id][i + 1];
            }
            notepad_buf_len[id]--;
            notepad_buf[id][notepad_buf_len[id]] = 0;
            notepad_cursor_pos[id]--;
        }
        desktop_needs_full_blit = 1;
        return;
    }

    if (key == KEY_ENTER) {

        if (notepad_buf_len[id] < NOTEPAD_BUF_SIZE - 1) {
            notepad_save_undo(id);
            pos = notepad_cursor_pos[id];

            for (i = notepad_buf_len[id]; i > pos; i--) {
                notepad_buf[id][i] = notepad_buf[id][i - 1];
            }
            notepad_buf[id][pos] = '\n';
            notepad_buf_len[id]++;
            notepad_buf[id][notepad_buf_len[id]] = 0;
            notepad_cursor_pos[id]++;
        }
        desktop_needs_full_blit = 1;
        return;
    }

    if (key == KEY_LEFT) {
        if (notepad_cursor_pos[id] > 0) {
            notepad_cursor_pos[id]--;
        }
        desktop_needs_full_blit = 1;
        return;
    }

    if (key == KEY_RIGHT) {
        if (notepad_cursor_pos[id] < notepad_buf_len[id]) {
            notepad_cursor_pos[id]++;
        }
        desktop_needs_full_blit = 1;
        return;
    }

    if (key >= 32 && key <= 126) {
        if (notepad_buf_len[id] < NOTEPAD_BUF_SIZE - 1) {
            pos = notepad_cursor_pos[id];

            for (i = notepad_buf_len[id]; i > pos; i--) {
                notepad_buf[id][i] = notepad_buf[id][i - 1];
            }
            notepad_buf[id][pos] = (char)key;
            notepad_buf_len[id]++;
            notepad_buf[id][notepad_buf_len[id]] = 0;
            notepad_cursor_pos[id]++;
        }
        desktop_needs_full_blit = 1;
    }
}

static void on_open(int id) {
    notepad_buf[id][0] = 0;
    notepad_buf_len[id] = 0;
    notepad_cursor_pos[id] = 0;
    notepad_cursor_x[id] = 0;
    notepad_cursor_y[id] = 0;
    notepad_scroll_y[id] = 0;
    notepad_has_undo[id] = 0;
    notepad_has_redo[id] = 0;
    notepad_has_selection[id] = 0;
    notepad_has_file[id] = 0;
    notepad_find_has_pattern[id] = 0;
    notepad_find_from_pos[id] = 0;
    notepad_bar_msg[id][0] = 0;
    notepad_zebra[id] = 0;
    const char *default_text = "Welcome to Notepad!\nType your notes here...\n";
int len = str_len(default_text);
for (int i = 0; i < len; i++)
    notepad_buf[id][i] = default_text[i];
notepad_buf_len[id] = len;
notepad_cursor_pos[id] = len;
}

static void draw(int id, int cx, int cy, int cw, int ch) {
    app_notepad_draw_client(id, cx, cy, cw, ch);
}

static int overlay_is_open(void) { return notepad_ctx_menu_open; }
static void close_overlay(void) { notepad_ctx_close(); }

#include "sv_desktop_internal.h"

static void on_click(int id, app_mouse_t *ev) {
    if (ev->button != 0) return;

    int scrollbar_w = 11;
    int scroll_x = ev->cx + ev->cw - scrollbar_w - 1;

    if (ev->mx >= scroll_x && ev->mx < scroll_x + scrollbar_w) {
        
        handle_notepad_scrollbar_click(id, ev->mx, ev->my);
    } else if (notepad_scrollbar_dragging < 0) {
        
        handle_notepad_click(id, ev->mx, ev->my, ev->cx, ev->cy, ev->cw, 0);
    }
}

static void on_drag(int id, app_mouse_t *ev) {
    if (notepad_scrollbar_dragging == id) {
        
        int scrollbar_w = 11;
        int content_h   = ev->ch - 4;
        int visible_lines = content_h / 8;
        int scroll_h    = content_h;
        int total_lines = notepad_total_lines[id];
        if (total_lines < 1) total_lines = 1;

        int thumb_h = (total_lines <= visible_lines)
            ? (scroll_h - 26)
            : (visible_lines * (scroll_h - 26)) / total_lines;
        if (thumb_h < 20) thumb_h = 20;

        int track_h = scroll_h - 26 - thumb_h;
        if (track_h < 1) track_h = 1;

        int delta_y   = ev->my - notepad_scrollbar_drag_start_y;
        int max_scroll = total_lines - visible_lines;
        if (max_scroll < 0) max_scroll = 0;

        int new_scroll = notepad_scrollbar_drag_start_scroll
                       + (delta_y * max_scroll) / track_h;
        if (new_scroll < 0)           new_scroll = 0;
        if (new_scroll > max_scroll)  new_scroll = max_scroll;

        if (notepad_scroll_y[id] != new_scroll) {
            notepad_scroll_y[id] = new_scroll;
            desktop_needs_full_blit = 1;
        }
    } else if (notepad_sel_dragging == id) {
        
        handle_notepad_drag(id, ev->mx, ev->my, ev->cx, ev->cy, ev->cw);
    }
}

static void on_release(int id, app_mouse_t *ev) {
    (void)id;
    (void)ev;
    notepad_scrollbar_dragging = -1;
    notepad_end_drag();
}

static void on_right_click(int id, app_mouse_t *ev) {
    (void)id;
    notepad_ctx_menu_x = ev->mx;
    notepad_ctx_menu_y = ev->my;
    notepad_ctx_open(ev->mx, ev->my);
    notepad_ctx_menu_hover = -1;
    desktop_needs_full_blit = 1;
}

extern const MenuItem menu_sav_items[];

static const MenuItem np_file_items[] = {
    { "New",        APP_notepad },
    { "Open...",    APP_NONE },
    { "Save",       APP_NONE },
    { "Save As...", APP_NONE },
    { "Close",      APP_NONE }
};

static const MenuItem np_edit_items[] = {
    { "Undo",       APP_NONE },
    { "Redo",       APP_NONE },
    { "Cut",        APP_NONE },
    { "Copy",       APP_NONE },
    { "Paste",      APP_NONE },
    { "Clear",      APP_NONE },
    { "Select All", APP_NONE }
};

static const MenuItem np_search_items[] = {
    { "Find...",    APP_NONE },
    { "Find Again", APP_NONE }
};

static const MenuItem np_format_items[] = {
    { "Font...", APP_NONE },
    { "Style",   APP_NONE }
};

static Menu np_menus[] = {
    { "@",       0, 0, menu_sav_items, SV_MENU_SAV_COUNT },
    { "File",    0, 0, np_file_items,  5 },
    { "Edit",    0, 0, np_edit_items,  7 },
    { "Search",  0, 0, np_search_items, 2 },
    { "Format",  0, 0, np_format_items, 2 }
};
#define NP_MENU_COUNT 5

static int on_menu_action(int id, const char *label) {
    if (str_eq(label, "Undo"))       { notepad_undo(id);       desktop_needs_full_blit = 1; return 1; }
    if (str_eq(label, "Redo"))       { notepad_redo(id);       desktop_needs_full_blit = 1; return 1; }
    if (str_eq(label, "Cut"))        { notepad_cut(id);        desktop_needs_full_blit = 1; return 1; }
    if (str_eq(label, "Copy"))       { notepad_copy(id);       desktop_needs_full_blit = 1; return 1; }
    if (str_eq(label, "Paste"))      { notepad_paste(id);      desktop_needs_full_blit = 1; return 1; }
    if (str_eq(label, "Clear"))      { notepad_clear(id);      desktop_needs_full_blit = 1; return 1; }
    if (str_eq(label, "Select All")) { notepad_select_all(id); desktop_needs_full_blit = 1; return 1; }
    if (str_eq(label, "Save"))       { notepad_save(id);       desktop_needs_full_blit = 1; return 1; }
    if (str_eq(label, "Save As...")) { notepad_save_as(id);    desktop_needs_full_blit = 1; return 1; }
    if (str_eq(label, "Close"))      { extern void win_close(int); win_close(id); return 1; }
    if (str_eq(label, "Find..."))    { notepad_find_next(id, 1); return 1; }
    if (str_eq(label, "Find Again")) { notepad_find_next(id, 0); return 1; }
    if (str_eq(label, "Font...")) {
        str_cpy(notepad_bar_msg[id], "Font: VGA 8x8 (fixed)", sizeof(notepad_bar_msg[id]));
        desktop_needs_full_blit = 1;
        return 1;
    }
    if (str_eq(label, "Style")) {
        notepad_zebra[id] ^= 1;
        str_cpy(notepad_bar_msg[id], notepad_zebra[id] ? "Style: striped rows" : "Style: plain",
                sizeof(notepad_bar_msg[id]));
        desktop_needs_full_blit = 1;
        return 1;
    }
    if (str_eq(label, "Open...")) {
        
        extern win_t wins[];
        int best_disk = -1, best_z = -1;
        for (int i = 0; i < MAX_WIN; i++) {
            if (wins[i].used && wins[i].app == APP_disk && wins[i].z > best_z) {
                best_z = wins[i].z; best_disk = i;
            }
        }
        if (best_disk >= 0) {
            extern void handle_disk_key(int id, int key);
            handle_disk_key(best_disk, KEY_ENTER);
        }
        return 1;
    }
    return 0;  
}

const app_desc_t app_notepad_desc = {
    .kind           = APP_notepad,
    .default_title  = "Notepad",
    .def_x = 70, .def_y = 40, .def_w = 180, .def_h = 130,
    .icon_bmp       = icon,
    .on_open        = on_open,
    .draw           = draw,
    .on_key         = handle_notepad_key,
    .on_click       = on_click,
    .on_drag        = on_drag,
    .on_release     = on_release,
    .on_right_click = on_right_click,
    .draw_overlay   = notepad_draw_context_menu,
    .hit_overlay    = notepad_hit_context_menu,
    .hover_overlay  = notepad_ctx_hover_update,
    .overlay_is_open = overlay_is_open,
    .close_overlay  = close_overlay,
    .click_overlay  = notepad_context_menu_click,
    .menu_bar_menus = np_menus,
    .menu_bar_count = NP_MENU_COUNT,
    .on_menu_action = on_menu_action,
};

void notepad_open_file(u32 dir_cluster, const char* name) {
    if (!name || !name[0]) return;
    int np = -1;
    for (int i = 0; i < MAX_WIN; i++) {
        if (wins[i].used && wins[i].app == APP_notepad) { np = i; break; }
    }
    if (np < 0) {
        extern void app_open(app_kind_t app);
        app_open(APP_notepad);
        np = win_top_id();
    }
    if (np < 0) return;
    extern void bring_to_front(int win_idx);
    bring_to_front(np);

    char tmp[NOTEPAD_BUF_SIZE];
    int n = fs_read_file_in_dir(dir_cluster, name, tmp, NOTEPAD_BUF_SIZE - 1);
    if (n < 0) n = 0;
    tmp[n] = 0;

    notepad_buf_len[np] = 0;
    for (int i = 0; tmp[i] && notepad_buf_len[np] < NOTEPAD_BUF_SIZE - 1; i++) {
        if (tmp[i] == '\r') continue;
        notepad_buf[np][notepad_buf_len[np]++] = tmp[i];
    }
    notepad_buf[np][notepad_buf_len[np]] = 0;
    notepad_cursor_pos[np] = notepad_buf_len[np];
    notepad_scroll_y[np] = 0;
    notepad_has_selection[np] = 0;

    notepad_set_file_association(np, dir_cluster, name);
    notepad_undo_len[np] = 0;
    notepad_has_undo[np] = 0;
    notepad_has_redo[np] = 0;
    notepad_clipboard_len = 0;
    notepad_find_has_pattern[np] = 0;
    notepad_find_pattern[np][0] = 0;
    notepad_find_from_pos[np] = 0;

    str_cpy(wins[np].title, name, sizeof(wins[np].title));
    desktop_needs_full_blit = 1;
}

