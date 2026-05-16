#include "app_pong.h"
#include "sv_desktop_internal.h"
#include "sv_gfx.h"
#include "string.h"
#include "keyboard.h"
#include "timer.h"

#define PONG_W          200
#define PONG_H          150
#define PADDLE_W        4
#define PADDLE_H        24
#define BALL_SIZE       3
#define PADDLE_SPEED    2
#define BALL_SPEED_INIT 2
#define SCORE_WIN       7

typedef enum { PONG_MENU, PONG_PLAY, PONG_PAUSE, PONG_WIN } pong_state_t;

typedef struct {
    pong_state_t state;

    
    int ly, ry;         

    
    int bx, by;         
    int vx, vy;         

    
    int score_l, score_r;

    
    int winner;

    
    int tick;
    int speed;          

    
    int key_up, key_down;
} pong_t;

static pong_t pong[MAX_WIN];

static int pong_abs(int v) { return v < 0 ? -v : v; }

static void pong_reset_ball(int id, int dir) {
    pong_t *p = &pong[id];
    p->bx = PONG_W / 2 - BALL_SIZE / 2;
    p->by = PONG_H / 2 - BALL_SIZE / 2;
    p->vx = p->speed * dir;
    p->vy = p->speed;
    p->tick = 0;
}

static void pong_start(int id) {
    pong_t *p = &pong[id];
    p->ly = (PONG_H - PADDLE_H) / 2;
    p->ry = (PONG_H - PADDLE_H) / 2;
    p->score_l = 0;
    p->score_r = 0;
    p->speed = BALL_SPEED_INIT;
    p->state = PONG_PLAY;
    pong_reset_ball(id, 1);
}

static const unsigned char digit_bmp[10][5] = {
    {0x7,0x5,0x5,0x5,0x7}, 
    {0x2,0x2,0x2,0x2,0x2}, 
    {0x7,0x1,0x7,0x4,0x7}, 
    {0x7,0x1,0x7,0x1,0x7}, 
    {0x5,0x5,0x7,0x1,0x1}, 
    {0x7,0x4,0x7,0x1,0x7}, 
    {0x7,0x4,0x7,0x5,0x7}, 
    {0x7,0x1,0x1,0x1,0x1}, 
    {0x7,0x5,0x7,0x5,0x7}, 
    {0x7,0x5,0x7,0x1,0x7}, 
};

static void draw_digit(int x, int y, int d, int col) {
    if (d < 0 || d > 9) return;
    for (int row = 0; row < 5; row++) {
        unsigned char bits = digit_bmp[d][row];
        for (int bit = 2; bit >= 0; bit--) {
            if (bits & (1 << bit))
                vga13_fill_rect(x + (2 - bit) * 2, y + row * 2, 2, 2, col);
        }
    }
}

static void draw_score(int x, int y, int val, int col) {
    if (val >= 10) {
        draw_digit(x, y, val / 10, col);
        draw_digit(x + 8, y, val % 10, col);
    } else {
        draw_digit(x, y, val, col);
    }
}

