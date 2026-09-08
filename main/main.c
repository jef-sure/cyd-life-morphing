#include <math.h>
#include <stdio.h>
#include <string.h>

#include "bus/dgx_spi_esp32.h"
#include "dgx_bits.h"
#include "dgx_colors.h"
#include "dgx_draw.h"
#include "dgx_font.h"
#include "driver/gpio.h"
#include "drivers/ili9341.h"
#include "drivers/vscreen.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "fonts/ArialRegular12.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum
{
    CYD_TFT_MOSI      = GPIO_NUM_13,
    CYD_TFT_MISO      = GPIO_NUM_12,
    CYD_TFT_SCLK      = GPIO_NUM_14,
    CYD_TFT_CS        = GPIO_NUM_15,
    CYD_TFT_DC        = GPIO_NUM_2,
    CYD_TFT_RST       = GPIO_NUM_NC,
    CYD_TFT_BACKLIGHT = GPIO_NUM_21,
    CYD_TFT_SPI_HOST  = SPI2_HOST,
    CYD_TFT_SPI_HZ    = 40 * 1000 * 1000,
};

static const char *TAG = "cyd-life-morphing";

typedef struct
{
    int     width;
    int     height;
    int64_t creation_time;
    uint8_t cells[0];
} LifeGeneration;

LifeGeneration *create_life(int width, int height)
{
    LifeGeneration *life = (LifeGeneration *)calloc(1, sizeof(LifeGeneration) + (width * height * sizeof(uint8_t)));
    if (life == NULL) {
        return NULL;
    }
    life->width         = width;
    life->height        = height;
    life->creation_time = esp_timer_get_time();
    return life;
}

#define CELL_OFFSET(x, y, width) ((y) * (width) + (x))

