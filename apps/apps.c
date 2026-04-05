#include "apps.h"
#include "../kernel/types.h"
#include "../kernel/vga.h"
#include "../kernel/keyboard.h"
#include "../kernel/gui.h"
#include "../kernel/string.h"
#include "../kernel/fs.h"

#define CALC_DISP 16

typedef struct {
    char  display[CALC_DISP+1];
    int   disp_len;
    long  operand;
    char  op;
    int   fresh;
    int   wid;
} CalcData;

static CalcData calc_state;

static const char* calc_btns[5][4] = {
    { "7", "8", "9", "/" },
    { "4", "5", "6", "*" },
    { "1", "2", "3", "-" },
    { "0", ".", "=", "+" },
    { "C", "+/-","BS","%" },
};

static long calc_get_val(CalcData* c) {
    long r = 0, neg = 0; int i = 0;
    if (c->display[0] == '-') { neg = 1; i = 1; }
    for (; c->display[i]; i++) {
        if (c->display[i] >= '0' && c->display[i] <= '9')
            r = r * 10 + (c->display[i]-'0');
    }
    return neg ? -r : r;
}

static void calc_set_val(CalcData* c, long v) {
    if (v < 0) { c->display[0] = '-'; kutoa((u32)(-v), c->display+1, 10); }
    else kutoa((u32)v, c->display, 10);
    c->disp_len = kstrlen(c->display);
}

static void calc_press(CalcData* c, const char* btn) {
    if (btn[0] == 'C') {
        c->display[0] = '0'; c->display[1] = 0; c->disp_len = 1;
        c->operand = 0; c->op = 0; c->fresh = 1;
    } else if (btn[0] == 'B' && btn[1] == 'S') {
        if (c->disp_len > 1) { c->display[--c->disp_len] = 0; }
        else { c->display[0]='0'; c->display[1]=0; c->disp_len=1; }
    } else if (btn[0] == '+' && btn[1] == '/') {
        if (c->display[0] == '-') {
            for (int i=0;c->display[i];i++) c->display[i]=c->display[i+1];
            c->disp_len--;
        } else {
            for (int i=c->disp_len;i>=0;i--) c->display[i+1]=c->display[i];
            c->display[0]='-'; c->disp_len++;
        }
    } else if (btn[0]>='0' && btn[0]<='9') {
        if (c->fresh || (c->disp_len==1 && c->display[0]=='0')) {
            c->display[0]=btn[0]; c->display[1]=0; c->disp_len=1; c->fresh=0;
        } else if (c->disp_len < CALC_DISP-1) {
            c->display[c->disp_len++]=btn[0]; c->display[c->disp_len]=0;
        }
    } else if (btn[0]=='0') {
        if (!c->fresh && !(c->disp_len==1&&c->display[0]=='0')) {
            if (c->disp_len < CALC_DISP-1) { c->display[c->disp_len++]='0'; c->display[c->disp_len]=0; }
        }
    } else if (btn[0]=='+' || btn[0]=='-' || btn[0]=='*' || btn[0]=='/') {
        c->operand = calc_get_val(c);
        c->op = btn[0]; c->fresh = 1;
    } else if (btn[0]=='=') {
        if (c->op) {
            long a = c->operand, b = calc_get_val(c);
            long res = 0;
            switch(c->op) {
                case '+': res=a+b; break; case '-': res=a-b; break;
                case '*': res=a*b; break;
                case '/': res=b?a/b:0; break;
                case '%': res=b?a%b:0; break;
            }
            calc_set_val(c, res); c->op=0; c->fresh=1;
        }
    } else if (btn[0]=='%') {
        long v = calc_get_val(c);
        calc_set_val(c, v / 100); c->fresh=1;
    }
}

static void calc_draw(int wid) {
    CalcData* c = &calc_state;
    int w, h; win_get_size(wid, &w, &h); (void)w; (void)h;

    u8 bg  = MAKE_COLOR(COLOR_BLACK, COLOR_LIGHT_GREY);
    u8 dbg = MAKE_COLOR(COLOR_BLACK, COLOR_WHITE);
    u8 btn = MAKE_COLOR(COLOR_BLACK, COLOR_LIGHT_GREY);
    u8 opb = MAKE_COLOR(COLOR_BLACK, COLOR_CYAN);
    u8 eqb = MAKE_COLOR(COLOR_WHITE, COLOR_BLUE);

    win_fill(wid, 0, 0, 22, 14, ' ', bg);

    win_fill(wid, 1, 1, 20, 1, ' ', dbg);
    int dlen = kstrlen(c->display);
    win_putstr(wid, 1 + (20 - dlen), 1, c->display, dbg);

    if (c->op) win_putchar(wid, 20, 1, c->op, MAKE_COLOR(COLOR_RED,COLOR_WHITE));

    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 4; col++) {
            int bx = 1 + col * 5;
            int by = 3 + row * 2;
            const char* lbl = calc_btns[row][col];
            u8 bc = (lbl[0]=='/'||lbl[0]=='*'||lbl[0]=='-'||
                     (lbl[0]=='+'&&lbl[1]==0)||lbl[0]=='%') ? opb :
                    (lbl[0]=='=') ? eqb : btn;
            win_fill(wid, bx, by, 4, 1, ' ', bc);
            int llen = kstrlen(lbl);
            win_putstr(wid, bx + (4-llen)/2, by, lbl, bc);

            win_putchar(wid, bx-0, by, ' ', MAKE_COLOR(COLOR_WHITE,COLOR_LIGHT_GREY));
        }
    }
}

