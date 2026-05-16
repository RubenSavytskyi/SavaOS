#include "app_calc.h"
#include "sv_desktop_internal.h"
#include "sv_gfx.h"
#include "string.h"
#include "keyboard.h"

static const char icon[24][25] = {
    "........................",
    "........................",
    ".....BBBBBBBBBBBBB......",
    "....BWWWWWWWWWWWWWB.....",
    "....BWWBBBBBBBBBWWB.....",
    "....BWBWWWWWWWWWBWB.....",
    "....BWBWWWWWWWWWBWB.....",
    "....BWWBBBBBBBBBWWB.....",
    "....BWWWWWWWWWWWWWB.....",
    "....BWWWWWWWWWWWWWB.....",
    "....BWBBWBBWBBWBBWB.....",
    "....BWWWWWWWWWWWWWB.....",
    "....BWBBWBBWBBWBBWB.....",
    "....BWWWWWWWWWWWWWB.....",
    "....BWBBWBBWBBWBBWB.....",
    "....BWWWWWWWWWWWWWB.....",
    "....BWBBWBBWBBWBBWB.....",
    "....BWWWWWWWWWWWWWB.....",
    "....BWBBWBBWBBWBBWB.....",
    "....BWWWWWWWWWWWWWB.....",
    "....BWWWWWWWWWWWWWB.....",
    ".....BBBBBBBBBBBBB......",
    "........................",
    "........................"
};

#define CALC_DISPLAY_LEN 32
static char calc_display[MAX_WIN][CALC_DISPLAY_LEN];
static double calc_accumulator[MAX_WIN];
static double calc_current[MAX_WIN];
static char calc_operation[MAX_WIN];
static int calc_has_decimal[MAX_WIN];
static int calc_new_entry[MAX_WIN];
static int calc_error[MAX_WIN];
static int calc_decimal_places[MAX_WIN];
static int calc_left_decimal_places[MAX_WIN];
static double calc_left_operand[MAX_WIN];
static int calc_show_expression[MAX_WIN];
static int calc_expr_complete[MAX_WIN];

int calc_scientific[MAX_WIN];
static char calc_clipboard[CALC_DISPLAY_LEN];
static int  calc_clipboard_len = 0;

int calc_ctx_menu_open = 0;
int calc_ctx_menu_x = 0;
int calc_ctx_menu_y = 0;
int calc_ctx_menu_hover = -1;
#define CALC_CTX_MENU_ITEMS 4
static const char *calc_ctx_menu_labels[CALC_CTX_MENU_ITEMS] = { "Copy", "Paste", "Standard", "Scientific" };

int calc_default_scientific = 0;

static void calc_calculate(int id);

static void calc_init(int id) {
    if (id < 0 || id >= MAX_WIN) return;
    calc_display[id][0] = '0';
    calc_display[id][1] = 0;
    calc_scientific[id] = calc_default_scientific;
    calc_accumulator[id] = 0;
    calc_current[id] = 0;
    calc_operation[id] = 0;
    calc_has_decimal[id] = 0;
    calc_decimal_places[id] = 0;
    calc_left_decimal_places[id] = 0;
    calc_new_entry[id] = 1;
    calc_error[id] = 0;
    calc_left_operand[id] = 0;
    calc_show_expression[id] = 0;
    calc_expr_complete[id] = 0;
}

