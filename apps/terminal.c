#include "apps.h"
#include "../kernel/types.h"
#include "../kernel/vga.h"
#include "../kernel/keyboard.h"
#include "../kernel/gui.h"
#include "../kernel/string.h"
#include "../kernel/fs.h"
#include "../kernel/timer.h"
#include "../kernel/rtc.h"

#define TERM_COLS    54
#define TERM_ROWS    15
#define HIST_LINES   200
#define HIST_LINELEN 56
#define INPUT_MAX    80

typedef struct {
    char   lines[HIST_LINES][HIST_LINELEN];
    int    n_lines;
    int    scroll;
    char   input[INPUT_MAX];
    int    input_len;
    int    wid;

    char   cmdhist[16][INPUT_MAX];
    int    cmdhist_n;
    int    cmdhist_pos;
} TermData;

static TermData term_state;

static void term_add_line(TermData* t, const char* s) {

    int len = kstrlen(s);
    int start = 0;
    do {
        int chunk = len - start;
        if (chunk > HIST_LINELEN-1) chunk = HIST_LINELEN-1;
        if (t->n_lines < HIST_LINES) {
            kstrncpy(t->lines[t->n_lines], s + start, chunk+1);
            t->lines[t->n_lines][chunk] = 0;
            t->n_lines++;
        } else {

            for (int i = 0; i < HIST_LINES-1; i++)
                kmemcpy(t->lines[i], t->lines[i+1], HIST_LINELEN);
            kstrncpy(t->lines[HIST_LINES-1], s + start, chunk+1);
        }
        start += chunk;
    } while (start < len);
}

static void term_print(TermData* t, const char* s) {

    char line[HIST_LINELEN];
    int li = 0;
    while (*s) {
        if (*s == '\n' || *s == '\r') {
            line[li] = 0; term_add_line(t, line); li = 0;
            if (*s == '\r' && *(s+1) == '\n') s++;
        } else if (li < HIST_LINELEN-1) {
            line[li++] = *s;
        }
        s++;
    }
    if (li > 0) { line[li]=0; term_add_line(t, line); }
}

static void cmd_help(TermData* t) {
    term_print(t,
        "SavaOS Command Prompt v0.1\n"
        "Available commands:\n"
        "  help         - this help\n"
        "  ver          - show version\n"
        "  uname        - system info\n"
        "  ls           - list files\n"
        "  cat <file>   - show file\n"
        "  echo <text>  - print text\n"
        "  time         - show system time\n"
        "  clear        - clear screen\n"
        "  calc <expr>  - simple calculator\n"
        "  mem          - memory info\n"
        "  color <n>    - change color\n");
}

static int simple_calc(const char* expr) {

    int a = 0, b = 0;
    char op = 0;
    int i = 0;
    int neg = 0;
    if (expr[i] == '-') { neg=1; i++; }
    while (expr[i] >= '0' && expr[i] <= '9') a = a*10 + (expr[i++]-'0');
    if (neg) a = -a;
    while (expr[i] == ' ') i++;
    op = expr[i++];
    while (expr[i] == ' ') i++;
    neg = 0;
    if (expr[i] == '-') { neg=1; i++; }
    while (expr[i] >= '0' && expr[i] <= '9') b = b*10 + (expr[i++]-'0');
    if (neg) b = -b;
    switch (op) {
        case '+': return a + b;
        case '-': return a - b;
        case '*': return a * b;
        case '/': return b ? a / b : 0;
        case '%': return b ? a % b : 0;
        default:  return a;
    }
}

static u8 term_colors[] = {
    MAKE_COLOR(COLOR_LIGHT_GREY, COLOR_BLACK),
    MAKE_COLOR(COLOR_GREEN,      COLOR_BLACK),
    MAKE_COLOR(COLOR_LIGHT_BLUE, COLOR_BLACK),
    MAKE_COLOR(COLOR_YELLOW,     COLOR_BLACK),
    MAKE_COLOR(COLOR_WHITE,      COLOR_BLUE),
};
static int term_color_idx = 0;