static void calc_key(int wid, int key) {
    CalcData* c = &calc_state;
    char btn[3] = {0};
    if (key >= '0' && key <= '9') { btn[0]=key; }
    else if (key=='+') { btn[0]='+'; }
    else if (key=='-') { btn[0]='-'; }
    else if (key=='*') { btn[0]='*'; }
    else if (key=='/') { btn[0]='/'; }
    else if (key=='%') { btn[0]='%'; }
    else if (key==KEY_ENTER || key=='=') { btn[0]='='; }
    else if (key==KEY_BACKSP) { btn[0]='B'; btn[1]='S'; }
    else if (key=='c' || key=='C') { btn[0]='C'; }
    else return;
    if (btn[0]) { calc_press(c, btn); gui_redraw_window(wid); }
}

void app_calc_open(void) {
    CalcData* c = &calc_state;
    c->display[0]='0'; c->display[1]=0; c->disp_len=1;
    c->operand=0; c->op=0; c->fresh=1;
    c->wid = gui_open_window(25, 3, 26, 16, "Calculator", calc_draw, calc_key, (void*)0);
}
void app_calc_init(void) { calc_state.wid = -1; }

typedef struct {
    char  names[FS_MAX_FILES][FS_MAX_NAME];
    int   n_files;
    int   selected;
    int   wid;
} FMData;

static FMData fm_state;

static void fm_refresh(FMData* f) {
    f->n_files = fs_list(f->names, FS_MAX_FILES);
    if (f->selected >= f->n_files) f->selected = f->n_files - 1;
    if (f->selected < 0) f->selected = 0;
}

static void fm_draw(int wid) {
    FMData* f = &fm_state;
    int w, h; win_get_size(wid, &w, &h);

    u8 header = MAKE_COLOR(COLOR_WHITE, COLOR_BLUE);
    u8 row_e  = MAKE_COLOR(COLOR_BLACK, COLOR_WHITE);
    u8 row_o  = MAKE_COLOR(COLOR_BLACK, COLOR_LIGHT_GREY);
    u8 sel    = MAKE_COLOR(COLOR_WHITE, COLOR_BLUE);
    u8 sta    = MAKE_COLOR(COLOR_BLACK, COLOR_LIGHT_GREY);

    win_fill(wid, 0, 0, w, 1, ' ', header);
    win_putstr(wid, 1, 0, "Name             Size    ", header);

    for (int i = 0; i < f->n_files && i < h-2; i++) {
        int fd = fs_open(f->names[i]);
        u32 sz = fd>=0 ? fs_size(fd) : 0;
        char line[64];
        ksnprintf(line, sizeof(line), "%-16s %5u B", f->names[i], sz);
        u8 rc = (i == f->selected) ? sel : (i%2==0 ? row_e : row_o);
        win_fill(wid, 0, 1+i, w, 1, ' ', rc);
        win_putstr(wid, 1, 1+i, line, rc);

        win_putchar(wid, 0, 1+i, '\x1A', rc);
    }

    char stat[64];
    ksnprintf(stat, sizeof(stat), " %d files   ENTER=open  DEL=delete",
              f->n_files);
    win_fill(wid, 0, h-1, w, 1, ' ', sta);
    win_putstr(wid, 0, h-1, stat, sta);
}

static void fm_key(int wid, int key) {
    FMData* f = &fm_state;
    if (key == KEY_UP) { if (f->selected > 0) f->selected--; }
    else if (key == KEY_DOWN) { if (f->selected < f->n_files-1) f->selected++; }
    else if (key == KEY_ENTER) {
        if (f->selected < f->n_files)
            app_notepad_open_file(f->names[f->selected]);
    } else if (key == KEY_DELETE) {
        if (f->selected < f->n_files) {
            fs_delete(f->names[f->selected]);
            fm_refresh(f);
        }
    }
    gui_redraw_window(wid);
}

void app_filemanager_open(void) {
    FMData* f = &fm_state;
    f->selected = 0;
    fm_refresh(f);
    f->wid = gui_open_window(5, 2, 44, 18, "File Manager", fm_draw, fm_key, (void*)0);
}
void app_filemanager_init(void) { fm_state.wid = -1; }
