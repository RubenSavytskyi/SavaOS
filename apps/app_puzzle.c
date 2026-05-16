#include "app_puzzle.h"
#include "sv_desktop_internal.h"
#include "sv_gfx.h"
#include "timer.h"
#include "keyboard.h"

int puzzle_tiles[MAX_WIN][16];
int puzzle_empty_pos[MAX_WIN];
static int puzzle_moves[MAX_WIN];
static int puzzle_solved[MAX_WIN];
static u32 puzzle_rng_state = 0;

static int puzzle_rand(void) {
    puzzle_rng_state = puzzle_rng_state * 1103515245 + 12345;
    return (int)(puzzle_rng_state & 0x7fff);
}

static int puzzle_is_solved(int id) {
    if (id < 0 || id >= MAX_WIN) return 0;
    for (int i = 0; i < 15; i++) {
        if (puzzle_tiles[id][i] != i + 1) return 0;
    }
    return puzzle_tiles[id][15] == 0;
}

void puzzle_init(int id) {
    if (id < 0 || id >= MAX_WIN) return;

    for (int i = 0; i < 15; i++) puzzle_tiles[id][i] = i + 1;
    puzzle_tiles[id][15] = 0;
    puzzle_empty_pos[id] = 15;
    puzzle_moves[id] = 0;
    puzzle_solved[id] = 0;

    puzzle_rng_state = timer_ticks() ^ ((u32)id * 0x9e3779b9u);
    int steps = 80 + (timer_ticks() % 80);
    int prev_empty = -1;
    while (steps-- > 0) {
        int empty = puzzle_empty_pos[id];
        int er = empty / 4;
        int ec = empty % 4;

        int neigh[4];
        int n = 0;
        if (er > 0) neigh[n++] = empty - 4;
        if (er < 3) neigh[n++] = empty + 4;
        if (ec > 0) neigh[n++] = empty - 1;
        if (ec < 3) neigh[n++] = empty + 1;

        if (prev_empty >= 0 && n > 1) {
            int filtered[4];
            int fn = 0;
            for (int i = 0; i < n; i++) {
                if (neigh[i] != prev_empty) filtered[fn++] = neigh[i];
            }
            if (fn > 0) {
                for (int i = 0; i < fn; i++) neigh[i] = filtered[i];
                n = fn;
            }
        }

        int next_empty = neigh[puzzle_rand() % n];

        puzzle_tiles[id][empty] = puzzle_tiles[id][next_empty];
        puzzle_tiles[id][next_empty] = 0;
        prev_empty = empty;
        puzzle_empty_pos[id] = next_empty;
    }

    puzzle_solved[id] = puzzle_is_solved(id);
}

void puzzle_try_move_tile(int id, int tile_idx) {
    if (id < 0 || id >= MAX_WIN) return;
    if (tile_idx < 0 || tile_idx >= 16) return;
    if (puzzle_tiles[id][tile_idx] == 0) return;

    int empty = puzzle_empty_pos[id];
    int er = empty / 4, ec = empty % 4;
    int tr = tile_idx / 4, tc = tile_idx % 4;

    int manhattan = (er > tr ? er - tr : tr - er) + (ec > tc ? ec - tc : tc - ec);
    if (manhattan != 1) return;

    puzzle_tiles[id][empty] = puzzle_tiles[id][tile_idx];
    puzzle_tiles[id][tile_idx] = 0;
    puzzle_empty_pos[id] = tile_idx;
    puzzle_moves[id]++;

    puzzle_solved[id] = puzzle_is_solved(id);
}

void handle_puzzle_key(int id, int key) {
    if (id < 0 || id >= MAX_WIN || wins[id].app != APP_puzzle) return;
    int empty = puzzle_empty_pos[id];
    int er = empty / 4, ec = empty % 4;
    int next_empty = -1;

    if (key == KEY_UP && er > 0) next_empty = empty - 4;
    else if (key == KEY_DOWN && er < 3) next_empty = empty + 4;
    else if (key == KEY_LEFT && ec > 0) next_empty = empty - 1;
    else if (key == KEY_RIGHT && ec < 3) next_empty = empty + 1;
    else if (key == KEY_ENTER) {
        puzzle_init(id);
        desktop_needs_full_blit = 1;
        return;
    } else {
        return;
    }

    puzzle_tiles[id][empty] = puzzle_tiles[id][next_empty];
    puzzle_tiles[id][next_empty] = 0;
    puzzle_empty_pos[id] = next_empty;
    puzzle_moves[id]++;
    puzzle_solved[id] = puzzle_is_solved(id);
    desktop_needs_full_blit = 1;
}