void calc_update_display(int id) {
    if (id < 0 || id >= MAX_WIN) return;
    if (calc_error[id]) {
        str_cpy(calc_display[id], "Error", CALC_DISPLAY_LEN);
        return;
    }

    auto void num_to_str(double val, char *buf, int max_len, int dec_places, int use_scientific) {
        if (use_scientific) {
            if (val == 0.0) {
                if (max_len > 1) { buf[0] = '0'; buf[1] = 0; }
                else if (max_len > 0) buf[0] = 0;
                return;
            }

            double absval = val < 0 ? -val : val;
            int exp = 0;

            while (absval >= 10.0 && exp < 99) { absval /= 10.0; exp++; }
            while (absval < 1.0 && exp > -99) { absval *= 10.0; exp--; }

            double mant = val < 0 ? -absval : absval;
            int prec = dec_places;
            if (prec <= 0) prec = 3;
            if (prec > 6) prec = 6;

            char mant_buf[32];
            num_to_str(mant, mant_buf, (int)sizeof(mant_buf), prec, 0);

            int pos = 0;
            for (int i = 0; mant_buf[i] && pos < max_len - 1; i++) buf[pos++] = mant_buf[i];
            if (pos < max_len - 1) buf[pos++] = 'E';

            if (pos < max_len - 1) buf[pos++] = (exp >= 0) ? '+' : '-';
            int eabs = exp >= 0 ? exp : -exp;

            if (eabs == 0) {
                if (pos < max_len - 1) buf[pos++] = '0';
            } else {
                char tmp[8];
                int tpos = 0;
                while (eabs > 0 && tpos < 7) {
                    tmp[tpos++] = (char)('0' + (eabs % 10));
                    eabs /= 10;
                }
                while (tpos > 0 && pos < max_len - 1) buf[pos++] = tmp[--tpos];
            }
            buf[pos] = 0;
            return;
        }

        if (dec_places == 0 && val == (int)val) {

            int v = (int)val;
            if (v < 0) v = -v;
            char temp[16];
            int pos = 0;
            if (v == 0) {
                temp[pos++] = '0';
            } else {
                while (v > 0 && pos < 15) {
                    temp[pos++] = '0' + (v % 10);
                    v /= 10;
                }
            }
            int i = 0;
            if (val < 0) buf[i++] = '-';
            for (int j = pos - 1; j >= 0 && i < max_len - 1; j--) {
                buf[i++] = temp[j];
            }
            buf[i] = 0;
        } else if (dec_places > 0) {

            long scaled, divisor = 1;
            int i = 0;

            for (int d = 0; d < dec_places; d++) divisor *= 10;

            double absval = val < 0 ? -val : val;

            absval += 0.000001;
            scaled = (long)((absval * divisor) + 0.5);

            long int_part = scaled / divisor;
            long dec_part = scaled % divisor;

            if (val < 0) buf[i++] = '-';

            if (int_part == 0) {
                buf[i++] = '0';
            } else {
                char temp[16];
                int pos = 0;
                long ip = int_part;
                while (ip > 0 && pos < 15) {
                    temp[pos++] = '0' + (ip % 10);
                    ip /= 10;
                }
                while (pos > 0 && i < max_len - 1) {
                    buf[i++] = temp[--pos];
                }
            }

            if (i < max_len - 1) buf[i++] = '.';

            long pad = divisor / 10;
            while (pad > 0 && dec_part < pad && i < max_len - 1) {
                buf[i++] = '0';
                pad /= 10;
            }

            if (dec_part == 0) {
                if (i < max_len - 1) buf[i++] = '0';
            } else {
                char temp[8];
                int pos = 0;
                long dp = dec_part;
                while (dp > 0 && pos < 7) {
                    temp[pos++] = '0' + (dp % 10);
                    dp /= 10;
                }
                while (pos > 0 && i < max_len - 1) {
                    buf[i++] = temp[--pos];
                }
            }
            buf[i] = 0;
        } else {

            int v = (int)val;
            if (v < 0) v = -v;
            char temp[16];
            int pos = 0;
            if (v == 0) {
                temp[pos++] = '0';
            } else {
                while (v > 0 && pos < 15) {
                    temp[pos++] = '0' + (v % 10);
                    v /= 10;
                }
            }
            int i = 0;
            if (val < 0) buf[i++] = '-';
            for (int j = pos - 1; j >= 0 && i < max_len - 1; j--) {
                buf[i++] = temp[j];
            }
            buf[i] = 0;
        }
    }

    char left_str[16] = {0};
    char right_str[16] = {0};
    char result_str[16] = {0};

    int sci = calc_scientific[id] ? 1 : 0;
    if (calc_expr_complete[id]) {

        num_to_str(calc_left_operand[id], left_str, 16, calc_left_decimal_places[id], sci);
        num_to_str(calc_current[id], right_str, 16, calc_decimal_places[id], sci);
        num_to_str(calc_current[id], result_str, 16, calc_decimal_places[id], sci);

        int i = 0;

        for (int j = 0; left_str[j] && i < CALC_DISPLAY_LEN - 1; j++)
            calc_display[id][i++] = left_str[j];

        if (i < CALC_DISPLAY_LEN - 1) calc_display[id][i++] = ' ';

        if (i < CALC_DISPLAY_LEN - 1) calc_display[id][i++] = calc_operation[id];

        if (i < CALC_DISPLAY_LEN - 1) calc_display[id][i++] = ' ';

        for (int j = 0; right_str[j] && i < CALC_DISPLAY_LEN - 1; j++)
            calc_display[id][i++] = right_str[j];

        if (i < CALC_DISPLAY_LEN - 3) {
            calc_display[id][i++] = ' ';
            calc_display[id][i++] = '=';
            calc_display[id][i++] = ' ';
        }

        for (int j = 0; result_str[j] && i < CALC_DISPLAY_LEN - 1; j++)
            calc_display[id][i++] = result_str[j];
        calc_display[id][i] = 0;

    } else if (calc_show_expression[id] && calc_operation[id]) {

        num_to_str(calc_left_operand[id], left_str, 16, calc_left_decimal_places[id], sci);
        num_to_str(calc_current[id], right_str, 16, calc_decimal_places[id], sci);

        int i = 0;

        for (int j = 0; left_str[j] && i < CALC_DISPLAY_LEN - 1; j++)
            calc_display[id][i++] = left_str[j];

        if (i < CALC_DISPLAY_LEN - 1) calc_display[id][i++] = ' ';

        if (i < CALC_DISPLAY_LEN - 1) calc_display[id][i++] = calc_operation[id];

        if (i < CALC_DISPLAY_LEN - 1) calc_display[id][i++] = ' ';

        for (int j = 0; right_str[j] && i < CALC_DISPLAY_LEN - 1; j++)
            calc_display[id][i++] = right_str[j];
        calc_display[id][i] = 0;

    } else {

        num_to_str(calc_current[id], calc_display[id], CALC_DISPLAY_LEN, calc_decimal_places[id], sci);
    }
}

