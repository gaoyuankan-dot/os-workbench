#include <game.h>

// L0: a minimal single-player Pong running on AbstractMachine (TRM + IOE only).
//
// Portability notes (the "two word lengths" requirement):
//   * integer-only physics -- no float/double (forbidden on non-native via -mno-sse)
//   * screen size is queried at runtime, never hardcoded (the OJ changes resolution)
//   * no pointer<->int casts that assume sizeof(int)==sizeof(void*)

#define FPS          60
#define FRAME_US     (1000000 / FPS)   // microseconds per frame
#define BALL_SIDE    10
#define PADDLE_W     80
#define PADDLE_H     12
#define PADDLE_STEP  24                 // pixels moved per left/right key press
#define BALL_SPEED   300               // pixels per second
#define LIVES        3

// colors are 0x00RRGGBB
#define COL_BG     0x00000000
#define COL_BALL   0x00ffffff
#define COL_PADDLE 0x0000ff00
#define COL_OVER   0x00ff0000

static int W, H;                  // screen size, queried at runtime
static int pad_x, pad_y;          // paddle position
static int bx, by, bvx, bvy;      // ball position / velocity (px, px/s)
static int lives, game_over;

// draw a solid w x h rectangle of `color` with its top-left at (x, y)
static void draw_rect(int x, int y, int w, int h, uint32_t color) {
  uint32_t pixels[w * h];
  AM_GPU_FBDRAW_T e = {
    .x = x, .y = y, .w = w, .h = h, .sync = 1, .pixels = pixels,
  };
  for (int i = 0; i < w * h; i++) pixels[i] = color;
  ioe_write(AM_GPU_FBDRAW, &e);
}

static void clear_screen(void) {
  for (int y = 0; y < H; y += 32)
    for (int x = 0; x < W; x += 32)
      draw_rect(x, y, 32, 32, COL_BG);
}

static void reset_ball(void) {
  bx = W / 2; by = H / 2;
  bvx = BALL_SPEED * 2 / 3;       // a slight angle
  bvy = BALL_SPEED;               // initially moving down
  pad_x = W / 2 - PADDLE_W / 2;
}

static void draw_scene(void) {
  clear_screen();
  draw_rect(pad_x, pad_y, PADDLE_W, PADDLE_H, COL_PADDLE);
  draw_rect(bx, by, BALL_SIDE, BALL_SIDE, game_over ? COL_OVER : COL_BALL);
}

static void kbd_event(AM_INPUT_KEYBRD_T ev) {
  if (!ev.keydown) return;
  if (ev.keycode == AM_KEY_ESCAPE) halt(0);     // ESC -> exit, the only allowed halt
  if (game_over) { game_over = 0; lives = LIVES; reset_ball(); return; }
  if (ev.keycode == AM_KEY_LEFT)  pad_x -= PADDLE_STEP;
  if (ev.keycode == AM_KEY_RIGHT) pad_x += PADDLE_STEP;
  if (pad_x < 0) pad_x = 0;
  if (pad_x > W - PADDLE_W) pad_x = W - PADDLE_W;
}

static void physics_step(void) {
  if (game_over) return;
  bx += bvx * FRAME_US / 1000000;
  by += bvy * FRAME_US / 1000000;

  if (bx < 0)             { bx = 0;              bvx = -bvx; }
  if (bx > W - BALL_SIDE) { bx = W - BALL_SIDE;  bvx = -bvx; }
  if (by < 0)             { by = 0;              bvy = -bvy; }

  // paddle bounce: ball coming down and overlapping the paddle
  if (bvy > 0 &&
      by + BALL_SIDE >= pad_y && by <= pad_y + PADDLE_H &&
      bx + BALL_SIDE >= pad_x && bx <= pad_x + PADDLE_W) {
    by = pad_y - BALL_SIDE;
    bvy = -bvy;
  } else if (by > H) {        // missed the paddle
    lives--;
    if (lives <= 0) { game_over = 1; bx = W / 2 - BALL_SIDE / 2; by = H / 2 - BALL_SIDE / 2; }
    else reset_ball();
  }
}

int main(const char *args) {
  (void)args;
  ioe_init();

  AM_GPU_CONFIG_T cfg = {0};
  ioe_read(AM_GPU_CONFIG, &cfg);
  W = cfg.width;
  H = cfg.height;
  pad_y = H - PADDLE_H - 8;

  lives = LIVES;
  game_over = 0;
  reset_ball();

  puts("L0 Pong. ESC=quit, <-/-> move paddle.\n");

  uint64_t next_frame = io_read(AM_TIMER_UPTIME).us;
  while (1) {
    while (io_read(AM_TIMER_UPTIME).us < next_frame) ;     // wait for the frame
    AM_INPUT_KEYBRD_T ev;
    while ((ev = io_read(AM_INPUT_KEYBRD)).keycode != AM_KEY_NONE)
      kbd_event(ev);
    physics_step();
    draw_scene();
    next_frame += FRAME_US;
  }
  return 0;   // unreachable; the game exits via halt(0) on ESC
}