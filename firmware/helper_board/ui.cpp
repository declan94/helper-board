#include <Arduino.h>
#include <lvgl.h>
#include <esp_heap_caps.h>
#include "ui.h"
#include "config.h"
#include "src/fonts/fonts.h"

static DisplayPort *sPort = NULL;

static void flushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  uint16_t *buffer = (uint16_t *)px_map;
  for (int y = area->y1; y <= area->y2; y++) {
    for (int x = area->x1; x <= area->x2; x++) {
      sPort->RLCD_SetPixel(x, y, (*buffer < 0x7fff) ? ColorBlack : ColorWhite);
      buffer++;
    }
  }
  sPort->RLCD_Display();
  lv_display_flush_ready(disp);
}

static lv_obj_t *mkLabel(lv_obj_t *parent, const lv_font_t *font, const char *text) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_black(), 0);
  lv_label_set_text(l, text);
  return l;
}

static const char *WEEKDAYS[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char *MONTHS[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

// 电量图标(montserrat 内置符号)
static const char *battSymbol(const BatteryState *b) {
  if (b->plugged) return LV_SYMBOL_CHARGE;
  if (b->percent > 85) return LV_SYMBOL_BATTERY_FULL;
  if (b->percent > 60) return LV_SYMBOL_BATTERY_3;
  if (b->percent > 35) return LV_SYMBOL_BATTERY_2;
  if (b->percent > 10) return LV_SYMBOL_BATTERY_1;
  return LV_SYMBOL_BATTERY_EMPTY;
}

// 菜单文本清洗:去掉首尾空白;空内容返回 false
static bool mealText(const char *raw, char *out, size_t outLen) {
  while (*raw == ' ' || *raw == '\n' || *raw == '\r' || *raw == '\t') raw++;
  strlcpy(out, raw, outLen);
  int n = strlen(out);
  while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == '\t')) out[--n] = '\0';
  return n > 0;
}

// "明日"页把每餐多行菜品压成一行(换行改顿号)
static void inlineMeal(const char *raw, char *out, size_t outLen) {
  char tmp[256];
  if (!mealText(raw, tmp, sizeof(tmp))) {
    strlcpy(out, "(not filled)", outLen);
    return;
  }
  out[0] = '\0';
  bool prevBreak = false;
  size_t o = 0;
  for (const char *p = tmp; *p && o + 3 < outLen; p++) {
    if (*p == '\n' || *p == '\r') {
      prevBreak = true;
      continue;
    }
    if (prevBreak) {
      out[o++] = ',';
      out[o++] = ' ';
      prevBreak = false;
    }
    out[o++] = *p;
  }
  out[o] = '\0';
}

static void buildStatusBar(lv_obj_t *scr, const UiModel *m) {
  char buf[64];

  // 日期(不显示时钟——电池方案下时钟无法实时走字)
  if (m->timeValid) {
    snprintf(buf, sizeof(buf), "%s %d, %s", MONTHS[m->now.tm_mon % 12], m->now.tm_mday,
             WEEKDAYS[m->now.tm_wday % 7]);
  } else {
    snprintf(buf, sizeof(buf), "Date unknown");
  }
  lv_obj_t *dateLabel = mkLabel(scr, &font_cjk_22, buf);
  lv_obj_align(dateLabel, LV_ALIGN_TOP_LEFT, 12, 10);

  // 电量:百分比 + 符号(符号固定在最右,百分比向左伸展,避免重叠)
  lv_obj_t *battIcon = mkLabel(scr, &lv_font_montserrat_16, battSymbol(&m->batt));
  lv_obj_align(battIcon, LV_ALIGN_TOP_RIGHT, -10, 13);
  snprintf(buf, sizeof(buf), "%d%%", m->batt.percent);
  lv_obj_t *battLabel = mkLabel(scr, &font_cjk_22, buf);
  lv_obj_align(battLabel, LV_ALIGN_TOP_RIGHT, -34, 10);

  // 分隔线
  lv_obj_t *line = lv_obj_create(scr);
  lv_obj_remove_style_all(line);
  lv_obj_set_style_bg_color(line, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
  lv_obj_set_size(line, LCD_WIDTH - 20, 2);
  lv_obj_align(line, LV_ALIGN_TOP_MID, 0, 44);
}

static void buildTabs(lv_obj_t *scr, MenuPage active) {
  static const char *NAMES[PAGE_COUNT] = { "Breakfast", "Lunch", "Dinner", "Tomorrow" };
  const int tabW = 92, tabH = 34, gap = 6;
  const int totalW = tabW * PAGE_COUNT + gap * (PAGE_COUNT - 1);
  int x0 = (LCD_WIDTH - totalW) / 2;
  for (int i = 0; i < PAGE_COUNT; i++) {
    lv_obj_t *tab = lv_obj_create(scr);
    lv_obj_remove_style_all(tab);
    lv_obj_set_size(tab, tabW, tabH);
    lv_obj_set_pos(tab, x0 + i * (tabW + gap), 56);
    lv_obj_set_style_radius(tab, 6, 0);
    bool isActive = (i == (int)active);
    lv_obj_set_style_bg_color(tab, isActive ? lv_color_black() : lv_color_white(), 0);
    lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(tab, lv_color_black(), 0);
    lv_obj_set_style_border_width(tab, isActive ? 0 : 1, 0);

    // "Breakfast/Tomorrow" 在 22px 下超出 92px 页签宽,页签用 16px
    lv_obj_t *l = mkLabel(tab, &font_cjk_16, NAMES[i]);
    lv_obj_set_style_text_color(l, isActive ? lv_color_white() : lv_color_black(), 0);
    lv_obj_center(l);
  }
}

static void buildContent(lv_obj_t *scr, const UiModel *m) {
  char body[720];

  if (m->page == PAGE_TOMORROW) {
    char b[300], l[300], d[300];
    if (m->menu.tomorrow.valid) {
      inlineMeal(m->menu.tomorrow.breakfast, b, sizeof(b));
      inlineMeal(m->menu.tomorrow.lunch, l, sizeof(l));
      inlineMeal(m->menu.tomorrow.dinner, d, sizeof(d));
      snprintf(body, sizeof(body), "Breakfast: %s\n\nLunch: %s\n\nDinner: %s", b, l, d);
    } else {
      strlcpy(body, "No menu for tomorrow yet", sizeof(body));
    }
    lv_obj_t *label = mkLabel(scr, &font_cjk_22, body);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LCD_WIDTH - 32);
    lv_obj_set_style_text_line_space(label, 6, 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 16, 106);
    return;
  }

  const char *raw = NULL;
  if (m->menu.today.valid) {
    switch (m->page) {
      case PAGE_BREAKFAST: raw = m->menu.today.breakfast; break;
      case PAGE_LUNCH: raw = m->menu.today.lunch; break;
      default: raw = m->menu.today.dinner; break;
    }
  }

  char text[512];
  bool has = raw && mealText(raw, text, sizeof(text));
  if (!has)
    strlcpy(text, m->menu.today.valid ? "This meal is not filled in" : "No menu for today yet",
            sizeof(text));

  // 22px + 行距 4:内容区可容纳 5 行整行,超出部分裁剪以保护页脚
  lv_obj_t *label = mkLabel(scr, &font_cjk_22, text);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_line_space(label, 4, 0);
  if (has) {
    lv_obj_set_size(label, LCD_WIDTH - 48, 164);  // 固定高度,超出自动裁剪
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 24, 102);
  } else {
    lv_obj_set_width(label, LCD_WIDTH - 48);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 20);  // 占位提示居中
  }
}