void calc_input_digit(int id, int digit) {
    if (id < 0 || id >= MAX_WIN) return;
    if (calc_error[id]) {
        calc_error[id] = 0;
        calc_current[id] = 0;
        calc_has_decimal[id] = 0;
        calc_decimal_places[id] = 0;
    }
    if (calc_new_entry[id]) {
        calc_current[id] = digit;
        calc_new_entry[id] = 0;
        calc_has_decimal[id] = 0;
        calc_decimal_places[id] = 0;
    } else {
        if (calc_has_decimal[id]) {

            calc_decimal_places[id]++;
            double decimal_factor = 1.0;
            for (int i = 0; i < calc_decimal_places[id]; i++) {
                decimal_factor *= 0.1;
            }
            calc_current[id] = calc_current[id] + digit * decimal_factor;
        } else {

            calc_current[id] = calc_current[id] * 10 + digit;
        }
    }
    calc_expr_complete[id] = 0;
    calc_update_display(id);
}

void calc_input_decimal(int id) {
    if (id < 0 || id >= MAX_WIN) return;
    if (calc_error[id]) return;
    if (calc_new_entry[id]) {
        calc_current[id] = 0;
        calc_new_entry[id] = 0;
    }
    calc_has_decimal[id] = 1;
    calc_decimal_places[id] = 0;
}

void calc_clear(int id) {
    if (id < 0 || id >= MAX_WIN) return;
    calc_current[id] = 0;
    calc_accumulator[id] = 0;
    calc_operation[id] = 0;
    calc_has_decimal[id] = 0;
    calc_decimal_places[id] = 0;
    calc_left_decimal_places[id] = 0;
    calc_new_entry[id] = 1;
    calc_error[id] = 0;
    calc_left_operand[id] = 0;
    calc_show_expression[id] = 0;
    calc_expr_complete[id] = 0;
    calc_update_display(id);
}

