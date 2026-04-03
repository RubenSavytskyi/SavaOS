#include "apps.h"
#include "../kernel/types.h"
#include "../kernel/vga.h"
#include "../kernel/keyboard.h"
#include "../kernel/gui.h"
#include "../kernel/string.h"
#include "../kernel/fs.h"

#define PAD_COLS    58
#define PAD_ROWS    16
#define PAD_BUFSIZE 1800

typedef struct {
    char  buf[PAD_BUFSIZE];
    int   len;
    int   cursor;   
    int   scroll;   
    int   wid;
    char  filename[FS_MAX_NAME];
    int   modified;
} PadData;

static PadData pad_state;


static void cursor_pos(PadData* p, int* row, int* col) {
    *row = 0; *col = 0;
    for (int i = 0; i < p->cursor && i < p->len; i++) {
        if (p->buf[i] == '\n') { (*row)++; *col = 0; }
        else (*col)++;
    }
}

static void pad_draw(int wid) {
    PadData* p = &pad_state;
    int w, h;
    win_get_size(wid, &w, &h);

    u8 bg  = MAKE_COLOR(COLOR_BLACK, COLOR_WHITE);
    u8 cur = MAKE_COLOR(COLOR_WHITE, COLOR_BLACK);
    u8 sta = MAKE_COLOR(COLOR_BLACK, COLOR_LIGHT_GREY);

    win_fill(wid, 0, 0, w, h-1, ' ', bg);
    win_fill(wid, 0, h-1, w, 1, ' ', sta);

    
    int cr, cc; cursor_pos(p, &cr, &cc);
    char status[64];
    ksnprintf(status, sizeof(status), " %s%s   Ln %d, Col %d",
              p->filename[0] ? p->filename : "untitled",
              p->modified ? "*" : "", cr+1, cc+1);
    win_putstr(wid, 0, h-1, status, sta);

    
    int display_h = h - 1;
    int line = 0, col = 0, row = 0;
    int in_view_line = 0; (void)in_view_line;

    for (int i = 0; i <= p->len; i++) {
        char c = (i < p->len) ? p->buf[i] : 0;

        
        if (i == p->cursor && line >= p->scroll && row < display_h) {
            win_putchar(wid, col, row, c ? c : ' ', cur);
            if (c == '\n' || c == 0) { row++; col = 0; line++; continue; }
            col++;
            continue;
        }

        if (c == '\n' || c == 0) {
            line++;
            if (line > p->scroll) { row++; col = 0; }
            if (c == 0) break;
            continue;
        }

        if (line >= p->scroll && row < display_h) {
            if (col < w) win_putchar(wid, col, row, c, bg);
        }
        col++;
        if (col >= w) { col = 0; line++; if (line > p->scroll) row++; }
    }
}

static void pad_key(int wid, int key) {
    PadData* p = &pad_state;

    if (key == KEY_ENTER) {
        if (p->len < PAD_BUFSIZE - 1) {
            
            for (int i = p->len; i > p->cursor; i--) p->buf[i] = p->buf[i-1];
            p->buf[p->cursor++] = '\n';
            p->len++;
            p->modified = 1;
        }
    } else if (key == KEY_BACKSP) {
        if (p->cursor > 0) {
            p->cursor--;
            for (int i = p->cursor; i < p->len-1; i++) p->buf[i] = p->buf[i+1];
            p->len--;
            p->buf[p->len] = 0;
            p->modified = 1;
        }
    } else if (key == KEY_DELETE) {
        if (p->cursor < p->len) {
            for (int i = p->cursor; i < p->len-1; i++) p->buf[i] = p->buf[i+1];
            p->len--;
            p->buf[p->len] = 0;
            p->modified = 1;
        }
    } else if (key == KEY_LEFT) {
        if (p->cursor > 0) p->cursor--;
    } else if (key == KEY_RIGHT) {
        if (p->cursor < p->len) p->cursor++;
    } else if (key == KEY_UP) {
        
        int target = p->cursor - 1;
        while (target > 0 && p->buf[target] != '\n') target--;
        int line_start = target; 
        if (target > 0) {
            target--;
            while (target > 0 && p->buf[target-1] != '\n') target--;
        } else target = 0;
        int col_now = 0;
        for (int i = (p->cursor > 0 ? p->cursor-1 : 0); i >= 0 && p->buf[i] != '\n'; i--) col_now++;
        p->cursor = target + col_now;
        if (p->cursor > line_start) p->cursor = line_start > 0 ? line_start : 0;
        
        p->cursor = p->cursor < 0 ? 0 : p->cursor;
    } else if (key == KEY_DOWN) {
        
        while (p->cursor < p->len && p->buf[p->cursor] != '\n') p->cursor++;
        if (p->cursor < p->len) p->cursor++;
    } else if (key == KEY_HOME) {
        while (p->cursor > 0 && p->buf[p->cursor-1] != '\n') p->cursor--;
    } else if (key == KEY_END) {
        while (p->cursor < p->len && p->buf[p->cursor] != '\n') p->cursor++;
    } else if (key == KEY_PGUP) {
        if (p->scroll >= 10) p->scroll -= 10; else p->scroll = 0;
    } else if (key == KEY_PGDN) {
        p->scroll += 10;
    } else if (key == 19) { 
        if (p->filename[0]) {
            int fd = fs_create(p->filename);
            fs_write(fd, p->buf, p->len);
            p->modified = 0;
        }
    } else if (key >= 32 && key < 127) {
        if (p->len < PAD_BUFSIZE - 1) {
            for (int i = p->len; i > p->cursor; i--) p->buf[i] = p->buf[i-1];
            p->buf[p->cursor++] = key;
            p->len++;
            p->buf[p->len] = 0;
            p->modified = 1;
        }
    }

    
    int cr, cc; cursor_pos(p, &cr, &cc); (void)cc;
    int w, h; win_get_size(wid, &w, &h); (void)w;
    if (cr < p->scroll) p->scroll = cr;
    if (cr >= p->scroll + h - 2) p->scroll = cr - (h-3);

    gui_redraw_window(wid);
}

void app_notepad_open(void) {
    app_notepad_open_file("notes.txt");
}

void app_notepad_open_file(const char* filename) {
    PadData* p = &pad_state;
    p->len = 0; p->cursor = 0; p->scroll = 0; p->modified = 0;
    p->buf[0] = 0;

    if (filename && filename[0]) {
        kstrncpy(p->filename, filename, FS_MAX_NAME);
        int fd = fs_open(filename);
        if (fd >= 0) {
            p->len = fs_read(fd, p->buf, PAD_BUFSIZE-1);
            if (p->len < 0) p->len = 0;
            p->buf[p->len] = 0;
        }
    } else {
        p->filename[0] = 0;
    }

    char title[40];
    ksnprintf(title, sizeof(title), "Notepad - %s",
              p->filename[0] ? p->filename : "untitled");

    p->wid = gui_open_window(3, 1, 66, 21, title,
                             pad_draw, pad_key, (void*)0);
}

void app_notepad_init(void) {
    pad_state.wid = -1;
    pad_state.len = 0;
    pad_state.filename[0] = 0;
}