static void buildFooter(lv_obj_t *scr, const UiModel *m) {
  char buf[160];

  // 左:当前页对应的备注
  const DayMenu *day = (m->page == PAGE_TOMORROW) ? &m->menu.tomorrow : &m->menu.today;
  if (day->valid && day->note[0]) {
    snprintf(buf, sizeof(buf), "Note: %s", day->note);
    lv_obj_t *note = mkLabel(scr, &font_cjk_16, buf);
    lv_label_set_long_mode(note, LV_LABEL_LONG_DOT);
    lv_obj_set_width(note, 150);
    lv_obj_align(note, LV_ALIGN_BOTTOM_LEFT, 12, -8);
  }

  // 右:同步状态
  char stat[80];
  if (m->menu.lastSync > 0) {
    struct tm st;
    localtime_r(&m->menu.lastSync, &st);
    if (m->syncAttempted && !m->syncOk) {
      snprintf(stat, sizeof(stat), "Sync failed (%02d-%02d %02d:%02d)",
               st.tm_mon + 1, st.tm_mday, st.tm_hour, st.tm_min);
    } else {
      snprintf(stat, sizeof(stat), "Updated %02d-%02d %02d:%02d",
               st.tm_mon + 1, st.tm_mday, st.tm_hour, st.tm_min);
    }
  } else {
    strlcpy(stat, m->syncAttempted ? "Sync failed" : "Not synced", sizeof(stat));
  }
  if (m->batt.percent <= BAT_LOW_WARN_PCT && !m->batt.plugged) {
    char tmp[96];
    snprintf(tmp, sizeof(tmp), "LOW BATTERY! %s", stat);
    strlcpy(stat, tmp, sizeof(stat));
  }
  lv_obj_t *sync = mkLabel(scr, &font_cjk_16, stat);
  lv_obj_align(sync, LV_ALIGN_BOTTOM_RIGHT, -12, -8);
}

void Ui_RenderAll(DisplayPort *port, const UiModel *m) {
  sPort = port;
  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return millis(); });

  lv_display_t *disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
  lv_display_set_flush_cb(disp, flushCb);
  size_t bufSize = LCD_WIDTH * LCD_HEIGHT * 2;  // RGB565 全帧
  uint8_t *buf = (uint8_t *)heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM);
  assert(buf);
  lv_display_set_buffers(disp, buf, NULL, bufSize, LV_DISPLAY_RENDER_MODE_FULL);

  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  buildStatusBar(scr, m);
  buildTabs(scr, m->page);
  buildContent(scr, m);
  buildFooter(scr, m);

  lv_refr_now(disp);  // 同步渲染并触发 flush → 上屏
}
