#include <game.h>

// L0: a minimal game running on AbstractMachine (TRM + IOE only).
//
// Portability notes (the "two word lengths" requirement):
//   * different stack layout and args pass to 32/64 bit
//   * integer-only physics -- no float/double (forbidden on non-native via
//   -mno-sse)
//   * screen size is queried at runtime, never hardcoded (the OJ changes
//   resolution)
//   * no pointer<->int casts that assume sizeof(int)==sizeof(void*)

#define FPS 90
#define FRAME_US (1000000 / FPS)  // microseconds per frame
#define BALL_SIDE 10
#define BALL_SPEED 300  // pixels per second
#define PADDLE_W 80
#define PADDLE_H 12
#define PADDLE_STEP 24  // pixels moved per left/right key press
#define LIVES 3

// colors are 0x00RRGGBB
#define COL_BG 0x00000000
#define COL_BALL 0x00ffffff    // WHITE
#define COL_PADDLE 0x0000ff00  // GREEN
#define COL_OVER 0x00ff0000    // RED

static int W, H;              // screen size, queried at runtime
static int pad_x, pad_y;      // paddle position
static int bx, by, bvx, bvy;  // ball position / velocity (px, px/s)
static int lives, game_over;

// draw a solid w x h rectangle of `color` with its top-left at (x, y)
static void draw_rect(int x, int y, int w, int h, uint32_t color) {
    uint32_t pixels[w * h];
    AM_GPU_FBDRAW_T e = {
            .x      = x,
            .y      = y,
            .w      = w,
            .h      = h,
            .sync   = 1,
            .pixels = pixels,
    };
    for (int i = 0; i < w * h; i++)
        pixels[i] = color;
    ioe_write(AM_GPU_FBDRAW, &e);
}

// (x,y) stands for top-left point, positive vx and vy for right and down
// directions
static void clear_screen(void) {
    for (int y = 0; y < H; y += 32)
        for (int x = 0; x < W; x += 32)
            draw_rect(x, y, 32, 32, COL_BG);
}

// Three steps to clear_screen, draw_rect for paddle, draw_rect for ball
static void draw_scene(void) {
    clear_screen();
    draw_rect(pad_x, pad_y, PADDLE_W, PADDLE_H, COL_PADDLE);
    draw_rect(bx, by, BALL_SIDE, BALL_SIDE, game_over ? COL_OVER : COL_BALL);
}

// Re-center the ball and paddle after a life loss (or a fresh start).
// (bx, by) is the ball's TOP-LEFT corner -- that's what draw_rect and the
// bounce math use -- so to put the ball's *center* at screen center we must
// subtract BALL_SIDE/2.
static void reset_ball(void) {
    bx    = W / 2 - BALL_SIDE / 2;
    by    = H / 2 - BALL_SIDE / 2 + 8;
    bvx   = BALL_SPEED * 2 / 3;    // a slight angle
    bvy   = BALL_SPEED;            // initially moving down
    pad_x = W / 2 - PADDLE_W / 2;  // paddle also re-centered horizontally
}

// Handle one keyboard event.
//
// Keyboard note: events come from io_read(AM_INPUT_KEYBRD), which polls
// the i8042 controller (port 0x60) -- NOT interrupt-driven -- so we must
// call it every frame (see main loop). With the toolchain-compat fix in
// am/src/x86/qemu/ioe.c, the Set-1 arrow scancodes are mapped to
// AM_KEY_LEFT/RIGHT/UP/DOWN. If NO key at all reaches here, the usual cause
// is that the QEMU or SDL window lacks keyboard focus -- click inside it first.
//
// ESC is the ONLY place we ever call halt(); every other exit path (life
// loss, game over) just keeps running -- an assert firing would also halt
// and that counts as Wrong Answer on the OJ.
static void kbd_event(AM_INPUT_KEYBRD_T ev) {
    if (!ev.keydown) return;  // ignore key-release events
    // ESC -> exit, the only allowed halt
    if (ev.keycode == AM_KEY_ESCAPE) halt(0);

    // Check game_over firstly
    if (game_over) {
        game_over = 0;
        lives     = LIVES;
        reset_ball();
        return;
    }

    if (ev.keycode == AM_KEY_LEFT) pad_x -= PADDLE_STEP;
    if (ev.keycode == AM_KEY_RIGHT) pad_x += PADDLE_STEP;

    if (pad_x < 0) pad_x = 0;  // clamp paddle to the screen
    if (pad_x > W - PADDLE_W) pad_x = W - PADDLE_W;
}

