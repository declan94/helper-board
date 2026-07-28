#include <Arduino.h>
#include <Preferences.h>
#include "menu_store.h"
#include "config.h"

static const char *NVS_NS = "menu";

static void loadRow(Preferences &prefs, int idx, DayMenu *row) {
  char key[8];
  memset(row, 0, sizeof(*row));
  snprintf(key, sizeof(key), "d%d", idx);
  if (prefs.getString(key, row->date, sizeof(row->date)) == 0) return;
  snprintf(key, sizeof(key), "b%d", idx);
  prefs.getString(key, row->breakfast, sizeof(row->breakfast));
  snprintf(key, sizeof(key), "l%d", idx);
  prefs.getString(key, row->lunch, sizeof(row->lunch));
  snprintf(key, sizeof(key), "e%d", idx);
  prefs.getString(key, row->dinner, sizeof(row->dinner));
  snprintf(key, sizeof(key), "n%d", idx);
  prefs.getString(key, row->note, sizeof(row->note));
  row->valid = true;
}

static void saveRow(Preferences &prefs, int idx, const DayMenu *row) {
  char key[8];
  snprintf(key, sizeof(key), "d%d", idx);
  prefs.putString(key, row->valid ? row->date : "");
  snprintf(key, sizeof(key), "b%d", idx);
  prefs.putString(key, row->breakfast);
  snprintf(key, sizeof(key), "l%d", idx);
  prefs.putString(key, row->lunch);
  snprintf(key, sizeof(key), "e%d", idx);
  prefs.putString(key, row->dinner);
  snprintf(key, sizeof(key), "n%d", idx);
  prefs.putString(key, row->note);
}

void MenuStore_Load(MenuData *out, const char *todayStr, const char *tomorrowStr) {
  memset(out, 0, sizeof(*out));
  Preferences prefs;
  if (!prefs.begin(NVS_NS, true)) return;
  DayMenu rows[2];
  loadRow(prefs, 0, &rows[0]);
  loadRow(prefs, 1, &rows[1]);
  out->lastSync = (time_t)prefs.getLong64("syncts", 0);
  prefs.end();

  for (int i = 0; i < 2; i++) {
    if (!rows[i].valid) continue;
    if (strcmp(rows[i].date, todayStr) == 0) out->today = rows[i];
    else if (strcmp(rows[i].date, tomorrowStr) == 0) out->tomorrow = rows[i];
  }
}

void MenuStore_Save(const MenuData *data) {
  Preferences prefs;
  if (!prefs.begin(NVS_NS, false)) return;
  saveRow(prefs, 0, &data->today);
  saveRow(prefs, 1, &data->tomorrow);
  prefs.putLong64("syncts", (int64_t)data->lastSync);
  prefs.end();
}

MenuPage MenuPage_DefaultFor(int minuteOfDay) {
  if (minuteOfDay >= TOMORROW_PREVIEW_START_MIN) return PAGE_TOMORROW;
  if (minuteOfDay >= MEAL_DINNER_START_MIN) return PAGE_DINNER;
  if (minuteOfDay >= MEAL_LUNCH_START_MIN) return PAGE_LUNCH;
  if (minuteOfDay >= MEAL_BREAKFAST_START_MIN) return PAGE_BREAKFAST;
  return PAGE_BREAKFAST;  // 凌晨也显示早餐
}
