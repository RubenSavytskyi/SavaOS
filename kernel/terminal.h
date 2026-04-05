#ifndef TERMINAL_H
#define TERMINAL_H

#include "types.h"

#define TERM_MAX_LINES      24
#define TERM_LINE_LEN       64
#define TERM_CLIPBOARD_SIZE 2048
#define TERM_CTX_MENU_ITEMS 4

typedef struct {
    char lines[TERM_MAX_LINES][TERM_LINE_LEN];
    int  line_count;
    char input[TERM_LINE_LEN];
    int  input_len;
    int  scroll_y;
} term_state_t;

extern term_state_t term_states[];
extern int term_max_win;

extern char term_clipboard[TERM_CLIPBOARD_SIZE];
extern int  term_clipboard_len;

extern int term_scrollbar_dragging;
extern int term_scrollbar_drag_start_y;
extern int term_scrollbar_drag_start_scroll;

extern int term_ctx_menu_open;
extern int term_ctx_menu_x;
extern int term_ctx_menu_y;
extern int term_ctx_menu_hover;
extern const char *term_ctx_menu_labels[TERM_CTX_MENU_ITEMS];

extern int desktop_needs_full_blit;
extern int str_len(const char *s);
extern void str_cpy(char *d, const char *s, int max);
extern int str_eq(const char *a, const char *b);
extern int str_starts_with(const char *s, const char *pfx);
extern void str_copy_n(char *dst, const char *src, int max);

void term_init(int max_windows);
void term_window_init(int id);

void term_push_line(int id, const char *s);
void term_clear(int id);

void term_exec_command(int id, const char *cmd);

void handle_terminal_key(int id, int key);

void term_build_output_text(int id, int clear_after);
void term_paste_clipboard_to_input(int id);

void handle_terminal_scrollbar_click(int id, int mx, int my, int wins_x, int wins_y, int wins_w, int wins_h);

void draw_term_context_menu(void);
int hit_term_context_menu(int mx, int my);
void term_context_menu_click(int mx, int my);
int term_ctx_menu_width(void);

const char* term_get_line(int id, int line_idx);
int term_get_line_count(int id);
int term_get_scroll_y(int id);
const char* term_get_input(int id);
int term_get_input_len(int id);

#endif