static void pong_tick(int id) {
    pong_t *p = &pong[id];
    if (p->state != PONG_PLAY) return;

    p->tick++;

    
    {
        
        u8 sc = inb(0x60);
        
        if (sc == 0x11) p->key_up = 1;
        else if (sc == 0x91) p->key_up = 0;
        if (sc == 0x1F) p->key_down = 1;
        else if (sc == 0x9F) p->key_down = 0;
    }
    if (p->key_up) {
        p->ly -= PADDLE_SPEED * 2;
        if (p->ly < 0) p->ly = 0;
    }
    if (p->key_down) {
        p->ly += PADDLE_SPEED * 2;
        if (p->ly + PADDLE_H > PONG_H) p->ly = PONG_H - PADDLE_H;
    }

    
    int ball_cy = p->by + BALL_SIZE / 2;
    int ai_cy   = p->ry + PADDLE_H / 2;
    if (ball_cy < ai_cy - 1) {
        p->ry -= PADDLE_SPEED;
        if (p->ry < 0) p->ry = 0;
    } else if (ball_cy > ai_cy + 1) {
        p->ry += PADDLE_SPEED;
        if (p->ry + PADDLE_H > PONG_H) p->ry = PONG_H - PADDLE_H;
    }

    
    p->bx += p->vx;
    p->by += p->vy;

    
    if (p->by <= 0)              { p->by = 0;              p->vy = -p->vy; }
    if (p->by + BALL_SIZE >= PONG_H) { p->by = PONG_H - BALL_SIZE; p->vy = -p->vy; }

    
    int lx = PADDLE_W + 2;
    if (p->vx < 0 &&
        p->bx <= lx + PADDLE_W &&
        p->bx + BALL_SIZE >= lx &&
        p->by + BALL_SIZE >= p->ly &&
        p->by <= p->ly + PADDLE_H)
    {
        p->bx = lx + PADDLE_W;
        p->vx = -p->vx;
        
        int rel = (p->by + BALL_SIZE / 2) - (p->ly + PADDLE_H / 2);
        p->vy = rel / 4;
        if (p->vy == 0) p->vy = 1;
        
        if (pong_abs(p->vx) < 6) p->vx = p->vx > 0 ? p->vx + 1 : p->vx - 1;
    }

    
    int rx = PONG_W - PADDLE_W - 2 - PADDLE_W;
    if (p->vx > 0 &&
        p->bx + BALL_SIZE >= rx &&
        p->bx <= rx + PADDLE_W &&
        p->by + BALL_SIZE >= p->ry &&
        p->by <= p->ry + PADDLE_H)
    {
        p->bx = rx - BALL_SIZE;
        p->vx = -p->vx;
        int rel = (p->by + BALL_SIZE / 2) - (p->ry + PADDLE_H / 2);
        p->vy = rel / 4;
        if (p->vy == 0) p->vy = -1;
        if (pong_abs(p->vx) < 6) p->vx = p->vx > 0 ? p->vx + 1 : p->vx - 1;
    }

    
    if (p->bx + BALL_SIZE < 0) {
        p->score_r++;
        if (p->score_r >= SCORE_WIN) { p->state = PONG_WIN; p->winner = 1; return; }
        pong_reset_ball(id, 1);
    }
    if (p->bx > PONG_W) {
        p->score_l++;
        if (p->score_l >= SCORE_WIN) { p->state = PONG_WIN; p->winner = 0; return; }
        pong_reset_ball(id, -1);
    }
}

static void draw_centered_string(int cx, int y, int area_w, const char *s, int fg, int bg, int inv) {
    int len = str_len(s);
    int tx = cx + (area_w - len * 6) / 2;
    vga13_draw_string(tx, y, s, fg, bg, inv);
}

static void pong_draw(int id, int cx, int cy, int cw, int ch) {
    pong_t *p = &pong[id];

    
    vga13_fill_rect(cx, cy, cw, ch, VGA13_BLACK);

    
    hline_px(cx, cx + cw - 1, cy,          VGA13_WHITE);
    hline_px(cx, cx + cw - 1, cy + ch - 1, VGA13_WHITE);

    
    for (int y = cy + 2; y < cy + ch - 2; y += 4)
        vga13_fill_rect(cx + cw / 2 - 1, y, 2, 2, PAL_DARK_GRAY);

    if (p->state == PONG_MENU) {
        draw_centered_string(cx, cy + ch/2 - 16, cw, "PONG", VGA13_WHITE, VGA13_BLACK, 0);
        draw_centered_string(cx, cy + ch/2 - 4,  cw, "Press ENTER to start", VGA13_WHITE, VGA13_BLACK, 0);
        draw_centered_string(cx, cy + ch/2 + 8,  cw, "W/S  - move paddle",  PAL_DARK_GRAY, VGA13_BLACK, 0);
        return;
    }

    if (p->state == PONG_WIN) {
        const char *msg = p->winner == 0 ? "YOU WIN!" : "CPU WINS";
        draw_centered_string(cx, cy + ch/2 - 8, cw, msg,            VGA13_WHITE, VGA13_BLACK, 0);
        draw_centered_string(cx, cy + ch/2 + 4, cw, "ENTER to retry", VGA13_WHITE, VGA13_BLACK, 0);
        
        draw_score(cx + cw/2 - 24, cy + 6, p->score_l, VGA13_WHITE);
        draw_score(cx + cw/2 + 12, cy + 6, p->score_r, VGA13_WHITE);
        return;
    }

    if (p->state == PONG_PAUSE) {
        draw_centered_string(cx, cy + ch/2 - 4, cw, "PAUSED  (P)", VGA13_WHITE, VGA13_BLACK, 0);
    }

    
    draw_score(cx + cw/2 - 24, cy + 6, p->score_l, VGA13_WHITE);
    draw_score(cx + cw/2 + 12, cy + 6, p->score_r, VGA13_WHITE);

    
    int lx = cx + PADDLE_W + 2;
    vga13_fill_rect(lx, cy + p->ly, PADDLE_W, PADDLE_H, VGA13_WHITE);

    
    int rx = cx + cw - PADDLE_W - 2 - PADDLE_W;
    vga13_fill_rect(rx, cy + p->ry, PADDLE_W, PADDLE_H, VGA13_WHITE);

    
    vga13_fill_rect(cx + p->bx, cy + p->by, BALL_SIZE, BALL_SIZE, VGA13_WHITE);
}

