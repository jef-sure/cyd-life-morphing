#include <inttypes.h>
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
    CYD_TFT_SPI_HZ    = 40 * 1000 * 1000
};

static const char *TAG      = "cyd-life-morphing";
static int         neibx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
static int         neiby[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

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

LifeGeneration *create_initial_beacon_life()
{
    int             width = 6;
    LifeGeneration *life  = create_life(width, width);
    if (life == NULL) {
        return NULL;
    }

    // Upper-left 2x2 block (minus its inner corner)
    life->cells[CELL_OFFSET(1, 1, width)] = 1;
    life->cells[CELL_OFFSET(2, 1, width)] = 1;
    life->cells[CELL_OFFSET(1, 2, width)] = 1;

    // Lower-right 2x2 block (minus its inner corner)
    life->cells[CELL_OFFSET(4, 3, width)] = 1;
    life->cells[CELL_OFFSET(3, 4, width)] = 1;
    life->cells[CELL_OFFSET(4, 4, width)] = 1;

    return life;
}

LifeGeneration *create_initial_toad_life()
{
    int             width = 6;
    LifeGeneration *life  = create_life(width, 4);
    if (life == NULL) {
        return NULL;
    }

    // Top row of the toad
    life->cells[CELL_OFFSET(2, 1, width)] = 1;
    life->cells[CELL_OFFSET(3, 1, width)] = 1;
    life->cells[CELL_OFFSET(4, 1, width)] = 1;

    // Bottom row of the toad, shifted one cell left
    life->cells[CELL_OFFSET(1, 2, width)] = 1;
    life->cells[CELL_OFFSET(2, 2, width)] = 1;
    life->cells[CELL_OFFSET(3, 2, width)] = 1;

    return life;
}

LifeGeneration *create_initial_pulsar_life()
{
    int             width = 15;
    LifeGeneration *life  = create_life(width, width);
    if (life == NULL) {
        return NULL;
    }

    int cx = width / 2; // center of the field (7)
    int cy = width / 2;

    // The pulsar has 8-fold (D4) symmetry: 4 rotations x diagonal mirror.
    // Just two 3-cell "arm" segments are enough to generate the whole shape.
    static const int arm_offsets[2][3][2] = {
        {{-4, -6}, {-3, -6}, {-2, -6}}, // horizontal arm segment
        {{-1, -4}, {-1, -3}, {-1, -2}}, // vertical arm segment
    };

    for (int seg = 0; seg < 2; seg++) {
        for (int i = 0; i < 3; i++) {
            int x = arm_offsets[seg][i][0];
            int y = arm_offsets[seg][i][1];
            for (int rot = 0; rot < 4; rot++) {
                life->cells[CELL_OFFSET(cx + x, cy + y, width)] = 1; // rotated cell
                life->cells[CELL_OFFSET(cx + y, cy + x, width)] = 1; // its diagonal mirror
                //
                int tmp = x;
                x       = -y;
                y       = tmp; // rotate 90 degrees for the next iteration
            }
        }
    }

    return life;
}

LifeGeneration *create_initial_rpentomino_life()
{
    int             width = 30;
    LifeGeneration *life  = create_life(width, 25);
    if (life == NULL) {
        return NULL;
    }

    int cx = width / 2 - 2;
    int cy = 25 / 2 - 2;

    life->cells[CELL_OFFSET(cx + 0, cy - 1, width)] = 1; // top-right cell of the R shape
    life->cells[CELL_OFFSET(cx + 1, cy - 1, width)] = 1; // top-right cell, extended right
    life->cells[CELL_OFFSET(cx - 1, cy + 0, width)] = 1; // middle-left cell
    life->cells[CELL_OFFSET(cx + 0, cy + 0, width)] = 1; // middle-center cell
    life->cells[CELL_OFFSET(cx + 0, cy + 1, width)] = 1; // bottom-center cell

    return life;
}

LifeGeneration *create_initial_gliders_life()
{
    const int        width           = 10;
    const int        height          = 11;
    static const int live_cells[][2] = {
        {1, 0 },
        {2, 1 },
        {0, 2 },
        {1, 2 },
        {2, 2 },
        {8, 0 },
        {7, 1 },
        {7, 2 },
        {8, 2 },
        {9, 2 },
        {1, 10},
        {2, 9 },
        {0, 8 },
        {1, 8 },
        {2, 8 },
        {7, 8 },
        {8, 8 },
        {9, 8 },
        {7, 9 },
        {8, 10},
    };

    LifeGeneration *life = create_life(width, height);
    if (life == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < sizeof(live_cells) / sizeof(live_cells[0]); i++) {
        int x                                 = live_cells[i][0];
        int y                                 = live_cells[i][1];
        life->cells[CELL_OFFSET(x, y, width)] = 1;
    }
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

static inline float max_float(float a, float b)
{
    return a > b ? a : b;
}

typedef struct
{
    int16_t x, y;
} Point;

typedef struct
{
    Point start;
    Point end;
} Segment;

typedef struct
{
    int     count;
    Segment segments[8];
} SegmentsInCell;

typedef LifeGeneration *(*life_creation_func_t)();

typedef struct
{
    LifeGeneration      *current;
    LifeGeneration      *next;
    int                  cell_width;
    int                  grid_width;
    int                  grid_height;
    int                  radius;
    int                  rlut_limit;
    int                 *rlut;
    int                 *xcell_offset;
    dgx_screen_t        *vscreen;
    uint8_t             *glow_next;
    uint8_t             *glow_prev;
    uint8_t             *source_used;
    SegmentsInCell      *segments;
    Point               *faded_points;
    Point               *static_points;
    life_creation_func_t life_creation_func;
} LifeTransformation;

static inline float smoothstep3(float t)
{
    return t * t * (3 - 2 * t);
}

static bool life_transformation_init(LifeTransformation *transformation, LifeGeneration *current, int screen_width, int screen_height,
                                     int max_cell_width)
{
    memset(transformation, 0, sizeof(*transformation));
    transformation->current     = current;
    transformation->cell_width  = min_int(max_cell_width, min_int(screen_width / current->width, screen_height / current->height));
    transformation->grid_width  = transformation->cell_width * current->width;
    transformation->grid_height = transformation->cell_width * current->height;
    transformation->radius      = transformation->cell_width / 2 + transformation->cell_width / 4;
    transformation->rlut_limit  = transformation->radius * transformation->radius;
    transformation->vscreen     = dgx_vscreen_init(transformation->grid_width, transformation->grid_height, 16, DgxScreenRGB);
    if (transformation->vscreen == NULL) {
        memset(transformation, 0, sizeof(*transformation));
        return false;
    }

    transformation->glow_prev     = calloc(transformation->grid_width * transformation->grid_height, sizeof(uint8_t));
    transformation->glow_next     = calloc(transformation->grid_width * transformation->grid_height, sizeof(uint8_t));
    transformation->source_used   = calloc((current->width * current->height + 7) / 8, sizeof(uint8_t));
    transformation->rlut          = malloc(transformation->rlut_limit * sizeof(int));
    transformation->xcell_offset  = malloc((transformation->radius + 1) * sizeof(int));
    transformation->segments      = calloc(current->width * current->height, sizeof(SegmentsInCell));
    transformation->faded_points  = calloc(current->width * current->height, sizeof(Point));
    transformation->static_points = calloc(current->width * current->height, sizeof(Point));
    if (transformation->rlut == NULL || transformation->glow_prev == NULL || transformation->glow_next == NULL ||
        transformation->source_used == NULL || transformation->xcell_offset == NULL || transformation->segments == NULL ||
        transformation->faded_points == NULL || transformation->static_points == NULL) {
        dgx_screen_destroy(&transformation->vscreen);
        free(transformation->rlut);
        free(transformation->glow_prev);
        free(transformation->glow_next);
        free(transformation->source_used);
        free(transformation->faded_points);
        free(transformation->static_points);
        free(transformation->xcell_offset);
        free(transformation->segments);
        memset(transformation, 0, sizeof(*transformation));
        return false;
    }

    for (int i = 0; i < transformation->rlut_limit; i++) {
        float x                 = (float)i / transformation->rlut_limit;
        transformation->rlut[i] = 255.0f * (1.0f - smoothstep3(x));
    }
    for (int dy = 0; dy <= transformation->radius; dy++) {
        int rem                          = transformation->rlut_limit - 1 - dy * dy;
        transformation->xcell_offset[dy] = (rem >= 0) ? (int)sqrtf((float)rem) : -1;
    }
    return true;
}

static void life_transformation_free(LifeTransformation *transformation)
{
    dgx_screen_destroy(&transformation->vscreen);
    free(transformation->rlut);
    free(transformation->glow_prev);
    free(transformation->glow_next);
    free(transformation->source_used);
    free(transformation->xcell_offset);
    free(transformation->segments);
    free(transformation->faded_points);
    free(transformation->static_points);
    memset(transformation, 0, sizeof(*transformation));
}

bool is_source_used(LifeTransformation *transformation, int x, int y)
{
    size_t idx = CELL_OFFSET(x, y, transformation->current->width);
    return (transformation->source_used[idx / 8u] & (1 << (idx % 8u))) != 0;
}

void mark_source_used(LifeTransformation *transformation, int x, int y)
{
    size_t idx = CELL_OFFSET(x, y, transformation->current->width);
    transformation->source_used[idx / 8u] |= (1 << (idx % 8u));
}

Point inner_point(float t, Point start, Point end)
{
    return (Point){.x = start.x + (int)((end.x - start.x) * t), .y = start.y + (int)((end.y - start.y) * t)};
}

static inline uint32_t div255(uint32_t n)
{
    return (n + 1 + (n >> 8)) >> 8;
}

static void collect_glow(LifeTransformation *transformation, uint8_t *glow, Point point, uint8_t intensity)
{
    if (intensity == 0) {
        return;
    }

    int y0 = max_int(point.y - transformation->radius, 0);
    int y1 = min_int(point.y + transformation->radius, transformation->grid_height - 1);

    for (int y = y0; y <= y1; y++) {
        int dy     = y - point.y;
        int max_dx = transformation->xcell_offset[abs(dy)];
        if (max_dx < 0) {
            continue;
        }

        int x0 = max_int(point.x - max_dx, 0);
        int x1 = min_int(point.x + max_dx, transformation->grid_width - 1);
        if (x0 > x1) {
            continue;
        }

        int dy2        = dy * dy;
        int row_offset = CELL_OFFSET(0, y, transformation->grid_width);
        for (int x = x0; x <= x1; x++) {
            int dx           = x - point.x;
            int distance     = dx * dx + dy2;
            int falloff      = transformation->rlut[distance];
            int contribution = div255(falloff * intensity);
            int idx          = row_offset + x;
            int collected    = glow[idx] + contribution;
            glow[idx]        = min_int(collected, 255);
        }
    }
}

void collect_initial_glow(LifeTransformation *transformation)
{
    LifeGeneration *current = transformation->current;
    for (int y = 0; y < current->height; y++) {
        for (int x = 0; x < current->width; x++) {
            if (current->cells[CELL_OFFSET(x, y, current->width)] == 0) continue;
            collect_glow(                                                                 //
                transformation,                                                           //
                transformation->glow_next,                                                //
                (Point){x * transformation->cell_width + transformation->cell_width / 2,  //
                        y * transformation->cell_width + transformation->cell_width / 2}, //
                255                                                                       //
            );
        }
    }
}

void draw_life_transformation(float t, LifeTransformation *transformation)
{
    LifeGeneration *current = transformation->current;
    LifeGeneration *next    = transformation->next;
    // Swap glow buffers to prepare for the next frame.
    uint8_t *glow_accu        = transformation->glow_prev;
    transformation->glow_prev = transformation->glow_next;
    transformation->glow_next = glow_accu;
    // glow_accu now points to the buffer that will accumulate the glow for the current frame.
    SegmentsInCell *segments = transformation->segments;
    memset(segments, 0, current->width * current->height * sizeof(SegmentsInCell));
    glow_accu            = transformation->glow_next;
    Point *faded_points  = transformation->faded_points;
    Point *static_points = transformation->static_points;
    int    faded_count   = 0;
    int    static_count  = 0;
    float  t_tail        = max_float(t * 1.5f - 0.5f, 0.0f);
    memset(transformation->source_used, 0, (current->width * current->height + 7) / 8);
    for (int y = 0; y < current->height; y++) {
        for (int x = 0; x < current->width; x++) {
            int idx       = CELL_OFFSET(x, y, current->width);
            int cell_case = current->cells[idx] + next->cells[idx] * 2;
            if (cell_case == 0) continue;
            if (cell_case == 3) {
                static_points[static_count] = (Point){x * transformation->cell_width + transformation->cell_width / 2,
                                                      y * transformation->cell_width + transformation->cell_width / 2};
                static_count++;
                continue;
            }
            // int next_neib_count = 0;
            // int curr_neib_count = 0;
            for (int i = 0; i < 8; i++) {
                int nx = x + neibx[i];
                int ny = y + neiby[i];
                if (nx >= 0 && nx < current->width && ny >= 0 && ny < current->height) {
                    // curr_neib_count += current->cells[CELL_OFFSET(nx, ny, current->width)];
                    // next_neib_count += next->cells[CELL_OFFSET(nx, ny, current->width)];
                    if (cell_case == 2 && current->cells[CELL_OFFSET(nx, ny, current->width)]) {
                        Point start = (Point){nx * transformation->cell_width + transformation->cell_width / 2,
                                              ny * transformation->cell_width + transformation->cell_width / 2};
                        Point end   = (Point){x * transformation->cell_width + transformation->cell_width / 2,
                                              y * transformation->cell_width + transformation->cell_width / 2};
                        mark_source_used(transformation, nx, ny);
                        //
                        segments[idx].segments[segments[idx].count].start = inner_point(t_tail, start, end);
                        segments[idx].segments[segments[idx].count].end   = inner_point(t, start, end);
                        segments[idx].count++;
                    }
                }
            }
        }
    }
    for (int y = 0; y < current->height; y++) {
        for (int x = 0; x < current->width; x++) {
            int idx       = CELL_OFFSET(x, y, current->width);
            int cell_case = current->cells[idx] + next->cells[idx] * 2;
            if (cell_case == 1 && !is_source_used(transformation, x, y)) {
                faded_points[faded_count] = (Point){x * transformation->cell_width + transformation->cell_width / 2,
                                                    y * transformation->cell_width + transformation->cell_width / 2};
                faded_count++;
            }
        }
    }
    memset(glow_accu, 0, transformation->grid_width * transformation->grid_height * sizeof(uint8_t));
    for (int y = 0; y < current->height; y++) {
        for (int x = 0; x < current->width; x++) {
            int idx = CELL_OFFSET(x, y, current->width);
            for (int i = 0; i < segments[idx].count; i++) {
                collect_glow(transformation, glow_accu, segments[idx].segments[i].start, 255 / 6);
                collect_glow(transformation, glow_accu, segments[idx].segments[i].end, 255 / 6);
            }
        }
    }
    uint8_t t_fade = max_int(0, 255 - (int16_t)(255.0f * t));
    for (int i = 0; i < faded_count; i++) {
        collect_glow(transformation, glow_accu, faded_points[i], t_fade);
    }
    for (int i = 0; i < static_count; i++) {
        collect_glow(transformation, glow_accu, static_points[i], 255);
    }
    float    t_smooth       = smoothstep3(t);
    uint32_t t_fx           = (uint32_t)(256.0f * t_smooth);
    uint16_t *direct_v_array = (uint16_t *)((dgx_vscreen_t *)transformation->vscreen)->v_array;
    for (int y = 0; y < transformation->grid_height; y++) {
        for (int x = 0; x < transformation->grid_width; x++) {
            int     glow_idx  = CELL_OFFSET(x, y, transformation->grid_width);
            uint8_t intensity = (glow_accu[glow_idx] * t_fx + (256 - t_fx) * transformation->glow_prev[glow_idx]) >> 8;
            // dgx_set_pixel(transformation->vscreen, x, y, dgx_rgb_to_16(intensity, intensity, intensity));
            uint16_t rgb        = DGX_RGB_16(intensity, intensity, intensity);
            *direct_v_array++   = (rgb >> 8) | (rgb << 8);
            glow_accu[glow_idx] = intensity;
        }
    }
}

static bool life_is_same(const LifeGeneration *a, const LifeGeneration *b)
{
    return memcmp(a->cells, b->cells, a->width * a->height) == 0;
}

LifeGeneration *init_life(LifeTransformation *transformation, life_creation_func_t create_func, dgx_screen_t *screen)
{
    LifeGeneration *step = create_func();
    if (step == NULL) {
        ESP_LOGE(TAG, "initial life allocation failed");
        return NULL;
    }
    int init_max_cell_width = min_int(screen->width / step->width, screen->height / step->height);
    if (init_max_cell_width % 2 == 0) init_max_cell_width--;
    while (!life_transformation_init(transformation, step, screen->width, screen->height, init_max_cell_width)) {
        ESP_LOGE(TAG, "life transformation with max cell width %d initialization failed", init_max_cell_width);
        init_max_cell_width -= 2;
        if (init_max_cell_width <= 3) {
            ESP_LOGE(TAG, "unable to initialize life transformation with any cell width");
            free(step);
            return NULL;
        }
    }
    transformation->life_creation_func = create_func;
    ESP_LOGI(TAG, "life transformation initialized with max cell width %d", init_max_cell_width);
    return step;
}

life_creation_func_t life_types[] = {
    create_initial_gliders_life,    //
    create_initial_navy_life,       //
    create_initial_beacon_life,     //
    create_initial_toad_life,       //
    create_initial_pulsar_life,     //
    create_initial_rpentomino_life, //
    NULL                            //
};

typedef enum
{
    ButtonReleased,
    ButtonPressed,
} ButtonState;

void app_main(void)
{
    dgx_screen_t *screen = cyd_init_display();
    if (screen == NULL) {
        return;
    }
    gpio_config_t boot_btn_config = {
        .pin_bit_mask = 1ULL << GPIO_NUM_0,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    gpio_config(&boot_btn_config);

    LifeTransformation transformation;
    LifeGeneration    *step = init_life(&transformation, life_types[0], screen);
    if (step == NULL) {
        return;
    }

    ESP_LOGI(TAG, "CYD display initialized: %dx%d, cell %dpx", screen->width, screen->height, transformation.cell_width);
    collect_initial_glow(&transformation);
    ButtonState button_state = gpio_get_level(GPIO_NUM_0) == 0 ? ButtonPressed : ButtonReleased;
    int64_t     fps_start    = esp_timer_get_time();
    uint32_t    frame_count  = 0;
    uint32_t    step_count   = 0;
    while (true) {
        step_count++;
        ESP_LOGI(TAG, "Step #%" PRIu32, step_count);
        LifeGeneration *next = next_generation(step);
        if (next != NULL && (!is_life_still_alive(next) || life_is_same(step, next))) {
            const char *reason = !is_life_still_alive(next) ? "extinct" : "still life";
            ESP_LOGI(TAG, "Restarting pattern (%s) after %" PRIu32 " steps", reason, step_count);
            free(next); // extinct or still life: morph back to the seed
            next       = transformation.life_creation_func();
            step_count = 0;
        }
        if (next == NULL) {
            ESP_LOGE(TAG, "next generation allocation failed");
            break;
        }
        transformation.next        = next;
        transformation.current     = step;
        int64_t start_time         = esp_timer_get_time();
        bool    restart_generation = false;
        while (true) {
            float t = (float)(esp_timer_get_time() - start_time) / 1000000.0f;
            if (t > 1.0f) t = 1.0f;
            dgx_fill_rectangle(transformation.vscreen, 0, 0, transformation.grid_width, transformation.grid_height, 0x000000);
            draw_life_transformation(t, &transformation);
            int offset_x = (screen->width - transformation.grid_width) / 2;
            int offset_y = (screen->height - transformation.grid_height) / 2;
            dgx_vscreen_to_screen(screen, offset_x, offset_y, transformation.vscreen);
            frame_count++;
            int64_t fps_elapsed = esp_timer_get_time() - fps_start;
            if (fps_elapsed >= 1000000) {
                ESP_LOGI(TAG, "FPS: %.1f", frame_count * 1000000.0f / fps_elapsed);
                fps_start   = esp_timer_get_time();
                frame_count = 0;
            }
            if (t >= 1.0f) break;
            bool pressed = gpio_get_level(GPIO_NUM_0) == 0;
            if (!pressed) {
                button_state = ButtonReleased;
            } else if (button_state == ButtonReleased) {
                ESP_LOGI(TAG, "Restarting: pattern switch requested via button (after %" PRIu32 " steps)", step_count);
                dgx_fill_rectangle(screen, offset_x, offset_y, transformation.vscreen->width, transformation.vscreen->height, 0x000000);
                button_state                     = ButtonPressed;
                life_creation_func_t create_func = life_types[0];
                for (int i = 0; life_types[i] != NULL; i++) {
                    if (life_types[i] == transformation.life_creation_func) {
                        create_func = life_types[i + 1] == NULL ? life_types[0] : life_types[i + 1];
                        break;
                    }
                }
                free(next);
                free(step);
                next = NULL;
                step = NULL;
                life_transformation_free(&transformation);
                step       = init_life(&transformation, create_func, screen);
                step_count = 0;
                if (step != NULL) {
                    collect_initial_glow(&transformation);
                }
                restart_generation = true;
                break;
            }
        }
        vTaskDelay(1); // feed the watchdog
        if (restart_generation) {
            if (step == NULL) break;
            continue;
        }
        free(step);
        step = next;
    }

    free(step);
    life_transformation_free(&transformation);
}
