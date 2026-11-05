#pragma once
#include <Arduino.h>
#include <ESP8266WiFi.h>

// Settings 1.4.3: встроенный ESP8266WebServer, без GyverDB и Table (экономия RAM)
#define SETT_NO_DB
#define SETT_NO_TABLE
#include <SettingsESP.h>

// ===================== Веб-интерфейс настроек =====================
// Типы Settings и билдеры живут только в этом заголовке: авто-прототипы
// Arduino для второго .ino ломаются на типах sets::.
SettingsESP sett("GyverLamp2");

// ID скрытого виджета Confirm (уникален, не пересекается с авто-ID UINT32_MAX-...)
static const size_t WEB_ID_CONFIRM_FACTORY = 0x7E57C0DEUL;

// -------------------- отложенные действия --------------------
// Никогда не перезагружаем и не пишем флеш внутри builder-колбэка:
// действие ставится в очередь и выполняется из loop() после sett.tick()
enum WebAction : uint8_t {
  WEB_ACT_NONE = 0,
  WEB_ACT_SAVE,
  WEB_ACT_REBOOT,
  WEB_ACT_FACTORY_RESET,
};
static WebAction webAction = WEB_ACT_NONE;
static uint32_t webActionMs = 0;
static bool webFactoryAsk = false;   // показать Confirm сброса
static bool webSavedNotice = false;  // показать уведомление «Сохранено»

static void webui_startAction(WebAction action) {
  webAction = action;
  webActionMs = millis();
}

// -------------------- страница «Статус» --------------------
static void webui_buildStatus(sets::Builder& b) {
  {
    sets::Menu m(b, "Статус");
    {
      sets::Group g(b, "Устройство");
      b.Label("Версия", "GyverLamp2 v0.23b");
      b.Label("Режим", cfg.WiFimode ? "Локальная сеть (STA)" : "Точка доступа (AP)");
      if (cfg.WiFimode) {
        if (WiFi.status() == WL_CONNECTED) b.Label("IP-адрес", WiFi.localIP().toString());
        else b.Label("IP-адрес", "нет подключения");
      } else {
        b.Label("IP-адрес", WiFi.softAPIP().toString());
      }
      b.LabelNum("Группа", cfg.group);
      b.Label("Роль", cfg.role ? "Ведущая" : "Ведомая");
      b.LabelNum("Текущий пресет", (uint32_t)(cfg.curPreset + 1));
      b.LabelNum("Всего пресетов", cfg.presetAmount);
    }
    {
      sets::Group g(b, "Система");
      b.Label("Хранилище", storage_ready() ? "готово" : "ошибка");
      b.LabelNum("Свободно RAM, Б", ESP.getFreeHeap());
      b.LabelNum("Время работы, с", (uint32_t)(millis() / 1000));
    }
  }
}

// -------------------- страница «Система» --------------------
static void webui_buildSystem(sets::Builder& b) {
  {
    sets::Menu m(b, "Система");
    {
      sets::Group g(b, "Сохранение");
      b.Paragraph("", "Изменения сохраняются автоматически через 30 секунд.");
      if (b.Button("Сохранить сейчас")) webui_startAction(WEB_ACT_SAVE);
    }
    {
      sets::Group g(b, "Перезагрузка");
      b.Paragraph("", "Перед перезагрузкой настройки будут сохранены.");
      if (b.Button("Перезагрузить")) webui_startAction(WEB_ACT_REBOOT);
    }
    {
      sets::Group g(b, "Сброс");
      b.Paragraph("", "Удаляет настройки, пресеты, рассвет и палитру. Отменить нельзя.");
      if (b.Button("Сброс настроек", sets::Colors::Red)) webFactoryAsk = true;
      bool res = false;
      if (b.Confirm(WEB_ID_CONFIRM_FACTORY, "Сбросить все настройки?", &res)) {
        if (res) webui_startAction(WEB_ACT_FACTORY_RESET);
      }
    }
    b.Paragraph("", "FastLED 3.7.7 · Settings 1.4.3 · " __DATE__);
  }
}

// -------------------- общий билдер (разбит на части: стек ESP8266) --------------------
static void webui_build(sets::Builder& b) {
  webui_buildStatus(b);
  webui_buildSystem(b);
}

// -------------------- обновления (запросы из вебморды) --------------------
static void webui_update(sets::Updater& upd) {
  if (webFactoryAsk) {
    webFactoryAsk = false;
    upd.confirm(WEB_ID_CONFIRM_FACTORY);
  }
  if (webSavedNotice) {
    webSavedNotice = false;
    upd.notice("Сохранено");
  }
}

// -------------------- выполнение отложенных действий --------------------
static void webui_runDeferred() {
  if (webAction == WEB_ACT_NONE) return;
  if ((uint32_t)(millis() - webActionMs) < 400) return;  // даём ответу уйти в браузер

  WebAction action = webAction;
  webAction = WEB_ACT_NONE;

  switch (action) {
    case WEB_ACT_SAVE:
      storage_saveAll(true);
      webSavedNotice = true;
      break;
    case WEB_ACT_REBOOT:
      storage_saveAll(true);
      ESP.restart();
      break;
    case WEB_ACT_FACTORY_RESET:
      storage_resetAll();
      ESP.restart();
      break;
    default:
      break;
  }
}

// -------------------- запуск / тикер --------------------
void webui_begin() {
  sett.setVersion("0.23b");
  sett.setPacketSize(512);
  sett.config.useFS = false;       // файловый менеджер/OTA Settings не включаем в v1
  sett.config.updateTout = 2500;
  sett.onBuild(webui_build);
  sett.onUpdate(webui_update);
  sett.begin(cfg.WiFimode == 0);   // captive portal только в режиме точки доступа
}

void webui_tick() {
  sett.tick();
  webui_runDeferred();
}