static void on_open(int id) {
    pong_t *p = &pong[id];
    p->state    = PONG_MENU;
    p->score_l  = 0;
    p->score_r  = 0;
    p->speed    = BALL_SPEED_INIT;
    p->ly       = (PONG_H - PADDLE_H) / 2;
    p->ry       = (PONG_H - PADDLE_H) / 2;
    p->bx       = PONG_W / 2;
    p->by       = PONG_H / 2;
    p->vx       = p->vy = 0;
    p->tick     = 0;
    p->winner   = 0;
}

static void on_draw(int id, int cx, int cy, int cw, int ch) {
    pong_tick(id);
    pong_draw(id, cx, cy, cw, ch);
    if (pong[id].state == PONG_PLAY) {
        

        sleep_ms(36);
        desktop_needs_full_blit = 1;
    }
}

static void on_key(int id, int key) {
    if (id < 0 || id >= MAX_WIN) return;
    pong_t *p = &pong[id];

    if (p->state == PONG_MENU || p->state == PONG_WIN) {
        if (key == KEY_ENTER) { pong_start(id); desktop_needs_full_blit = 1; }
        return;
    }

    if (key == 'p' || key == 'P') {
        p->state = (p->state == PONG_PLAY) ? PONG_PAUSE : PONG_PLAY;
        desktop_needs_full_blit = 1;
        return;
    }

    if (p->state != PONG_PLAY) return;

    
    {
        u8 sc = inb(0x60);
        p->key_up   = (sc == 0x11); 
        p->key_down = (sc == 0x1F); 
    }
    (void)key;
}

static void on_click(int id, app_mouse_t *ev) {
    (void)id; (void)ev;
}

extern const MenuItem menu_sav_items[];

static const MenuItem pong_game_items[] = {
    { "New Game", APP_NONE },
    { "Pause",    APP_NONE }
};

static Menu pong_menus[] = {
    { "@",    0, 0, menu_sav_items, SV_MENU_SAV_COUNT },
    { "Game", 0, 0, pong_game_items, 2 }
};
#define PONG_MENU_COUNT 2

static int on_menu_action(int id, const char *label) {
    if (str_eq(label, "New Game")) { pong_start(id); desktop_needs_full_blit = 1; return 1; }
    if (str_eq(label, "Pause")) {
        pong_t *p = &pong[id];
        if (p->state == PONG_PLAY)  p->state = PONG_PAUSE;
        else if (p->state == PONG_PAUSE) p->state = PONG_PLAY;
        desktop_needs_full_blit = 1;
        return 1;
    }
    return 0;
}

const app_desc_t app_pong_desc = {
    .kind           = APP_pong,
    .default_title  = "Pong",
    .def_x = 60, .def_y = 30, .def_w = PONG_W, .def_h = PONG_H + 20,
    

    .on_open        = on_open,
    .draw           = on_draw,
    .on_key         = on_key,
    .on_click       = on_click,
    .menu_bar_menus = pong_menus,
    .menu_bar_count = PONG_MENU_COUNT,
    .on_menu_action = on_menu_action,
};
