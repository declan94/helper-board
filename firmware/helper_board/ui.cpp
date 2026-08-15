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

// 正文区:上边贴着页签下沿(56+34=90),下边留出页脚(底部 16px 一行 = 272 起)
#define CONTENT_TOP 98
#define CONTENT_H 170
#define CONTENT_W (LCD_WIDTH - 48)
#define CONTENT_X 24

// 字号阶梯(由大到小)。LVGL 文本高度 = n*(line_height+line_space) - line_space,
// 按 CONTENT_H=170 算整行容量:22px→5 行,19px→6 行,16px→7 行,13px→9 行
struct FontStep {
  const lv_font_t *font;
  int lineSpace;
  int mealGap;  // "明日"页餐与餐之间的间距
};
static const FontStep FONT_STEPS[] = {
  { &font_cjk_22, 4, 12 },
  { &font_cjk_19, 3, 10 },
  { &font_cjk_16, 3, 10 },
  { &font_cjk_13, 2, 6 },
};
static const int FONT_STEP_COUNT = sizeof(FONT_STEPS) / sizeof(FONT_STEPS[0]);

// 按某一档位排版标签,返回换行后的实际高度
static int measureAtStep(lv_obj_t *label, int step) {
  lv_obj_set_style_text_font(label, FONT_STEPS[step].font, 0);
  lv_obj_set_style_text_line_space(label, FONT_STEPS[step].lineSpace, 0);
  lv_obj_set_width(label, CONTENT_W);
  lv_obj_set_height(label, LV_SIZE_CONTENT);
  lv_obj_update_layout(label);
  return lv_obj_get_height(label);
}

static void buildContent(lv_obj_t *scr, const UiModel *m) {
  char body[720];

  if (m->page == PAGE_TOMORROW) {
    if (!m->menu.tomorrow.valid) {
      lv_obj_t *label = mkLabel(scr, &font_cjk_22, "No menu for tomorrow yet");
      lv_obj_align(label, LV_ALIGN_CENTER, 0, 20);
      return;
    }
    // 三餐各一个标签:餐内换行紧凑,餐与餐之间留半行
    static const char *NAMES[3] = { "Breakfast", "Lunch", "Dinner" };
    const char *meals[3] = { m->menu.tomorrow.breakfast, m->menu.tomorrow.lunch,
                             m->menu.tomorrow.dinner };
    lv_obj_t *labels[3];
    for (int i = 0; i < 3; i++) {
      char inlined[300];
      inlineMeal(meals[i], inlined, sizeof(inlined));
      snprintf(body, sizeof(body), "%s: %s", NAMES[i], inlined);
      labels[i] = mkLabel(scr, &font_cjk_22, body);
      lv_label_set_long_mode(labels[i], LV_LABEL_LONG_WRAP);
    }
    // 三餐合起来自适应:含餐间间距的总高必须落进正文区。
    // 和今日页一样从最大档起步——明日页的菜品被 inlineMeal 压成了段落而不是
    // 一菜一行,内容往往只有三四行,写死从 16px 起步会白白浪费半屏。
    int step = 0, heights[3], total = 0;
    for (;;) {
      total = FONT_STEPS[step].mealGap * 2;
      for (int i = 0; i < 3; i++) {
        heights[i] = measureAtStep(labels[i], step);
        total += heights[i];
      }
      if (total <= CONTENT_H || step >= FONT_STEP_COUNT - 1) break;
      step++;
    }
    // 余下的空白上下均分,三餐居中而不是顶头堆着、底下空一块
    int y = CONTENT_TOP + (total < CONTENT_H ? (CONTENT_H - total) / 2 : 0);
    for (int i = 0; i < 3; i++) {
      lv_obj_set_pos(labels[i], CONTENT_X, y);
      y += heights[i] + FONT_STEPS[step].mealGap;
    }
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

  lv_obj_t *label = mkLabel(scr, &font_cjk_22, text);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  if (!has) {
    lv_obj_set_style_text_line_space(label, 4, 0);
    lv_obj_set_width(label, CONTENT_W);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 20);  // 占位提示居中
    return;
  }

  // 菜品多时逐档缩字号:22px(≤5 行)→ 19px(6 行)→ 16px → 13px。
  // 最小档仍放不下则固定高度裁剪,保证不压到页脚
  int step = 0;
  while (measureAtStep(label, step) > CONTENT_H && step < FONT_STEP_COUNT - 1) step++;
  lv_obj_set_size(label, CONTENT_W, CONTENT_H);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, CONTENT_X, CONTENT_TOP);
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
  if (m->syncing) {
    lv_obj_t *sync = mkLabel(scr, &font_cjk_16, "Updating...");
    lv_obj_align(sync, LV_ALIGN_BOTTOM_RIGHT, -12, -8);
    return;
  }
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
  // 呼叫通道长时间没通就必须让人看见:呼叫方在外面按了铃却石沉大海是最糟的
  // 失效方式,屏幕是唯一能让屋里的人发现并告知的渠道。
  if (m->timeValid && m->menu.lastSync > 0) {
    if (m->callChannelOkAt == 0 || m->nowEpoch - m->callChannelOkAt > CALL_CHANNEL_STALE_SEC) {
      char tmp[96];
      snprintf(tmp, sizeof(tmp), "CALL OFFLINE! %s", stat);
      strlcpy(stat, tmp, sizeof(stat));
    }
  }
  if (m->batt.percent <= BAT_LOW_WARN_PCT && !m->batt.plugged) {
    char tmp[96];
    snprintf(tmp, sizeof(tmp), "LOW BATTERY! %s", stat);
    strlcpy(stat, tmp, sizeof(stat));
  }
  lv_obj_t *sync = mkLabel(scr, &font_cjk_16, stat);
  lv_obj_align(sync, LV_ALIGN_BOTTOM_RIGHT, -12, -8);
}