static void physics_step(void) {
    if (game_over) return;

    // bvx, bvy for pixels per second
    bx += bvx * FRAME_US / 1000000;
    by += bvy * FRAME_US / 1000000;

    if (bx < 0) {
        bx  = 0;
        bvx = -bvx;
    }
    if (bx > W - BALL_SIDE) {
        bx  = W - BALL_SIDE;
        bvx = -bvx;
    }
    if (by < 0) {
        by  = 0;
        bvy = -bvy;
    }

    // paddle bounce: ball coming down and overlapping the paddle
    if (bvy > 0 && by + BALL_SIDE >= pad_y && by <= pad_y + PADDLE_H &&
            bx + BALL_SIDE >= pad_x && bx <= pad_x + PADDLE_W) {
        by  = pad_y - BALL_SIDE;
        bvy = -bvy;
    } else if (by > H) {  // missed the paddle
        lives--;
        if (lives <= 0) {
            game_over = 1;
            bx        = W / 2 - BALL_SIDE / 2;
            by        = H / 2 - BALL_SIDE / 2;
            pad_x     = W / 2 - PADDLE_W  / 2;
        } else
            reset_ball();
    }
}

// AM's entry point is main(const char *args), NOT standard main(int, char**).
// ioe_init() MUST be the first IOE call (it sets up the device table) -- doing
// any io_read/ioe_read before it is undefined.
int main(const char* args) {
    (void)args;
    ioe_init();

    // Query the screen size at runtime. On x86_64-qemu the bootblock sets a
    // VBE mode (640x480x32 by default -- see am/src/x86/qemu/boot/start.S) and
    // gpu_init() reads the VBE info; NEVER hardcode W/H. Test portability
    // locally by running two real resolutions: `make ARCH=x86_64-qemu run`
    // (640x480) and `make ARCH=native run` (400x300 SDL window).
    AM_GPU_CONFIG_T cfg = {0};
    ioe_read(AM_GPU_CONFIG, &cfg);
    W = cfg.width;
    H = cfg.height;

    reset_ball();
    // pad_y is set ONCE here because the paddle only moves horizontally --
    // its Y never changes. pad_x, by contrast, is re-centered on every life
    // loss, so it lives in reset_ball() with the rest of the per-life state.
    // (8px bottom margin keeps the paddle off the very edge of the screen.)
    pad_y = H - PADDLE_H - 8;

    game_over = 0;
    lives     = LIVES;

    puts("L0 Pong. ESC=quit, <-/-> move paddle.\n");

    // io_read(reg) is just a macro over ioe_read(reg, &local) that returns
    // the struct (see klib-macros.h) -- the two are the same mechanism. Use
    // io_read for a quick one-shot read (timer, keyboard), ioe_read when you
    // want the struct as a named local you keep (the GPU cfg above). Mixing
    // them is idiomatic, not a bad smell.
    uint64_t next_frame =
            io_read(AM_TIMER_UPTIME).us;  // frame timing in microseconds
    while (1) {
        while (io_read(AM_TIMER_UPTIME).us < next_frame)
            ;  // spin until next frame
        AM_INPUT_KEYBRD_T ev;
        // drain ALL queued key events this frame; AM_KEY_NONE means "buffer
        // empty"
        while ((ev = io_read(AM_INPUT_KEYBRD)).keycode != AM_KEY_NONE)
            kbd_event(ev);
        physics_step();
        draw_scene();
        next_frame += FRAME_US;  // 1/FPS seconds, in us
    }
    return 0;  // unreachable; the game ONLY exits via halt(0) on ESC
}
