#include "am_model.h"
#include "am_view.h"
#include "bsp_display_rounding.h"
#include "lvgl.h"
#include "src/misc/lv_text_private.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LV_FONT_DECLARE(am_font_16);

static uint16_t framebuffer[240 * 320];
static uint8_t draw_buffer[240 * 20 * 2];

static void validate_layout(const char *name)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_update_layout(screen);
    uint32_t count = lv_obj_get_child_count(screen);
    unsigned labels = 0;
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t *object = lv_obj_get_child(screen, i);
        lv_area_t area;
        lv_obj_get_coords(object, &area);
        assert(area.x1 >= 0 && area.y1 >= 0 && area.x2 < 240 && area.y2 < 320);
        if (!lv_obj_check_type(object, &lv_label_class)) continue;
        const char *text = lv_label_get_text(object);
        const lv_font_t *font = lv_obj_get_style_text_font(object, LV_PART_MAIN);
        assert(lv_obj_get_height(object) >= font->line_height);
        uint32_t cursor = 0;
        while (text[cursor]) {
            uint32_t codepoint = lv_text_encoded_next(text, &cursor);
            if (codepoint == '\n') continue;
            lv_font_glyph_dsc_t glyph = {0};
            if (!lv_font_get_glyph_dsc(font, &glyph, codepoint, 0) || glyph.is_placeholder) {
                fprintf(stderr, "%s missing glyph U+%04X\n", name, (unsigned)codepoint);
                assert(false);
            }
        }
        if (!text[0]) continue;
        labels++;
        for (uint32_t j = i + 1; j < count; j++) {
            lv_obj_t *other = lv_obj_get_child(screen, j);
            if (!lv_obj_check_type(other, &lv_label_class) || !lv_label_get_text(other)[0]) continue;
            lv_area_t second;
            lv_obj_get_coords(other, &second);
            assert(area.x1 > second.x2 || area.x2 < second.x1 || area.y1 > second.y2 || area.y2 < second.y1);
        }
    }
    lv_mem_monitor_t memory;
    lv_mem_monitor(&memory);
    printf("Layout %s: %u labels, bounds/overlap/glyphs PASS, LVGL free %zu, largest %zu, peak %zu/%zu\n",
        name, labels, memory.free_size, memory.free_biggest_size, memory.max_used, memory.total_size);
}

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    int width = area->x2 - area->x1 + 1;
    int stride = lv_draw_buf_width_to_stride(width, LV_COLOR_FORMAT_RGB565);
    for (int y = area->y1; y <= area->y2; y++) {
        const uint16_t *row = (const uint16_t *)(pixels + (y - area->y1) * stride);
        for (int x = area->x1; x <= area->x2; x++) {
            framebuffer[y * 240 + x] = bsp_display_pixel_outside_rounded_rect(x, y, 240, 320, 30)
                ? 0 : row[x - area->x1];
        }
    }
    lv_display_flush_ready(display);
}

static void dump(lv_display_t *display, const char *directory, const char *name)
{
    validate_layout(name);
    lv_refr_now(display);
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.ppm", directory, name);
    FILE *file = fopen(path, "wb");
    assert(file);
    fprintf(file, "P6\n240 320\n255\n");
    unsigned distinct = 0;
    for (size_t i = 0; i < 240 * 320; i++) {
        uint16_t color = framebuffer[i];
        unsigned char rgb[] = {
            (unsigned char)(((color >> 11) & 31) * 255 / 31),
            (unsigned char)(((color >> 5) & 63) * 255 / 63),
            (unsigned char)((color & 31) * 255 / 31)
        };
        assert(fwrite(rgb, 1, sizeof(rgb), file) == sizeof(rgb));
        if (color != framebuffer[160 * 240 + 120] && color != 0) distinct++;
    }
    assert(distinct > 300);
    fclose(file);
    printf("Rendered %s: %u foreground pixels\n", name, distinct);
}