static lv_display_t *sDisp = NULL;

// lv_init + display 注册只做一次;每次渲染前清空重建屏幕内容
static lv_obj_t *uiBegin() {
  if (!sDisp) {
    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return millis(); });
    sDisp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
    lv_display_set_flush_cb(sDisp, flushCb);
    size_t bufSize = LCD_WIDTH * LCD_HEIGHT * 2;  // RGB565 全帧
    uint8_t *buf = (uint8_t *)heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM);
    assert(buf);
    lv_display_set_buffers(sDisp, buf, NULL, bufSize, LV_DISPLAY_RENDER_MODE_FULL);
  }
  lv_obj_t *scr = lv_screen_active();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  return scr;
}

void Ui_Splash(DisplayPort *port, const char *text) {
  sPort = port;
  lv_obj_t *scr = uiBegin();
  lv_obj_t *label = mkLabel(scr, &font_cjk_22, text);
  lv_obj_center(label);
  lv_refr_now(sDisp);
}

void Ui_CallScreen(DisplayPort *port, const char *text, time_t sentAt, bool timeValid) {
  sPort = port;
  lv_obj_t *scr = uiBegin();

  // 顶部:铃铛 + 发出时刻。整屏最醒目的一行。
  char head[48];
  if (timeValid && sentAt > 0) {
    struct tm st;
    localtime_r(&sentAt, &st);
    snprintf(head, sizeof(head), LV_SYMBOL_BELL "  CALL  %02d:%02d", st.tm_hour, st.tm_min);
  } else {
    snprintf(head, sizeof(head), LV_SYMBOL_BELL "  CALL");
  }
  lv_obj_t *title = mkLabel(scr, &font_cjk_22, head);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 22);

  lv_obj_t *line = lv_obj_create(scr);
  lv_obj_remove_style_all(line);
  lv_obj_set_style_bg_color(line, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
  lv_obj_set_size(line, LCD_WIDTH - 40, 2);
  lv_obj_align(line, LV_ALIGN_TOP_MID, 0, 60);

  // 正文:留言可能是中文,字库是 GB2312 全字集,直接渲染。
  // 长留言逐档缩字号,复用正文区那套阶梯。
  const int bodyTop = 78, bodyH = 160;
  lv_obj_t *body = mkLabel(scr, &font_cjk_22, (text && text[0]) ? text : "(no message)");
  lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
  int step = 0;
  while (measureAtStep(body, step) > bodyH && step < FONT_STEP_COUNT - 1) step++;
  lv_obj_set_size(body, CONTENT_W, bodyH);
  lv_obj_align(body, LV_ALIGN_TOP_MID, 0, bodyTop);

  lv_obj_t *hint = mkLabel(scr, &font_cjk_16, "Press KEY to stop the ring");
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -14);

  lv_refr_now(sDisp);
}

void Ui_RenderAll(DisplayPort *port, const UiModel *m) {
  sPort = port;
  lv_obj_t *scr = uiBegin();

  buildStatusBar(scr, m);
  buildTabs(scr, m->page);
  buildContent(scr, m);
  buildFooter(scr, m);

  lv_refr_now(sDisp);  // 同步渲染并触发 flush → 上屏
}
