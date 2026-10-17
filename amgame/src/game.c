// L0 uses only TRM(putch, halt) and IOE(timer, keyboard, GPU). image builds with -mno-sse
#include <game.h>

#define FPS 	 30
#define FRAME_US (1000000 / FPS)
#define FIX	   256
#define MAX_W	 2048
#define BLACK	 0x000000
#define ORANGE 0xffa500

typedef struct {
	// fixed_point units
  int x, y;
	// fixed-point units per second
  int vx, vy;
  int size;
} Ball;

static int W, H;
static uint32_t row_buf[MAX_W];

// The trailing comma matters: AM_KEYS pastes KEYNAME(k) entries side by side
// with no separator, so the comma has to come from KEYNAME itself.
#define KEYNAME(k) [AM_KEY_##k] = #k,
static const char* key_names[] = { [AM_KEY_NONE] = "NONE", AM_KEYS(KEYNAME) };
static const char* key_name(int code) {
  return (code >= 0 && code < (int)LENGTH(key_names)) ? key_names[code] : "?";
}

static void frame_sync(void) {
  io_write(AM_GPU_FBDRAW, 0, 0, NULL, 0, 0, true);
}

static int clamp(int v, int lo, int hi) {
	return v < lo ? lo : (v > hi? hi : v);
}

static void fill_rect(int x, int y, int w, int h, uint32_t color) {
	// cliff a Ball
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > W) w = W - x;
	if (y + h > H) h = H - y;

	if (w <= 0 || h <= 0) 
		return;
	for (int i = 0; i < w; ++i) 
		row_buf[i] = color;
	// one row per call
	for (int j = 0; j < h; ++j)
		io_write(AM_GPU_FBDRAW, x, y + j, row_buf, w, 1, false);
}

static void ball_init(Ball* b) {
	b->size = clamp((W < H ? W : H) / 24, 4, 64);
	b->x = (W - b->size) / 2 * FIX;
	b->y = (H - b->size) / 2 * FIX;
	b->vx = H * FIX / 3;
	b->vy = H * FIX / 5;
}

static void ball_update(Ball* b) {
	int max_x = (W - b->size) * FIX, max_y = (H - b->size) * FIX;
	b->x += b->vx / FPS;
	b->y += b->vy / FPS;

	if (b->x < 0) {
		b->x = -b->x;
		b->vx = -b->vx;	
	}
	if (b->x > max_x) {
		b->x = 2 * max_x - b->x;
		b->vx = -b->vx;	
	}
	if(b->y < 0) {
		b->y = -b->y;
		b->vy = -b->vy;	
	}
	if(b->y > max_y) {
		b->y = 2 * max_y - b->y;	
		b->vy = -b->vy;
	}

	b->x = clamp(b->x, 0, max_x);
	b->y = clamp(b->y, 0, max_y);
}

static void handle_key(Ball* b, int key) {
	// delta is a quarter H per second
	int step = H * FIX / 4;
	int vmax = 2 * H * FIX;
	switch (key) {
		case AM_KEY_UP:	case AM_KEY_W:
			b->vy -= step;
			break;	
		case AM_KEY_DOWN: case AM_KEY_S:
			b->vy += step;
			break;
		case AM_KEY_LEFT: case AM_KEY_A:
			b->vx -= step;
			break;
		case AM_KEY_RIGHT: case AM_KEY_D:
			b->vx += step;
			break;
		case AM_KEY_SPACE:
			b->vx = b->vy = 0;
			break;
		case AM_KEY_ESCAPE:
			halt(0);
		default:
			break;
	}
	b->vx = clamp(b->vx, -vmax, vmax);
	b->vy = clamp(b->vy, -vmax, vmax);
}

// Operating system is a C program!
int main(const char *args) {
	// precede io_read/io_write
  ioe_init();

	// Read the config once. Clamp W: row_buf holds MAX_W pixels, so a wider
	// screen would make fill_rect write past the end of it.
	AM_GPU_CONFIG_T gpu = io_read(AM_GPU_CONFIG);
	W = gpu.width < MAX_W ? gpu.width : MAX_W;
	H = gpu.height;

	// printf comes from klib on QEMU, and from glibc on ARCH=native.
	// "screen=" doubles as the start-up marker qemu_drive.py waits for.
	printf("mainargs=\"%s\" screen=%dx%d\n", args, W, H); // make run mainargs=xxx

	// L0 initial game
  //splash();
	
	Ball b;
	ball_init(&b);
	fill_rect(0, 0, W, H, BLACK);
	frame_sync();

	int old_x = -1, old_y = -1;
	uint32_t frames = 0;
	uint64_t next = io_read(AM_TIMER_UPTIME).us, fps_t0 = next;

	while(1) {
		uint64_t now;	
		while ((now = io_read(AM_TIMER_UPTIME).us) < next) ;
		next += FRAME_US;
		if (now > next + 10 * FRAME_US)
			next = now + FRAME_US;

		for (int n = 0; n < 64; ++n) {
			AM_INPUT_KEYBRD_T ev = io_read(AM_INPUT_KEYBRD);	

			if (ev.keycode == AM_KEY_NONE)
				break;
			if (!ev.keydown)
				continue;

			printf("key %s\n", key_name(ev.keycode));
			handle_key(&b, ev.keycode);
		}

		ball_update(&b);

		int px = b.x / FIX, py = b.y / FIX;
		if (px != old_x || py != old_y) {
			if(old_x >= 0)
				fill_rect(old_x, old_y, b.size, b.size, BLACK);
			fill_rect(px, py, b.size, b.size, ORANGE);
			old_x = px;
			old_y = py;
			frame_sync();
		}

		frames++;
		if(now - fps_t0 >= 1000000) {
			printf("fps=%u\n", frames);  // frames is uint32_t: %u, not %d
		  frames = 0;	
			fps_t0 = now;
		}
	}

  return 0;
}