void calc_clear_entry(int id) {
    if (id < 0 || id >= MAX_WIN) return;

    calc_current[id] = 0;
    calc_has_decimal[id] = 0;
    calc_decimal_places[id] = 0;
    calc_new_entry[id] = 1;
    calc_error[id] = 0;
    calc_update_display(id);
}

void calc_set_operation(int id, char op) {
    if (id < 0 || id >= MAX_WIN) return;
    if (calc_error[id]) return;

    if (calc_operation[id] && !calc_new_entry[id]) {
        calc_calculate(id);
    }
    calc_accumulator[id] = calc_current[id];
    calc_left_operand[id] = calc_current[id];
    calc_left_decimal_places[id] = calc_decimal_places[id];
    calc_operation[id] = op;
    calc_new_entry[id] = 1;
    calc_has_decimal[id] = 0;
    calc_decimal_places[id] = 0;
    calc_show_expression[id] = 1;
    calc_expr_complete[id] = 0;
}

static void calc_calculate(int id) {
    if (id < 0 || id >= MAX_WIN) return;
    if (!calc_operation[id]) return;

    switch (calc_operation[id]) {
        case '+':
            calc_current[id] = calc_accumulator[id] + calc_current[id];
            break;
        case '-':
            calc_current[id] = calc_accumulator[id] - calc_current[id];
            break;
        case '*':
            calc_current[id] = calc_accumulator[id] * calc_current[id];
            break;
        case '/':
            if (calc_current[id] == 0) {
                calc_error[id] = 1;
            } else {
                calc_current[id] = calc_accumulator[id] / calc_current[id];
            }
            break;
    }
    calc_operation[id] = 0;
    calc_new_entry[id] = 1;
    calc_has_decimal[id] = 0;
    calc_decimal_places[id] = 0;
    calc_left_decimal_places[id] = 0;
    calc_accumulator[id] = calc_current[id];
    calc_show_expression[id] = 0;
    calc_expr_complete[id] = 0;
    calc_update_display(id);
}

void calc_equals(int id) {
    if (id < 0 || id >= MAX_WIN) return;
    if (calc_error[id]) return;

    calc_expr_complete[id] = 1;
    calc_calculate(id);
}

static void handle_calc_key(int id, int key) {
    if (id < 0 || id >= MAX_WIN || wins[id].app != APP_calc) return;

    if (key >= '0' && key <= '9') {
        calc_input_digit(id, key - '0');
        desktop_needs_full_blit = 1;
    } else if (key == '.' || key == ',') {
        calc_input_decimal(id);
        desktop_needs_full_blit = 1;
    } else if (key == '+') {
        calc_set_operation(id, '+');
        desktop_needs_full_blit = 1;
    } else if (key == '-') {
        calc_set_operation(id, '-');
        desktop_needs_full_blit = 1;
    } else if (key == '*') {
        calc_set_operation(id, '*');
        desktop_needs_full_blit = 1;
    } else if (key == '/') {
        calc_set_operation(id, '/');
        desktop_needs_full_blit = 1;
    } else if (key == '=' || key == KEY_ENTER) {
        calc_equals(id);
        desktop_needs_full_blit = 1;
    } else if (key == 'c' || key == 'C' || key == KEY_ESC) {
        calc_clear(id);
        desktop_needs_full_blit = 1;
    }
}