void app_puzzle_draw_client(int id, int cx, int cy, int cw, int ch) {

    vga13_fill_rect(cx, cy, cw, ch, VGA13_WHITE);
    for (int py = cy; py < cy + ch; py += 2) {
        for (int px = cx + ((py - cy) & 1); px < cx + cw; px += 2) {
            vga13_put_pixel(px, py, PAL_DARK_GRAY);
        }
    }

    char buf[24];
    ksnprintf(buf, sizeof(buf), "Moves: %u", (unsigned)puzzle_moves[id]);
    int moves_w = str_len(buf) * 6;
    int moves_x = cx + (cw - moves_w) / 2;
    vga13_draw_string(moves_x, cy + 8, buf, VGA13_BLACK, VGA13_WHITE, 0);

    if (puzzle_solved[id]) {
        const char *sol = "Solved";
        int sol_w = str_len(sol) * 6;
        int sol_x = cx + cw - sol_w - 8;
        if (sol_x < moves_x + moves_w + 4) sol_x = moves_x + moves_w + 4;
        if (sol_x + sol_w > cx + cw - 2) sol_x = cx + cw - sol_w - 2;
        vga13_draw_string(sol_x, cy + 8, sol, VGA13_BLACK, VGA13_WHITE, 0);
    }

    int header_h = 8 + 8 + 8;
    int board_max_h = ch - header_h - 8 - 12 - 8;
    if (board_max_h < 28) return;
    int board_size = (cw < board_max_h) ? cw : board_max_h;
    if (board_size > cw - 16) board_size = cw - 16;
    board_size -= board_size % 4;
    if (board_size < 32) return;

    int tile = board_size / 4;
    int bx = cx + (cw - board_size) / 2;
    int by = cy + header_h;

    vga13_fill_rect(bx, by, board_size, board_size, VGA13_WHITE);
    hline_px(bx, bx + board_size - 1, by, VGA13_BLACK);
    hline_px(bx, bx + board_size - 1, by + board_size - 1, VGA13_BLACK);
    vline_px(bx, by, by + board_size - 1, VGA13_BLACK);
    vline_px(bx + board_size - 1, by, by + board_size - 1, VGA13_BLACK);

    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            int idx = r * 4 + c;
            int val = puzzle_tiles[id][idx];

            int tx = bx + c * tile;
            int ty = by + r * tile;

            if (val == 0) {

                vga13_fill_rect(tx + 1, ty + 1, tile - 2, tile - 2, VGA13_WHITE);
                continue;
            }

            u8 tile_bg = puzzle_solved[id] ? PAL_DARK_GRAY : VGA13_WHITE;
            u8 tile_fg = puzzle_solved[id] ? VGA13_WHITE : VGA13_BLACK;

            vga13_fill_rect(tx + 1, ty + 1, tile - 2, tile - 2, tile_bg);
            hline_px(tx + 1, tx + tile - 2, ty + 1, VGA13_BLACK);
            hline_px(tx + 1, tx + tile - 2, ty + tile - 2, VGA13_BLACK);
            vline_px(tx + 1, ty + 1, ty + tile - 2, VGA13_BLACK);
            vline_px(tx + tile - 2, ty + 1, ty + tile - 2, VGA13_BLACK);

            char num[3];
            ksnprintf(num, sizeof(num), "%d", val);
            int nw = str_len(num) * 6;
            int nx = tx + (tile - nw) / 2;
            int ny = ty + (tile - 7) / 2;
            if (nx < tx + 1) nx = tx + 1;
            if (ny < ty + 1) ny = ty + 1;
            vga13_draw_string(nx, ny, num, tile_fg, tile_bg, 0);
        }
    }

    int btn_w = board_size;
    int btn_h = 12;
    int btn_x = bx;
    int btn_y = by + board_size + 8;

    vga13_fill_rect(btn_x + 1, btn_y + btn_h, btn_w - 1, 1, PAL_DARK_GRAY);
    vga13_fill_rect(btn_x + btn_w, btn_y + 1, 1, btn_h - 1, PAL_DARK_GRAY);
    vga13_put_pixel(btn_x + btn_w, btn_y + btn_h, PAL_DARK_GRAY);

    vga13_fill_rect(btn_x, btn_y, btn_w, btn_h, VGA13_WHITE);

    hline_px(btn_x, btn_x + btn_w - 1, btn_y, VGA13_BLACK);
    hline_px(btn_x, btn_x + btn_w - 1, btn_y + btn_h - 1, VGA13_BLACK);
    vline_px(btn_x, btn_y, btn_y + btn_h - 1, VGA13_BLACK);
    vline_px(btn_x + btn_w - 1, btn_y, btn_y + btn_h - 1, VGA13_BLACK);

    int label_len = 8;
    int label_x = btn_x + (btn_w - label_len * 6) / 2;
    int label_y = btn_y + (btn_h - 8) / 2;
    if (label_y < btn_y + 2) label_y = btn_y + 2;
    vga13_draw_string(label_x, label_y, "New Game", VGA13_BLACK, VGA13_WHITE, 0);
}