static void execute_cmd(TermData* t, const char* cmd) {

    while (*cmd == ' ') cmd++;
    if (!*cmd) return;

    if (t->cmdhist_n < 16) {
        kstrncpy(t->cmdhist[t->cmdhist_n++], cmd, INPUT_MAX);
    } else {
        for (int i = 0; i < 15; i++)
            kmemcpy(t->cmdhist[i], t->cmdhist[i+1], INPUT_MAX);
        kstrncpy(t->cmdhist[15], cmd, INPUT_MAX);
    }
    t->cmdhist_pos = t->cmdhist_n;

    char buf[128];
    ksnprintf(buf, sizeof(buf), "C:\\> %s", cmd);
    term_add_line(t, buf);

    if (kstrcmp(cmd, "help") == 0) {
        cmd_help(t);
    } else if (kstrcmp(cmd, "clear") == 0) {
        t->n_lines = 0;
        t->scroll = 0;
    } else if (kstrcmp(cmd, "ls") == 0) {
        char names[FS_MAX_FILES][FS_MAX_NAME];
        int n = fs_list(names, FS_MAX_FILES);
        term_add_line(t, "Directory of C:\\");
        term_add_line(t, "");
        for (int i = 0; i < n; i++) {
            int fd = fs_open(names[i]);
            ksnprintf(buf, sizeof(buf), "  %-16s  %4u bytes",
                      names[i], fd >= 0 ? fs_size(fd) : 0);
            term_add_line(t, buf);
        }
        ksnprintf(buf, sizeof(buf), "\n  %d file(s)\n", n);
        term_print(t, buf);
    } else if (kstrncmp(cmd, "cat ", 4) == 0) {
        const char* fname = cmd + 4;
        while (*fname == ' ') fname++;
        int fd = fs_open(fname);
        if (fd < 0) {
            ksnprintf(buf, sizeof(buf), "File not found: %s\n", fname);
            term_print(t, buf);
        } else {
            term_print(t, fs_get_data(fd));
        }
    } else if (kstrncmp(cmd, "echo ", 5) == 0) {
        term_add_line(t, cmd + 5);
    } else if (kstrcmp(cmd, "time") == 0) {
        rtc_time_t tm;
        rtc_read(&tm);
        ksnprintf(buf, sizeof(buf), "%02u:%02u:%02u\n",
                  tm.hour, tm.minute, tm.second);
        term_print(t, buf);
    } else if (kstrncmp(cmd, "calc ", 5) == 0) {
        int result = simple_calc(cmd + 5);
        ksnprintf(buf, sizeof(buf), "= %d\n", result);
        term_print(t, buf);
    } else if (kstrncmp(cmd, "color ", 6) == 0) {
        int n = katoi(cmd + 6);
        if (n >= 0 && n < 5) {
            term_color_idx = n;
            term_add_line(t, "Color changed.");
        } else {
            term_add_line(t, "Usage: color 0-4");
        }
    } else {
        ksnprintf(buf, sizeof(buf), "'%s' is not recognized.", cmd);
        term_add_line(t, buf);
        term_add_line(t, "Type 'help' for commands.");
    }
}

static void term_draw(int wid) {
    TermData* t = &term_state;
    int w, h;
    win_get_size(wid, &w, &h);

    u8 bg   = MAKE_COLOR(COLOR_BLACK, COLOR_BLACK);
    u8 text = term_colors[term_color_idx];
    u8 inp  = MAKE_COLOR(COLOR_WHITE, COLOR_BLACK);
    u8 prm  = MAKE_COLOR(COLOR_LIGHT_GREY, COLOR_BLACK);

    win_fill(wid, 0, 0, w, h, ' ', bg);

    int display_rows = h - 2;
    int start_line = t->n_lines - display_rows + t->scroll;
    if (start_line < 0) start_line = 0;

    for (int row = 0; row < display_rows && start_line + row < t->n_lines; row++) {
        win_putstr(wid, 0, row, t->lines[start_line + row], text);
    }

    win_hline(wid, 0, h-2, w, MAKE_COLOR(COLOR_DARK_GREY, COLOR_BLACK));

    char prompt_line[HIST_LINELEN + 8];
    ksnprintf(prompt_line, sizeof(prompt_line), "C:\\> %s", t->input);
    win_putstr(wid, 0, h-1, prompt_line, prm);

    int cx = 5 + t->input_len;
    if (cx < w) {
        u32 ticks = timer_ticks();
        if ((ticks / 50) % 2 == 0)
            win_putchar(wid, cx, h-1, '\xDB', inp);
    }
}

static void term_key(int wid, int key) {
    TermData* t = &term_state;

    if (key == KEY_ENTER) {
        t->input[t->input_len] = 0;
        execute_cmd(t, t->input);
        t->input_len = 0;
        t->input[0] = 0;
        t->scroll = 0;
    } else if (key == KEY_BACKSP) {
        if (t->input_len > 0) t->input[--t->input_len] = 0;
    } else if (key == KEY_UP) {
        if (t->cmdhist_pos > 0) {
            t->cmdhist_pos--;
            kstrncpy(t->input, t->cmdhist[t->cmdhist_pos], INPUT_MAX);
            t->input_len = kstrlen(t->input);
        }
    } else if (key == KEY_DOWN) {
        if (t->cmdhist_pos < t->cmdhist_n - 1) {
            t->cmdhist_pos++;
            kstrncpy(t->input, t->cmdhist[t->cmdhist_pos], INPUT_MAX);
            t->input_len = kstrlen(t->input);
        } else {
            t->input[0] = 0; t->input_len = 0;
        }
    } else if (key == KEY_PGUP) {
        t->scroll -= 5; if (t->scroll < -(t->n_lines)) t->scroll = -(t->n_lines);
    } else if (key == KEY_PGDN) {
        t->scroll += 5; if (t->scroll > 0) t->scroll = 0;
    } else if (key >= 32 && key < 127) {
        if (t->input_len < INPUT_MAX - 1) {
            t->input[t->input_len++] = key;
            t->input[t->input_len] = 0;
        }
    }

    gui_redraw_window(wid);
}

void app_terminal_open(void) {
    TermData* t = &term_state;
    if (t->wid >= 0) { gui_set_active(t->wid); gui_redraw(); return; }

    t->n_lines = 0; t->scroll = 0;
    t->input_len = 0; t->input[0] = 0;
    t->cmdhist_n = 0; t->cmdhist_pos = 0;
    term_color_idx = 1;

    term_print(t,
         "Type 'help' for available commands.\n\n");

    int wid = gui_open_window(4, 2, 62, 20,
        "Command Prompt",
        term_draw, term_key, (void*)0);
    t->wid = wid;
}

void app_terminal_init(void) {
    term_state.wid = -1;
}