void app_calc_draw_client(int id, int cx, int cy, int cw, int ch) {

    int btn_w = 16;
    int btn_h = 12;
    int gap = 3;
    int display_h = 16;
    int margin = 6;
    int start_x = cx + margin;
    int start_y = cy + margin;
    int i, r, c;
    char display_buf[16];
    int display_len;

    int btn_area_y = start_y + display_h + 6;

    int total_btn_w = 4 * btn_w + 3 * gap;
    (void)total_btn_w;

    vga13_fill_rect(cx, cy, cw, ch, VGA13_WHITE);
    for (int py = cy; py < cy + ch; py += 2) {
        for (int px = cx + ((py - cy) & 1); px < cx + cw; px += 2) {
            vga13_put_pixel(px, py, PAL_DARK_GRAY);
        }
    }

    int disp_w = total_btn_w;
    vga13_fill_rect(start_x, start_y, disp_w, display_h, VGA13_WHITE);
    hline_px(start_x, start_x + disp_w - 1, start_y, VGA13_BLACK);
    hline_px(start_x, start_x + disp_w - 1, start_y + display_h - 1, VGA13_BLACK);
    vline_px(start_x, start_y, start_y + display_h - 1, VGA13_BLACK);
    vline_px(start_x + disp_w - 1, start_y, start_y + display_h - 1, VGA13_BLACK);

    if (id >= 0 && id < MAX_WIN) {
        display_len = str_len(calc_display[id]);
        if (display_len > 11) display_len = 11;
        for (i = 0; i < display_len; i++) display_buf[i] = calc_display[id][i];
        display_buf[i] = 0;
    } else {
        display_buf[0] = '0'; display_buf[1] = 0;
    }
    int text_w = str_len(display_buf) * 6;
    int text_x = start_x + disp_w - 4 - text_w;
    if (text_x < start_x + 2) text_x = start_x + 2;
    vga13_draw_string(text_x, start_y + 5, display_buf, VGA13_BLACK, VGA13_WHITE, 0);

    const char *btn_labels[20] = {
        "C", "E", "=", "*",
        "7", "8", "9", "/",
        "4", "5", "6", "-",
        "1", "2", "3", "+",
        "0", "", ".",  ""
    };

    for (r = 0; r < 5; r++) {
        for (c = 0; c < 4; c++) {
            int bx = start_x + c * (btn_w + gap);
            int by = btn_area_y + r * (btn_h + gap);
            int bw = btn_w;
            int bh = btn_h;
            char label[2] = { btn_labels[r*4+c][0], 0 };

            if (r == 4 && c == 0) {
                bw = btn_w * 2 + gap;
            }

            else if (r == 4 && c == 1) {
                continue;
            }

            else if (r == 3 && c == 3) {
                bh = btn_h * 2 + gap;
            }

            else if (r == 4 && c == 3) {
                continue;
            }

            else if (btn_labels[r*4+c][0] == 0) {
                continue;
            }

            vga13_fill_rect(bx + 1, by + bh, bw - 1, 1, PAL_DARK_GRAY);
            vga13_fill_rect(bx + bw, by + 1, 1, bh - 1, PAL_DARK_GRAY);
            vga13_put_pixel(bx + bw, by + bh, PAL_DARK_GRAY);

            vga13_fill_rect(bx, by, bw, bh, VGA13_WHITE);

            hline_px(bx, bx + bw - 1, by, VGA13_BLACK);
            hline_px(bx, bx + bw - 1, by + bh - 1, VGA13_BLACK);
            vline_px(bx, by, by + bh - 1, VGA13_BLACK);
            vline_px(bx + bw - 1, by, by + bh - 1, VGA13_BLACK);

            int label_x = bx + (bw - 6) / 2;
            int label_y = by + (bh - 8) / 2;
            if (label_y < by + 2) label_y = by + 2;
            vga13_draw_string(label_x, label_y, label, VGA13_BLACK, VGA13_WHITE, 0);
        }
    }
}

void calc_copy_last_number_to_clipboard(int id) {
    if (id < 0 || id >= MAX_WIN || wins[id].app != APP_calc) return;
    calc_clipboard_len = 0;
    calc_clipboard[0] = 0;

    const char *disp = calc_display[id];
    const char *eq = 0;
    for (int i = 0; disp[i]; i++) {
        if (disp[i] == '=') { eq = &disp[i]; break; }
    }

    const char *src = eq ? (eq + 1) : disp;
    while (*src == ' ') src++;

    int len = str_len(src);
    if (len >= CALC_DISPLAY_LEN) len = CALC_DISPLAY_LEN - 1;
    for (int i = 0; i < len; i++) calc_clipboard[i] = src[i];
    calc_clipboard[len] = 0;
    calc_clipboard_len = len;
}

