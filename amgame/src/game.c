// L0 uses only TRM(putch, halt) and IOE(timer, keyboard, GPU). Image builds
// with -mno-sse
#include <game.h>
#include "amdev.h"

#define FPS      30
#define FRAME_US (1000000 / FPS)
#define MAX_W    2048

#define MARGIN 0x303030
#define BLACK  0x000000
#define HEAD   0xffa500

#define COLS   32
#define ROWS   24
#define PERIOD 6

typedef struct {
    // column, row
    int x, y;
} Cell;

typedef enum {
    DIR_UP, DIR_RIGHT, DIR_DOWN, DIR_LEFT
} Dir;

typedef struct {
    Cell head;
    Dir dir;
    int timer;
} Game;
static Game game;

static int W, H;
static uint32_t row_buf[MAX_W];
static int cell_sz, org_x, org_y;
static const int DX[4] = {0, 1, 0, -1};
static const int DY[4] = {-1, 0, 1, 0};

// The trailing comma matters: AM_KEYS pastes KEYNAME(k) entries side by side
// with no separator, so the comma has to come from KEYNAME itself.
#define KEYNAME(k) [AM_KEY_##k] = #k,
static const char* key_names[] = {[AM_KEY_NONE] = "NONE", AM_KEYS(KEYNAME)};
static const char* key_name(int code) {
    return (code >= 0 && code < (int)LENGTH(key_names)) ? key_names[code] : "?";
}

static int key_to_dir(int key) {
    switch(key) {
    case AM_KEY_UP: case AM_KEY_W:
        return DIR_UP;

    case AM_KEY_RIGHT: case AM_KEY_D:
        return DIR_RIGHT;
    case AM_KEY_DOWN: case AM_KEY_S:
        return DIR_DOWN;
    case AM_KEY_LEFT: case AM_KEY_A:
        return DIR_LEFT;
    default:
        return -1;
    }
}

static void frame_sync(void) {
    io_write(AM_GPU_FBDRAW, 0, 0, NULL, 0, 0, true);
}

static void fill_rect(int x, int y, int w, int h, uint32_t color) {
    // cliff a Ball
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {

        h += y;
        y = 0;
    }
    if (x + w > W) w = W - x;
    if (y + h > H) h = H - y;

    if (w <= 0 || h <= 0) return;
    for (int i = 0; i < w; ++i)
        row_buf[i] = color;
    // one row per call
    for (int j = 0; j < h; ++j)
        io_write(AM_GPU_FBDRAW, x, y + j, row_buf, w, 1, false);
}

static void grid_init() {
    cell_sz = (W / COLS < H / ROWS) ? W / COLS : H / ROWS;
    if (cell_sz < 2)
        cell_sz = 2;
    org_x = (W - cell_sz * COLS) / 2;
    org_y = (H - cell_sz * ROWS) / 2;
}

static void draw_cell(Cell c, uint32_t color) {
    int gap = cell_sz >= 6 ? 1 : 0;
    fill_rect(org_x + c.x * cell_sz, org_y + c.y * cell_sz, cell_sz - gap,
              cell_sz - gap, color);
}

static void draw_board(void) {
    fill_rect(0, 0, W, H, MARGIN);
    fill_rect(org_x, org_y, COLS * cell_sz, ROWS * cell_sz, BLACK);
}

static void new_game(Game* g) {
    g->head  = (Cell){COLS / 4, ROWS / 2};
    g->dir   = DIR_RIGHT;
    g->timer = 0;
    draw_board();
    draw_cell(g->head, HEAD);
    frame_sync();
}

static void snake_step(Game* g) {
    draw_cell(g->head, BLACK);
    g->head.x = (g->head.x + DX[g->dir] + COLS) % COLS;
    g->head.y = (g->head.y + DY[g->dir] + ROWS) % ROWS;
    draw_cell(g->head, HEAD);
}

static void update(Game* g) {
    if(++g->timer < PERIOD)
        return;
    g->timer = 0;
    snake_step(g);
}

static void on_key(Game* g, int key) {
    (void)g;
    if(key == AM_KEY_ESCAPE)
        halt(0);
    int d = key_to_dir(key);
    if (d >= 0)
        g->dir = d;
}

// Operating system is a C program!
int main(const char* args) {
    ioe_init();

    AM_GPU_CONFIG_T gpu = io_read(AM_GPU_CONFIG);
    W                   = gpu.width < MAX_W ? gpu.width : MAX_W;
    H                   = gpu.height;

    printf("mainargs=\"%s\" screen=%dx%d\n", args, W, H);  // make run mainargs=xxx

    grid_init();
    new_game(&game);

    uint32_t frames = 0;
    uint64_t next = io_read(AM_TIMER_UPTIME).us, fps_t0 = next;

    while (1) {
        uint64_t now;
        while ((now = io_read(AM_TIMER_UPTIME).us) < next)
            ;
        next += FRAME_US;
        if (now > next + 10 * FRAME_US) next = now + FRAME_US;

        for (int n = 0; n < 64; ++n) {
            AM_INPUT_KEYBRD_T ev = io_read(AM_INPUT_KEYBRD);

            if (ev.keycode == AM_KEY_NONE) break;
            if (!ev.keydown) continue;

            printf("key %s\n", key_name(ev.keycode));
            on_key(&game, ev.keycode);
        }

        update(&game);
        frame_sync();


        frames++;
        if (now - fps_t0 >= 1000000) {
            printf("fps=%u\n", frames);  // frames is uint32_t: %u, not %d
            frames = 0;
            fps_t0 = now;
        }
    }

    return 0;
}