static void on_open(int id) {
    puzzle_moves[id] = 0;
    puzzle_empty_pos[id] = 15;
    puzzle_solved[id] = 0;
    puzzle_init(id);
}

static void draw(int id, int cx, int cy, int cw, int ch) {
    app_puzzle_draw_client(id, cx, cy, cw, ch);
}

static void on_click(int id, app_mouse_t *ev) {
    int cx = ev->cx, cy = ev->cy, cw = ev->cw, ch = ev->ch;
    int mx = ev->mx, my = ev->my;
    int header_h = 8 + 8;
    int board_max_h = ch - header_h - 8 - 12 - 8;
    if (board_max_h < 28) return;
    int board_size = (cw < board_max_h) ? cw : board_max_h;
    if (board_size > cw - 16) board_size = cw - 16;
    board_size -= board_size % 4;
    if (board_size < 32) return;
    int bx = cx + (cw - board_size) / 2;
    int by = cy + header_h;

    int btn_w = board_size, btn_h = 12;
    int btn_x = bx, btn_y = by + board_size + 8;
    if (mx >= btn_x && mx < btn_x + btn_w && my >= btn_y && my < btn_y + btn_h) {
        puzzle_init(id);
        desktop_needs_full_blit = 1;
        return;
    }

    if (mx < bx || mx >= bx + board_size || my < by || my >= by + board_size) return;
    {
        int tile = board_size / 4;
        int col = (mx - bx) / tile;
        int row = (my - by) / tile;
        int idx = row * 4 + col;
        if (puzzle_tiles[id][idx] != 0) {
            int empty = puzzle_empty_pos[id];
            int er = empty / 4, ec = empty % 4;
            int tr = idx / 4, tc = idx % 4;
            int manhattan = (er > tr ? er - tr : tr - er) + (ec > tc ? ec - tc : tc - ec);
            if (manhattan == 1) {
                puzzle_try_move_tile(id, idx);
                desktop_needs_full_blit = 1;
            }
        }
    }
}

extern const MenuItem menu_sav_items[];

static const MenuItem puzzle_game_items[] = {
    { "New Game", APP_NONE },
};
static Menu puzzle_menus[] = {
    { "@",    0, 0, menu_sav_items, SV_MENU_SAV_COUNT },
    { "Game", 0, 0, puzzle_game_items, 1 },
};

static int on_menu_action(int id, const char *label) {
    if (str_eq(label, "New Game")) {
        puzzle_init(id);
        desktop_needs_full_blit = 1;
        return 1;
    }
    return 0;
}

const app_desc_t app_puzzle_desc = {
    .kind           = APP_puzzle,
    .default_title  = "Puzzle",
    .def_x = 50, .def_y = 35, .def_w = 130, .def_h = 155,
    .on_open        = on_open,
    .draw           = draw,
    .on_key         = handle_puzzle_key,
    .on_click       = on_click,
    .menu_bar_menus = puzzle_menus,
    .menu_bar_count = 2,
    .on_menu_action = on_menu_action,
};