static double calc_parse_double(const char *s) {

    if (!s) return 0.0;
    int i = 0;
    while (s[i] == ' ') i++;

    int neg = 0;
    if (s[i] == '-') { neg = 1; i++; }
    else if (s[i] == '+') { i++; }

    double val = 0.0;
    while (s[i] >= '0' && s[i] <= '9') {
        val = val * 10.0 + (double)(s[i] - '0');
        i++;
    }

    if (s[i] == '.') {
        i++;
        double base = 0.1;
        while (s[i] >= '0' && s[i] <= '9') {
            val += (double)(s[i] - '0') * base;
            base *= 0.1;
            i++;
        }
    }

    if (s[i] == 'e' || s[i] == 'E') {
        i++;
        int exp_neg = 0;
        if (s[i] == '-') { exp_neg = 1; i++; }
        else if (s[i] == '+') { i++; }

        int exp = 0;
        while (s[i] >= '0' && s[i] <= '9') {
            exp = exp * 10 + (s[i] - '0');
            i++;
        }

        double scale = 1.0;
        int step = exp_neg ? -exp : exp;
        if (step > 0) {
            for (int k = 0; k < step; k++) scale *= 10.0;
        } else if (step < 0) {
            for (int k = 0; k < -step; k++) scale /= 10.0;
        }
        val *= scale;
    }

    return neg ? -val : val;
}

void calc_paste_clipboard_to_calc(int id) {
    if (id < 0 || id >= MAX_WIN || wins[id].app != APP_calc) return;
    if (calc_clipboard_len <= 0) return;

    calc_accumulator[id] = 0;
    calc_current[id] = calc_parse_double(calc_clipboard);
    calc_operation[id] = 0;
    calc_error[id] = 0;

    calc_has_decimal[id] = 0;
    calc_decimal_places[id] = 0;

    const char *p = calc_clipboard;
    while (*p == ' ') p++;

    const char *dot = 0;
    for (int k = 0; p[k]; k++) {
        if (p[k] == '.') { dot = &p[k]; break; }
        if (p[k] == 'e' || p[k] == 'E') break;
    }
    if (dot) {
        calc_has_decimal[id] = 1;
        int cnt = 0;
        for (int k = 1; dot[k] && cnt < 6; k++) {
            if (dot[k] >= '0' && dot[k] <= '9') cnt++;
            else break;
        }
        calc_decimal_places[id] = cnt;
    }

    calc_new_entry[id] = 1;
    calc_left_decimal_places[id] = 0;
    calc_left_operand[id] = 0;
    calc_show_expression[id] = 0;
    calc_expr_complete[id] = 0;

    calc_update_display(id);
    desktop_needs_full_blit = 1;
}

int calc_ctx_menu_width(void) {
    int i, w = 70;
    for (i = 0; i < CALC_CTX_MENU_ITEMS; i++) {
        int tw = str_len(calc_ctx_menu_labels[i]) * 6 + 16;
        if (tw > w) w = tw;
    }
    return w;
}