LifeGeneration *next_generation(const LifeGeneration *current)
{
    LifeGeneration *next = create_life(current->width, current->height);
    if (next == NULL) {
        return NULL;
    }

    static int neibx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static int neiby[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
    for (int y = 0; y < current->height; y++) {
        for (int x = 0; x < current->width; x++) {
            int alive_neighbors = 0;
            for (int n = 0; n < 8; n++) {
                int dx = neibx[n];
                int dy = neiby[n];
                int nx = x + dx;
                int ny = y + dy;
                if (nx >= 0 && nx < current->width && ny >= 0 && ny < current->height) {
                    alive_neighbors += current->cells[CELL_OFFSET(nx, ny, current->width)];
                }
            }
            int idx = CELL_OFFSET(x, y, current->width);
            if (current->cells[idx]) {
                next->cells[idx] = (alive_neighbors == 2 || alive_neighbors == 3) ? 1 : 0;
            } else {
                next->cells[idx] = (alive_neighbors == 3) ? 1 : 0;
            }
        }
    }

    next->creation_time = esp_timer_get_time();
    return next;
}

bool is_life_still_alive(const LifeGeneration *life)
{
    for (int i = 0; i < life->width * life->height; i++) {
        if (life->cells[i]) {
            return true;
        }
    }
    return false;
}

static dgx_screen_t *cyd_init_display(void)
{
    dgx_bus_protocols_t *bus =
        dgx_spi_init(CYD_TFT_SPI_HOST, SPI_DMA_CH_AUTO, CYD_TFT_MOSI, CYD_TFT_MISO, CYD_TFT_SCLK, CYD_TFT_CS, CYD_TFT_DC, CYD_TFT_SPI_HZ, 0);
    if (bus == NULL) {
        ESP_LOGE(TAG, "DGX SPI init failed");
        return NULL;
    }

    dgx_screen_t *screen = dgx_ili9341_init(bus, CYD_TFT_RST, CYD_TFT_BACKLIGHT, 16, DgxScreenRGB);
    if (screen == NULL) {
        ESP_LOGE(TAG, "ILI9341 init failed");
        return NULL;
    }

    dgx_ili9341_orientation(screen, DgxScreenRightLeft, DgxScreenTopBottom, true);
    dgx_fill_rectangle(screen, 0, 0, screen->width, screen->height, DGX_BLACK(DGX_RGB_16));
    return screen;
}

LifeGeneration *create_initial_navy_life()
{
    int             width = 9;
    LifeGeneration *life  = create_life(width, width);
    if (life == NULL) {
        return NULL;
    }
    life->cells[CELL_OFFSET(4, 3, width)] = 1; // Set the cell above the center alive for the initial navy life pattern
    life->cells[CELL_OFFSET(3, 4, width)] = 1; // Set the cell to the left of the center alive for the initial navy life pattern
    life->cells[CELL_OFFSET(4, 4, width)] = 1; // Set the center cell alive for the initial navy life pattern
    life->cells[CELL_OFFSET(5, 4, width)] = 1; // Set the cell to the right of the center alive for the initial navy life pattern
    return life;
}

static inline int min_int(int a, int b)
{
    return a < b ? a : b;
}

static inline int max_int(int a, int b)
{
    return a > b ? a : b;
}

typedef struct
{
    int x, y;
} Point;

typedef struct
{
    Point pos;
    int   intensity; // 0..255
} GlowPoint;

typedef struct
{
    int        cell_size;
    int        radius;
    int        width;  // pixels
    int        height; // pixels
    uint8_t   *lum;
    GlowPoint *glow;
    int        glow_capacity;
} LifeRenderer;

static Point cell_center(int x, int y, int cell_size)
{
    return (Point){x * cell_size + cell_size / 2, y * cell_size + cell_size / 2};
}

static Point gravity_motion(Point source, Point target, float t)
{
    int   dx   = target.x - source.x;
    int   dy   = target.y - source.y;
    float dist = sqrtf((float)dx * dx + (float)dy * dy);
    if (dist < 0.001f) {
        return source;
    }

    float nx     = (float)dx / dist;
    float ny     = (float)dy / dist;
    float travel = dist * t;

    return (Point){source.x + (int)(nx * travel), source.y + (int)(ny * travel)};
}

static int gravity_intensity(float distance, float gravity_strength)
{
    float force = distance / (1.0f + gravity_strength * 0.12f);
    float value = 32.0f + force * 8.5f;
    if (value > 255.0f) value = 255.0f;
    if (value < 0.0f) value = 0.0f;
    return (int)value;
}

static bool life_renderer_init(LifeRenderer *r, int screen_width, int screen_height, int cells_x, int cells_y)
{
    int cell_size = min_int(screen_width / cells_x, screen_height / cells_y);
    if (cell_size % 2 == 0) cell_size--; // odd size gives an exact center pixel
    r->cell_size     = cell_size;
    r->radius        = cell_size / 2;
    r->width         = cell_size * cells_x;
    r->height        = cell_size * cells_y;
    r->glow_capacity = cells_x * cells_y * 8; // a cell contributes at most one point per neighbor
    r->lum           = malloc(r->width * r->height);
    r->glow          = malloc(r->glow_capacity * sizeof(GlowPoint));
    if (r->lum == NULL || r->glow == NULL) {
        free(r->lum);
        free(r->glow);
        return false;
    }
    return true;
}

static void life_renderer_free(LifeRenderer *r)
{
    free(r->lum);
    free(r->glow);
}

// Additively stamps a disc with quadratic falloff that reaches zero at the radius.
static void stamp_glow(const LifeRenderer *r, GlowPoint g)
{
    if (g.intensity <= 0) return;
    int r2 = r->radius * r->radius;
    int x0 = max_int(g.pos.x - r->radius, 0);
    int x1 = min_int(g.pos.x + r->radius, r->width - 1);
    int y0 = max_int(g.pos.y - r->radius, 0);
    int y1 = min_int(g.pos.y + r->radius, r->height - 1);
    for (int py = y0; py <= y1; py++) {
        int      dy  = py - g.pos.y;
        uint8_t *row = r->lum + py * r->width;
        for (int px = x0; px <= x1; px++) {
            int dx = px - g.pos.x;
            int d2 = dx * dx + dy * dy;
            if (d2 >= r2) continue;
            int v   = row[px] + g.intensity * (r2 - d2) / r2;
            row[px] = v > 255 ? 255 : v;
        }
    }
}

static void append_glow(LifeRenderer *r, int *count, Point pos, int intensity)
{
    if (*count >= r->glow_capacity) {
        return;
    }
    r->glow[*count] = (GlowPoint){pos, intensity};
    (*count)++;
}

static int collect_glow(LifeRenderer *r, float t, const LifeGeneration *current, const LifeGeneration *next)
{
    int       n = 0;
    const int w = current->width;
    const int h = current->height;
    bool      matched_next[w * h];
    memset(matched_next, false, sizeof(matched_next));

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int idx = CELL_OFFSET(x, y, w);
            if (!current->cells[idx]) {
                continue;
            }

            Point source   = cell_center(x, y, r->cell_size);
            int   best_idx = -1;
            int   best_d2  = INT_MAX;

            for (int yy = 0; yy < h; yy++) {
                for (int xx = 0; xx < w; xx++) {
                    int dest_idx = CELL_OFFSET(xx, yy, w);
                    if (!next->cells[dest_idx] || matched_next[dest_idx]) {
                        continue;
                    }

                    int dx = xx - x;
                    int dy = yy - y;
                    int d2 = dx * dx + dy * dy;
                    if (d2 < best_d2) {
                        best_d2  = d2;
                        best_idx = dest_idx;
                    }
                }
            }

            if (best_idx >= 0) {
                int   dest_x           = best_idx % w;
                int   dest_y           = best_idx / w;
                Point dst              = cell_center(dest_x, dest_y, r->cell_size);
                int   dx               = dst.x - source.x;
                int   dy               = dst.y - source.y;
                float dist             = sqrtf((float)dx * dx + (float)dy * dy);
                Point pos              = gravity_motion(source, dst, t);
                matched_next[best_idx] = true;
                append_glow(r, &n, pos, gravity_intensity(dist, 9.0f));
            } else {
                append_glow(r, &n, source, (int)(255.0f * (1.0f - t)));
            }
        }
    }

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int idx = CELL_OFFSET(x, y, w);
            if (!next->cells[idx] || matched_next[idx]) {
                continue;
            }

            Point center = cell_center(x, y, r->cell_size);
            append_glow(r, &n, center, (int)(255.0f * t));
        }
    }

    return n;
}