static void font_probe(void)
{
    const uint32_t positive[] = {0x4e2d, 0x6587, 0x8fd0, 0x884c, 0x4efb, 0x52a1, 0x989d, 0x5ea6, 0x9f99, 0x3002, 0x2191};
    for (size_t i = 0; i < sizeof(positive) / sizeof(positive[0]); i++) {
        lv_font_glyph_dsc_t glyph = {0};
        assert(lv_font_get_glyph_dsc(&am_font_16, &glyph, positive[i], 0) && !glyph.is_placeholder);
    }
    lv_font_glyph_dsc_t missing = {0};
    assert(!lv_font_get_glyph_dsc(&am_font_16, &missing, 0x1f680, 0) || missing.is_placeholder);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    assert(argc == 3);
    FILE *fixture = fopen(argv[1], "rb");
    assert(fixture);
    char line[AM_LINE_MAX + 1] = {0};
    size_t length = fread(line, 1, AM_LINE_MAX, fixture);
    fclose(fixture);
    while (length > 0 && line[length - 1] == '\n') line[--length] = '\0';
    static am_model_t model;
    am_model_init(&model);
    am_effect_t effect;
    assert(am_protocol_apply(&model, line, length, AM_USB, 1000, &effect));
    assert(model.snapshot.task_count >= 3 && model.snapshot.quota_count >= 3 && model.snapshot.alert_count >= 3);
    static am_model_t baseline;
    baseline = model;

    lv_init();
    lv_display_t *display = lv_display_create(240, 320);
    assert(display);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, flush);
    lv_display_set_buffers(display, draw_buffer, NULL, sizeof(draw_buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    font_probe();
    am_view_render(&model, 1000, 86, 70, 20, 0, NULL);
    dump(display, argv[2], "overview");
    model.page = AM_PAGE_NEWS;
    am_view_render(&model, 1000, 86, 70, 20, 0, NULL);
    dump(display, argv[2], "news-direct");
    model.page = AM_PAGE_TASKS;
    am_view_render(&model, 1000, 86, 70, 20, 0, NULL);
    dump(display, argv[2], "tasks");
    model.selected = 1;
    am_view_render(&model, 1000, 86, 70, 20, 0, NULL);
    dump(display, argv[2], "tasks-focus");
    model.selected = 0;
    model.page = AM_PAGE_TASK_DETAIL;
    am_view_render(&model, 1000, 86, 70, 20, 0, NULL);
    dump(display, argv[2], "task-detail");
    model.detail_page = 1;
    am_view_render(&model, 1000, 86, 70, 20, 0, NULL);
    dump(display, argv[2], "task-tokens");
    model.detail_index = 1;
    am_view_render(&model, 1000, 86, 70, 20, 0, NULL);
    dump(display, argv[2], "task-tokens-null");
    model.detail_index = 2;
    am_view_render(&model, 1000, 0, 70, 20, 0, NULL);
    dump(display, argv[2], "task-tokens-zero");
    model.detail_index = 0;
    model.page = AM_PAGE_ALERTS;
    am_view_render(&model, 1000, 86, 70, 20, 0, NULL);
    dump(display, argv[2], "alerts");
    char ack_id[AM_ID_MAX];
    assert(am_model_ack(&model, 0, ack_id, sizeof(ack_id)));
    am_view_render(&model, 1000, 86, 70, 20, 0, NULL);
    dump(display, argv[2], "alerts-pending");
    model.page = AM_PAGE_QUOTAS;
    model.detail_page = 0;
    am_view_render(&model, 1000, -1, 70, 20, 0, NULL);
    dump(display, argv[2], "quotas");
    model.detail_page = 1;
    am_view_render(&model, 1000, 86, 70, 20, 0, NULL);
    dump(display, argv[2], "quota-source");
    model.detail_page = 2;
    am_utf8_copy(model.snapshot.news_title, sizeof(model.snapshot.news_title), "Tibo · 重置公告待确认");
    am_utf8_copy(model.snapshot.news_state, sizeof(model.snapshot.news_state), "available");
    am_utf8_copy(model.snapshot.news_timing, sizeof(model.snapshot.news_timing), "Lands tomorrow before 10am PST.");
    model.snapshot.news_published_at = 1792652154;
    am_view_render(&model, 1000, 86, 70, 20, 0, NULL);
    dump(display, argv[2], "reset-announcement");
    model.page = AM_PAGE_NEWS;
    model.detail_page = 0;
    am_view_render(&model, 1000, 86, 70, 20, 0, NULL);
    dump(display, argv[2], "news-direct-current");
    model.page = AM_PAGE_OVERVIEW;
    model.selected = 2;
    am_view_render(&model, 1000, 86, 70, 20, 0, NULL);
    dump(display, argv[2], "overview-news");
    model.page = AM_PAGE_QUOTAS;
    model.detail_page = 2;
    am_utf8_copy(model.snapshot.news_state, sizeof(model.snapshot.news_state), "stale");
    am_view_render(&model, 1000, 86, 70, 20, 0, NULL);
    dump(display, argv[2], "reset-announcement-stale");
    model.detail_page = 0;
    model.selected = 1;
    model.detail_page = 0;
    am_view_render(&model, 1000, 86, 70, 20, 0, NULL);
    dump(display, argv[2], "quota-unavailable");
    model.detail_page = 1;
    am_view_render(&model, 1000, 86, 70, 20, 0, NULL);
    dump(display, argv[2], "quota-unavailable-source");
    model.selected = 2;
    model.detail_page = 0;
    am_view_render(&model, 1000, 86, 70, 20, 0, NULL);
    dump(display, argv[2], "quota-week");
    model.page = AM_PAGE_SETTINGS;
    model.selected = 0;
    am_view_render(&model, 1000, 86, 70, 20, 0, NULL);
    dump(display, argv[2], "settings");
    am_model_set_mute(&model, true);
    am_view_render(&model, 1000, 86, 100, 0, 0, NULL);
    dump(display, argv[2], "settings-muted-pending");
    am_view_render(&model, 1000, 86, 70, 20, 123456, NULL);
    dump(display, argv[2], "pairing");
    model.page = AM_PAGE_OVERVIEW;
    am_view_render(&model, 50000, 86, 70, 20, 0, NULL);
    dump(display, argv[2], "offline");
    model.transport = AM_BLE;
    am_view_render(&model, 1000, 86, 70, 20, 0, NULL);
    dump(display, argv[2], "ble-online");
    model.transport = AM_USB;
    am_view_render(&model, 1000, 86, 70, 20, 0, &model.snapshot.alerts[0]);
    dump(display, argv[2], "notification");
    model.page = AM_PAGE_TASK_DETAIL;
    model.detail_page = 0;
    am_utf8_copy(model.snapshot.tasks[0].title, sizeof(model.snapshot.tasks[0].title),
        "这是一条用于验证中文长标题自动截断和多任务查看的任务");
    am_utf8_copy(model.snapshot.tasks[0].project, sizeof(model.snapshot.tasks[0].project),
        "very-long-project-name-for-layout-verification");
    am_view_render(&model, 1000, 0, 70, 20, 0, NULL);
    dump(display, argv[2], "long-title");
    model.snapshot.tasks[0].project[0] = '\0';
    model.snapshot.tasks[0].tokens.output = 0;
    am_view_render(&model, 1000, 0, 70, 20, 0, NULL);
    dump(display, argv[2], "empty-project-zero");
    model.page = AM_PAGE_QUOTAS;
    model.snapshot.quotas[0].used_percent = 0;
    model.snapshot.quotas[0].observed_at = model.snapshot.sent_at;
    am_utf8_copy(model.snapshot.quotas[0].state, sizeof(model.snapshot.quotas[0].state), "available");
    model.snapshot.quotas[0].detail[0] = '\0';
    am_view_render(&model, 1000, 0, 70, 20, 0, NULL);
    dump(display, argv[2], "zero-quota");
    model.page = AM_PAGE_TASKS;
    model.snapshot.task_count = 0;
    am_view_render(&model, 1000, 0, 70, 20, 0, NULL);
    dump(display, argv[2], "empty-tasks");
    model.page = AM_PAGE_ALERTS;
    model.snapshot.alert_count = 0;
    am_view_render(&model, 1000, 0, 70, 20, 0, NULL);
    dump(display, argv[2], "empty-alerts");
    model.page = AM_PAGE_QUOTAS;
    model.snapshot.quota_count = 0;
    am_view_render(&model, 1000, 0, 70, 20, 0, NULL);
    dump(display, argv[2], "empty-quotas");
    model = baseline;
    model.snapshot.task_count = AM_TASK_MAX;
    for (size_t i = 3; i < AM_TASK_MAX; i++) {
        model.snapshot.tasks[i] = model.snapshot.tasks[i % 3];
        snprintf(model.snapshot.tasks[i].id, AM_ID_MAX, "ui-task-%u", (unsigned)i);
    }
    model.selected = AM_TASK_MAX - 1;
    model.page = AM_PAGE_TASKS;
    am_view_render(&model, 1000, 86, 70, 20, 0, NULL);
    dump(display, argv[2], "tasks-last-page");
    model = baseline;
    model.snapshot.running = 0;
    model.snapshot.needs_action = 0;
    model.snapshot.completed_today = 0;
    model.page = AM_PAGE_OVERVIEW;
    am_view_render(&model, 1000, 0, 70, 20, 0, NULL);
    dump(display, argv[2], "overview-zero");
    am_model_init(&model);
    am_view_render(&model, 1000, -1, 70, 20, 0, NULL);
    dump(display, argv[2], "waiting");
    model = baseline;
    for (unsigned i = 0; i < 200; i++) {
        model.page = (am_page_t)(i % 7);
        model.detail_page = i % 2;
        model.selected = 0;
        am_view_render(&model, 1000 + i * 100, 86, 70, 20, 0, NULL);
        lv_refr_now(display);
    }
    validate_layout("navigation-soak-200");
    return 0;
}