void calc_draw_context_menu(void) {
    int i, w, h;
    if (!calc_ctx_menu_open) return;
    w = calc_ctx_menu_width();
    h = CALC_CTX_MENU_ITEMS * 11 + 6;

    if (calc_ctx_menu_x + w > VGA13_WIDTH) calc_ctx_menu_x = VGA13_WIDTH - w;
    if (calc_ctx_menu_y + h > VGA13_HEIGHT) calc_ctx_menu_y = VGA13_HEIGHT - h;

    vga13_fill_rect(calc_ctx_menu_x + 1, calc_ctx_menu_y + h, w, 1, PAL_DARK_GRAY);
    vga13_fill_rect(calc_ctx_menu_x + w, calc_ctx_menu_y + 1, 1, h, PAL_DARK_GRAY);
    vga13_fill_rect(calc_ctx_menu_x, calc_ctx_menu_y, w, h, VGA13_WHITE);

    hline_px(calc_ctx_menu_x, calc_ctx_menu_x + w - 1, calc_ctx_menu_y, VGA13_BLACK);
    hline_px(calc_ctx_menu_x, calc_ctx_menu_x + w - 1, calc_ctx_menu_y + h - 1, VGA13_BLACK);
    vline_px(calc_ctx_menu_x, calc_ctx_menu_y, calc_ctx_menu_y + h - 1, VGA13_BLACK);
    vline_px(calc_ctx_menu_x + w - 1, calc_ctx_menu_y, calc_ctx_menu_y + h - 1, VGA13_BLACK);

    for (i = 0; i < CALC_CTX_MENU_ITEMS; i++) {
        int iy = calc_ctx_menu_y + 2 + i * 11;
        int inv = (i == calc_ctx_menu_hover);
        if (inv) vga13_fill_rect(calc_ctx_menu_x + 2, iy - 1, w - 4, 10, VGA13_BLACK);
        vga13_draw_string(calc_ctx_menu_x + 8, iy, calc_ctx_menu_labels[i], VGA13_BLACK, VGA13_WHITE, inv);
    }
}

int calc_hit_context_menu(int mx, int my); 

int calc_hit_context_menu(int mx, int my) {
    int w, h;
    if (!calc_ctx_menu_open) return -1;
    w = calc_ctx_menu_width();
    h = CALC_CTX_MENU_ITEMS * 11 + 6;
    if (mx < calc_ctx_menu_x || mx >= calc_ctx_menu_x + w || my < calc_ctx_menu_y || my >= calc_ctx_menu_y + h) return -1;
    return (my - calc_ctx_menu_y - 3) / 11;
}

static void calc_ctx_hover_update(int mx, int my);

static void calc_context_menu_click(int mx, int my, int win_id) {
    int item = calc_hit_context_menu(mx, my);
    if (win_id < 0 || wins[win_id].app != APP_calc) {
        calc_ctx_menu_open = 0;
        calc_ctx_menu_hover = -1;
        desktop_needs_full_blit = 1;
        return;
    }

    if (item == 0) calc_copy_last_number_to_clipboard(win_id);
    else if (item == 1) calc_paste_clipboard_to_calc(win_id);
    else if (item == 2) { calc_scientific[win_id] = 0; calc_update_display(win_id); }
    else if (item == 3) { calc_scientific[win_id] = 1; calc_update_display(win_id); }

    calc_ctx_menu_open = 0;
    calc_ctx_menu_hover = -1;
    desktop_needs_full_blit = 1;
}

static void on_open(int id) {
    calc_init(id);
}

static void draw(int id, int cx, int cy, int cw, int ch) {
    app_calc_draw_client(id, cx, cy, cw, ch);
}

void calc_set_scientific_all(int enable) {
    int i;
    calc_default_scientific = enable;
    for (i = 0; i < MAX_WIN; i++) {
        if (wins[i].used && wins[i].app == APP_calc) {
            calc_scientific[i] = enable;
            calc_update_display(i);
        }
    }
    desktop_needs_full_blit = 1;
}

static int overlay_is_open(void);

static int overlay_is_open(void) { return calc_ctx_menu_open; }
static void close_overlay(void) { calc_ctx_close(); }

#include "sv_desktop_internal.h"

