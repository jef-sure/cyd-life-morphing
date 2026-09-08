#include <stdio.h>

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

typedef struct
{
    int x, y;
} Point;

typedef struct
{
    Point start, end;
} Segment;

typedef struct
{
    int     count;
    Segment segments[8];
} SegmentsInCell;

typedef struct
{
    int x, y;
} Vector;

Point inner_point(float t, Point p_start, Point p_end)
{
    Point p;
    p.x = p_start.x + (int)((p_end.x - p_start.x) * t);
    p.y = p_start.y + (int)((p_end.y - p_start.y) * t);
    return p;
}

Vector vector_between(Point p_start, Point p_end)
{
    Vector v;
    v.x = p_end.x - p_start.x;
    v.y = p_end.y - p_start.y;
    return v;
}

Vector vector_add(Vector v1, Vector v2)
{
    Vector v;
    v.x = v1.x + v2.x;
    v.y = v1.y + v2.y;
    return v;
}

int64_t vector_length_squared(Vector v)
{
    return (int64_t)v.x * v.x + (int64_t)v.y * v.y;
}

void draw_life_transformation(dgx_screen_t *screen, float t, const LifeGeneration *current, const LifeGeneration *next)
{
    int screen_cell_size = min_int(screen->width / current->width, screen->height / current->height);
    if (screen_cell_size % 2 == 0) screen_cell_size--; // Ensure the cell size is odd
    int offset_x      = (screen->width - screen_cell_size * current->width) / 2;
    int offset_y      = (screen->height - screen_cell_size * current->height) / 2;
    int center_offset = screen_cell_size / 2;
    dgx_fill_rectangle(screen, 0, 0, screen->width, screen->height, DGX_BLACK(DGX_RGB_16));
    SegmentsInCell *active_segments = calloc(current->width * current->height, sizeof(SegmentsInCell));
    Point          *faded_points    = malloc(current->width * current->height * sizeof(Point)); // Allocate memory for faded points
    int             faded_count     = 0;
    static int      neibx[8]        = {-1, 0, 1, -1, 1, -1, 0, 1};
    static int      neiby[8]        = {-1, -1, -1, 0, 0, 1, 1, 1};
    for (int y = 0; y < current->height; y++) {
        for (int x = 0; x < current->width; x++) {
            uint8_t cell_case = current->cells[CELL_OFFSET(x, y, current->width)] + 2 * next->cells[CELL_OFFSET(x, y, next->width)];
            if (cell_case == 3 || cell_case == 0) continue; // no move
            // possible active vectors
            int active_neighbors = 0;
            for (int i = 0; i < 8; i++) {
                int nx = x + neibx[i];
                int ny = y + neiby[i];
                if (nx >= 0 && nx < current->width && ny >= 0 && ny < current->height) {
                    bool neighbor_is_source =
                        cell_case == 1 ? next->cells[CELL_OFFSET(nx, ny, next->width)] : current->cells[CELL_OFFSET(nx, ny, current->width)];
                    if (neighbor_is_source) {
                        SegmentsInCell *cell_segments = &active_segments[CELL_OFFSET(x, y, current->width)];
                        cell_segments->segments[cell_segments->count++] =
                            cell_case == 1 ? ((Segment){
                                                 {x * screen_cell_size + center_offset,  y * screen_cell_size + center_offset },
                                                 {nx * screen_cell_size + center_offset, ny * screen_cell_size + center_offset}
                        })
                                           : ((Segment){{nx * screen_cell_size + center_offset, ny * screen_cell_size + center_offset},
                                                        {x * screen_cell_size + center_offset, y * screen_cell_size + center_offset}});
                        active_neighbors++;
                    }
                }
            }
            if (active_neighbors == 0) {
                faded_points[faded_count++] = (Point){x * screen_cell_size, y * screen_cell_size};
            }
        }
    }
    for (int y = 0; y < current->height; y++) {
        for (int x = 0; x < current->width; x++) {
            for (int screen_x = x * screen_cell_size; screen_x < (x + 1) * screen_cell_size; screen_x++) {
                for (int screen_y = y * screen_cell_size; screen_y < (y + 1) * screen_cell_size; screen_y++) {
                    Vector v_result = {0, 0};
                    for (int nx = -1; nx <= 1; nx++) {
                        for (int ny = -1; ny <= 1; ny++) {
                            int cx = x + nx;
                            int cy = y + ny;
                            if (cx >= 0 && cx < current->width && cy >= 0 && cy < current->height) {
                                SegmentsInCell *cell_segments = &active_segments[CELL_OFFSET(cx, cy, current->width)];
                                for (int i = 0; i < cell_segments->count; i++) {
                                    Segment seg   = cell_segments->segments[i];
                                    Point   inner = inner_point(t, seg.start, seg.end);
                                    Vector  v     = {inner.x - screen_x, inner.y - screen_y};
                                    v_result      = vector_add(v_result, v);
                                }
                            }
                        }
                    }
                    int64_t magnitude = vector_length_squared(v_result);
                    if (magnitude == 0) {
                        magnitude = 1;
                    }
                    uint8_t  color_level = 255 / (1 + magnitude / 256);
                    uint16_t color       = dgx_rgb_to_16(color_level, color_level, color_level);
                    dgx_set_pixel(screen, offset_x + screen_x, offset_y + screen_y, color);
                }
            }
        }
    }
    // Free the allocated memory after use
    free(active_segments);
    free(faded_points);
}

void app_main(void)
{
    dgx_screen_t *screen = cyd_init_display();
    if (screen == NULL) {
        return;
    }

    dgx_screen_t *vscreen = dgx_vscreen_init(screen->width, screen->height, 16, DgxScreenRGB);
    if (vscreen == NULL) {
        ESP_LOGE(TAG, "virtual screen init failed");
        return;
    }

    ESP_LOGI(TAG, "CYD display initialized: %dx%d", screen->width, screen->height);
    LifeGeneration *step = create_initial_navy_life();
    while (true) {
        LifeGeneration *next       = next_generation(step);
        int64_t         start_time = esp_timer_get_time();
        while (esp_timer_get_time() - start_time < 1000000) {
            float t = (float)(esp_timer_get_time() - start_time) / 1000000.0f;
            draw_life_transformation(vscreen, t, step, next);
            dgx_vscreen_to_screen(screen, 0, 0, vscreen);
            vTaskDelay(1);
        }
        free(step);
        step = next;
    }
}