static void draw_life_transformation(LifeRenderer *r, dgx_screen_t *vscreen, float t, const LifeGeneration *current, const LifeGeneration *next)
{
    int count = collect_glow(r, t, current, next);
    memset(r->lum, 0, r->width * r->height);
    for (int i = 0; i < count; i++) {
        stamp_glow(r, r->glow[i]);
    }
    for (int py = 0; py < r->height; py++) {
        const uint8_t *row = r->lum + py * r->width;
        for (int px = 0; px < r->width; px++) {
            uint8_t v = row[px];
            dgx_set_pixel(vscreen, px, py, dgx_rgb_to_16(v, v, v));
        }
    }
}

static bool life_is_same(const LifeGeneration *a, const LifeGeneration *b)
{
    return memcmp(a->cells, b->cells, a->width * a->height) == 0;
}

void app_main(void)
{
    dgx_screen_t *screen = cyd_init_display();
    if (screen == NULL) {
        return;
    }

    LifeGeneration *step = create_initial_navy_life();
    if (step == NULL) {
        ESP_LOGE(TAG, "initial life allocation failed");
        return;
    }

    LifeRenderer renderer;
    if (!life_renderer_init(&renderer, screen->width, screen->height, step->width, step->height)) {
        ESP_LOGE(TAG, "renderer allocation failed");
        free(step);
        return;
    }
    const int offset_x = (screen->width - renderer.width) / 2;
    const int offset_y = (screen->height - renderer.height) / 2;

    dgx_screen_t *vscreen = dgx_vscreen_init(renderer.width, renderer.height, 16, DgxScreenRGB);
    if (vscreen == NULL) {
        ESP_LOGE(TAG, "virtual screen init failed");
        life_renderer_free(&renderer);
        free(step);
        return;
    }

    ESP_LOGI(TAG, "CYD display initialized: %dx%d, cell %dpx", screen->width, screen->height, renderer.cell_size);

    while (true) {
        LifeGeneration *next = next_generation(step);
        if (next != NULL && (!is_life_still_alive(next) || life_is_same(step, next))) {
            free(next); // extinct or still life: morph back to the seed
            next = create_initial_navy_life();
        }
        if (next == NULL) {
            ESP_LOGE(TAG, "next generation allocation failed");
            break;
        }

        int64_t start_time = esp_timer_get_time();
        while (true) {
            float t = (float)(esp_timer_get_time() - start_time) / 1000000.0f;
            if (t > 1.0f) t = 1.0f;
            draw_life_transformation(&renderer, vscreen, t, step, next);
            dgx_vscreen_to_screen(screen, offset_x, offset_y, vscreen);
            if (t >= 1.0f) break;
            vTaskDelay(1);
        }
        free(step);
        step = next;
    }

    free(step);
    life_renderer_free(&renderer);
}