static void on_click(int id, app_mouse_t *ev) {
    int cx = ev->cx, cy = ev->cy;
    int mx = ev->mx, my = ev->my;
    int btn_w = 16, btn_h = 12, gap = 3;
    int display_h = 16, margin = 6;
    int start_x = cx + margin;
    int start_y = cy + margin;
    int btn_area_y = start_y + display_h + 6;

    if (mx < start_x || mx >= start_x + 4*(btn_w+gap) ||
        my < btn_area_y || my >= btn_area_y + 5*(btn_h+gap)) return;

    int col = (mx - start_x) / (btn_w + gap);
    int row = (my - btn_area_y) / (btn_h + gap);
    if (col < 0) col = 0;
    if (col > 3) col = 3;
    if (row < 0) row = 0;
    if (row > 4) row = 4;

    char btn = 0;
    if (row == 0) {
        char row0[4] = {'C','E','=','*'}; btn = row0[col];
    } else if (row == 1) {
        char row1[4] = {'7','8','9','/'}; btn = row1[col];
    } else if (row == 2) {
        char row2[4] = {'4','5','6','-'}; btn = row2[col];
    } else if (row == 3) {
        if (col == 3) btn = '+';
        else { char row3[3] = {'1','2','3'}; btn = row3[col]; }
    } else if (row == 4) {
        if (col <= 1) btn = '0';
        else if (col == 2) btn = '.';
    }

    if (btn >= '0' && btn <= '9') calc_input_digit(id, btn - '0');
    else if (btn == '.') calc_input_decimal(id);
    else if (btn == 'C') calc_clear(id);
    else if (btn == 'E') calc_clear_entry(id);
    else if (btn == '=') calc_equals(id);
    else if (btn == '+') calc_set_operation(id, '+');
    else if (btn == '-') calc_set_operation(id, '-');
    else if (btn == '*') calc_set_operation(id, '*');
    else if (btn == '/') calc_set_operation(id, '/');
    desktop_needs_full_blit = 1;
}

static void on_right_click(int id, app_mouse_t *ev) {
    (void)id;
    calc_ctx_menu_x = ev->mx;
    calc_ctx_menu_y = ev->my;
    calc_ctx_open(ev->mx, ev->my);
    desktop_needs_full_blit = 1;
}

extern const MenuItem menu_sav_items[];

static const MenuItem calc_edit_items[] = {
    { "Copy",  APP_NONE },
    { "Paste", APP_NONE }
};
static const MenuItem calc_view_items[] = {
    { "Standard",  APP_NONE },
    { "Scientific", APP_NONE }
};

static Menu calc_menus[] = {
    { "@",    0, 0, menu_sav_items, SV_MENU_SAV_COUNT },
    { "Edit", 0, 0, calc_edit_items, 2 },
    { "View", 0, 0, calc_view_items, 2 }
};
#define CALC_MENU_COUNT 3

static int on_menu_action(int id, const char *label) {
    if (str_eq(label, "Copy"))      { calc_copy_last_number_to_clipboard(id); return 1; }
    if (str_eq(label, "Paste"))     { calc_paste_clipboard_to_calc(id); return 1; }
    if (str_eq(label, "Standard"))  { calc_scientific[id] = 0; calc_update_display(id); desktop_needs_full_blit = 1; return 1; }
    if (str_eq(label, "Scientific")){ calc_scientific[id] = 1; calc_update_display(id); desktop_needs_full_blit = 1; return 1; }
    return 0;
}

const app_desc_t app_calc_desc = {
    .kind           = APP_calc,
    .default_title  = "Calc",
    .def_x = 85, .def_y = 40, .def_w = 95, .def_h = 125,
    .icon_bmp       = icon,
    .on_open        = on_open,
    .draw           = draw,
    .on_key         = handle_calc_key,
    .on_click       = on_click,
    .on_right_click = on_right_click,
    .draw_overlay   = calc_draw_context_menu,
    .hit_overlay    = calc_hit_context_menu,
    .hover_overlay  = calc_ctx_hover_update,
    .overlay_is_open = overlay_is_open,
    .close_overlay  = close_overlay,
    .click_overlay  = calc_context_menu_click,
    .menu_bar_menus = calc_menus,
    .menu_bar_count = CALC_MENU_COUNT,
    .on_menu_action = on_menu_action,
};

void calc_ctx_open(int x, int y) {
    calc_ctx_menu_x = x; calc_ctx_menu_y = y;
    calc_ctx_menu_open = 1; calc_ctx_menu_hover = -1;
}
void calc_ctx_close(void) { calc_ctx_menu_open = 0; calc_ctx_menu_hover = -1; }
void calc_ctx_hover_update(int mx, int my) {
    calc_ctx_menu_hover = calc_hit_context_menu(mx, my);
}
