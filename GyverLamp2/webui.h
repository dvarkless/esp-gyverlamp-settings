#pragma once
#include <Arduino.h>
#include <ESP8266WiFi.h>

// Settings 1.4.3: встроенный ESP8266WebServer, без GyverDB и Table (экономия RAM)
#define SETT_NO_DB
#define SETT_NO_TABLE
#include <SettingsESP.h>

// Прототипы функций из других .ino (вызываются из этого заголовка; сами
// определения появляются позже в объединённом скетче, прототипы не мешают)
void controlHandler(bool state);
void changePreset(int dir);
void sendToSlaves(byte data1, byte data2);
void restartUDP();
void fadeDown(uint32_t time);
void cfgValidate();
void switchToPhot();
void switchToMic();
void disableADC();

// ===================== Веб-интерфейс настроек =====================
// Типы Settings и билдеры живут только в этом заголовке: авто-прототипы
// Arduino для второго .ino ломаются на типах sets::.
SettingsESP sett("GyverLamp2");

// ID скрытых виджетов Confirm (уникальны, не пересекаются с авто-ID UINT32_MAX-...)
static const size_t WEB_ID_CONFIRM_FACTORY = 0x7E57C0DEUL;
static const size_t WEB_ID_CONFIRM_WIFI = 0x7E57C0DFUL;

// -------------------- отложенные действия --------------------
// Никогда не перезагружаем и не пишем флеш внутри builder-колбэка:
// действие ставится в очередь и выполняется из loop() после sett.tick()
enum WebAction : uint8_t {
  WEB_ACT_NONE = 0,
  WEB_ACT_SAVE,
  WEB_ACT_REBOOT,
  WEB_ACT_FACTORY_RESET,
  WEB_ACT_SLEEP,
  WEB_ACT_SLEEP_CANCEL,
  WEB_ACT_PHOTO_MIN,
  WEB_ACT_PHOTO_MAX,
  WEB_ACT_WIFI_RESTART,
};
static WebAction webAction = WEB_ACT_NONE;
static uint32_t webActionMs = 0;
static bool webFactoryAsk = false;   // показать Confirm сброса
static bool webSavedNotice = false;  // показать уведомление «Сохранено»

// -------------------- подтверждения WiFi --------------------
enum WebConfirm : uint8_t {
  WEB_CONFIRM_NONE = 0,
  WEB_CONFIRM_WIFI_MODE,
  WEB_CONFIRM_WIFI_CREDS,
};
static WebConfirm webConfirmKind = WEB_CONFIRM_NONE;
static bool webConfirmSent = false;      // запрос уже отправлен в браузер
static uint8_t webWiFiPendingMode = 0;   // выбранный, но не применённый режим

// буферы Wi-Fi: правки идут сюда, в cfg попадают только после подтверждения
static char webSsid[32];
static char webPass[32];
static uint8_t webSleepMin = 30;         // таймер сна, минут

// часовые пояса: индекс 0..26 соответствует cfg.GMT 1..27 (cfg.GMT = offset + 13)
static const char WEB_GMT_OPTIONS[] = "-12;-11;-10;-9;-8;-7;-6;-5;-4;-3;-2;-1;0;+1;+2;+3;+4;+5;+6;+7;+8;+9;+10;+11;+12;+13;+14";

static void webui_copyStr(char* dst, const char* src, size_t size) {
  if (!size) return;
  size_t i = 0;
  for (; i + 1 < size && src[i]; i++) dst[i] = src[i];
  dst[i] = 0;
}

static void webui_startAction(WebAction action) {
  webAction = action;
  webActionMs = millis();
}

// -------------------- применение конфига --------------------
// Аналог обработчика config-пакета (parsing.ino type-1): проверки, ограничения,
// мощность ленты, часовой пояс и переключение АЦП. Запись в файл не форсируем:
// Saver сам сохранит изменения после 30 с тишины.
static void webui_applyConfig() {
  cfgValidate();
  cfg.deviceType = constrain(cfg.deviceType, GL_TYPE_STRIP, GL_TYPE_PARAL);
  cfg.matrix = constrain(cfg.matrix, 1, 8);
  cfg.adcMode = constrain(cfg.adcMode, GL_ADC_NONE, GL_ADC_BOTH);
  cfg.group = constrain(cfg.group, 1, 10);
  cfg.role = constrain(cfg.role, 0, 1);
  cfg.WiFimode = constrain(cfg.WiFimode, 0, 1);
  cfg.rotPeriod = constrain(cfg.rotPeriod, 1, 60);
  cfg.maxCur = constrain(cfg.maxCur, 1, 100);
  cfg.workFrom = constrain(cfg.workFrom, 0, 23);
  cfg.workTo = constrain(cfg.workTo, 0, 23);
  cfg.GMT = constrain(cfg.GMT, 1, 27);
  if (cfg.state > 1) cfg.state = 0;

  if (cfg.deviceType == GL_TYPE_STRIP) {
    cfg.length = constrain(cfg.length, 1, MAX_LEDS);
    cfg.width = 1;
  } else {
    cfg.length = constrain(cfg.length, 1, MAX_LEDS);
    cfg.width = constrain(cfg.width, 1, MAX_LEDS);
    if ((int32_t)cfg.length * cfg.width > MAX_LEDS) cfg.width = MAX_LEDS / cfg.length;
    if (cfg.width < 1) cfg.width = 1;
  }

  ntp.setTimeOffset((cfg.GMT - 13) * 3600);
  FastLED.setMaxPowerInVoltsAndMilliamps(STRIP_VOLT, cfg.maxCur * 100);

  if (cfg.adcMode == GL_ADC_BRI) switchToPhot();
  else if (cfg.adcMode == GL_ADC_MIC) switchToMic();
  else disableADC();
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

// -------------------- страница «Свет» --------------------
static void webui_buildLight(sets::Builder& b) {
  {
    sets::Menu m(b, "Свет");
    {
      sets::Group g(b, "Питание и яркость");
      bool power = cfg.state;
      if (b.Switch("Питание", &power)) controlHandler(power);
      b.Slider("Яркость", 0, 255, 1, "", &cfg.bright);
      b.Slider("Мин. яркость", 0, 255, 1, "", &cfg.minBright);
      b.Slider("Макс. яркость", 0, 255, 1, "", &cfg.maxBright);
    }
    {
      sets::Group g(b, "Пресет");
      b.LabelNum("Текущий пресет", (uint32_t)(cfg.curPreset + 1));
      {
        sets::Buttons btns(b);
        if (b.Button("Пред.")) {
          changePreset(-1);
          if (cfg.role == GL_MASTER) sendToSlaves(1, cfg.curPreset);
        }
        if (b.Button("След.")) {
          changePreset(1);
          if (cfg.role == GL_MASTER) sendToSlaves(1, cfg.curPreset);
        }
      }
    }
    {
      sets::Group g(b, "Автосмена");
      bool rot = cfg.rotation;
      if (b.Switch("Автосмена", &rot)) cfg.rotation = rot;
      b.Select("Тип автосмены", "По порядку;Случайно", &cfg.rotRnd);
      if (b.Spinner("Период, мин", 1, 60, 1, &cfg.rotPeriod)) webui_applyConfig();
    }
    {
      sets::Group g(b, "Расписание");
      b.Spinner("Включение, ч", 0, 23, 1, &cfg.workFrom);
      b.Spinner("Выключение, ч", 0, 23, 1, &cfg.workTo);
      b.Paragraph("", "Если часы совпадают — лампа работает всегда.");
    }
    {
      sets::Group g(b, "Таймер сна");
      b.Slider("Выключить через", 0, 60, 5, " мин", &webSleepMin);
      {
        sets::Buttons btns(b);
        if (b.Button("Запустить")) webui_startAction(WEB_ACT_SLEEP);
        if (b.Button("Отмена")) webui_startAction(WEB_ACT_SLEEP_CANCEL);
      }
      b.Paragraph("", "0 минут — отмена таймера.");
    }
  }
}

// -------------------- страница «Сеть» --------------------
static void webui_buildNetwork(sets::Builder& b) {
  {
    sets::Menu m(b, "Сеть");
    {
      sets::Group g(b, "WiFi");
      uint8_t mode = (cfg.WiFimode <= 1) ? cfg.WiFimode : 0;
      if (b.Select("Режим WiFi", "Точка доступа;Локальная сеть", &mode)) {
        if (mode != cfg.WiFimode) {
          webWiFiPendingMode = mode;
          webConfirmKind = WEB_CONFIRM_WIFI_MODE;
          webConfirmSent = false;
        }
      }
      b.Input("Имя сети (SSID)", webSsid);
      b.Pass("Пароль", webPass);
      if (b.Button("Сохранить WiFi и перезагрузить")) {
        webConfirmKind = WEB_CONFIRM_WIFI_CREDS;
        webConfirmSent = false;
      }
      bool res = false;
      if (b.Confirm(WEB_ID_CONFIRM_WIFI, "Подтвердить изменение WiFi?", &res)) {
        WebConfirm kind = webConfirmKind;
        webConfirmKind = WEB_CONFIRM_NONE;
        webConfirmSent = false;
        if (res && kind == WEB_CONFIRM_WIFI_MODE) {
          cfg.WiFimode = webWiFiPendingMode;
          webui_startAction(WEB_ACT_WIFI_RESTART);
        } else if (res && kind == WEB_CONFIRM_WIFI_CREDS) {
          webui_copyStr(cfg.ssid, webSsid, sizeof(cfg.ssid));
          webui_copyStr(cfg.pass, webPass, sizeof(cfg.pass));
          webui_startAction(WEB_ACT_WIFI_RESTART);
        }
      }
      b.Paragraph("", "Смена режима или настроек WiFi перезагружает лампу.");
    }
    {
      sets::Group g(b, "Сеть ламп");
      if (b.Spinner("Группа", 1, 10, 1, &cfg.group)) {
        cfg.group = constrain(cfg.group, 1, 10);
        restartUDP();
      }
      if (b.Select("Роль", "Ведомая;Ведущая", &cfg.role)) {
        cfg.role = constrain(cfg.role, 0, 1);
        restartUDP();
      }
    }
    {
      sets::Group g(b, "Время");
      uint8_t gmt = (cfg.GMT >= 1 && cfg.GMT <= 27) ? (cfg.GMT - 1) : 15;   // 15 -> +3
      if (b.Select("Часовой пояс", WEB_GMT_OPTIONS, &gmt)) {
        cfg.GMT = gmt + 1;
        webui_applyConfig();
      }
      b.Paragraph("", "Москва: +3. Пояс применяется сразу.");
    }
  }
}

// -------------------- страница «Устройство» --------------------
static void webui_buildHardware(sets::Builder& b) {
  {
    sets::Menu m(b, "Устройство");
    {
      sets::Group g(b, "Геометрия");
      uint8_t dev = (cfg.deviceType >= GL_TYPE_STRIP && cfg.deviceType <= GL_TYPE_PARAL) ? (cfg.deviceType - 1) : 1;
      if (b.Select("Тип устройства", "Лента;Зигзаг;Параллельно", &dev)) {
        cfg.deviceType = dev + 1;
        webui_applyConfig();
        b.reload();
      }
      if (b.Spinner("Длина", 1, MAX_LEDS, 1, &cfg.length)) webui_applyConfig();
      if (b.Spinner("Ширина", 1, MAX_LEDS, 1, &cfg.width)) webui_applyConfig();
      uint8_t mtx = (cfg.matrix >= 1 && cfg.matrix <= 8) ? (cfg.matrix - 1) : 0;
      if (b.Select("Тип матрицы", "1;2;3;4;5;6;7;8", &mtx)) {
        cfg.matrix = mtx + 1;
        webui_applyConfig();
      }
      b.Paragraph("", "Для ленты ширина принудительно 1, длина не больше MAX_LEDS.");
    }
    {
      sets::Group g(b, "Питание");
      uint16_t ma = (uint16_t)cfg.maxCur * 100;
      if (b.Slider("Макс. ток", 100, 10000, 100, " мА", &ma)) {
        cfg.maxCur = constrain(ma / 100, 1, 100);
        webui_applyConfig();
      }
      b.Paragraph("", "Ограничение тока блока питания для ленты.");
    }
    {
      sets::Group g(b, "Датчик (АЦП)");
      uint8_t adc = (cfg.adcMode >= GL_ADC_NONE && cfg.adcMode <= GL_ADC_BOTH) ? (cfg.adcMode - 1) : 0;
      if (b.Select("Режим АЦП", "Выкл;Датчик света;Микрофон;Оба", &adc)) {
        cfg.adcMode = adc + 1;
        webui_applyConfig();
      }
      {
        sets::Buttons btns(b);
        if (b.Button("Запомнить мин. свет")) webui_startAction(WEB_ACT_PHOTO_MIN);
        if (b.Button("Запомнить макс. свет")) webui_startAction(WEB_ACT_PHOTO_MAX);
      }
      b.Paragraph("", "В этой сборке АЦП отключён (USE_ADC 0): режим и калибровка не действуют.");
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
  webui_buildLight(b);
  webui_buildNetwork(b);
  webui_buildHardware(b);
  webui_buildSystem(b);
}

// -------------------- обновления (запросы из вебморды) --------------------
static void webui_update(sets::Updater& upd) {
  if (webFactoryAsk) {
    webFactoryAsk = false;
    upd.confirm(WEB_ID_CONFIRM_FACTORY);
  }
  if (webConfirmKind != WEB_CONFIRM_NONE && !webConfirmSent) {
    webConfirmSent = true;
    if (webConfirmKind == WEB_CONFIRM_WIFI_MODE)
      upd.update(WEB_ID_CONFIRM_WIFI, "Сменить режим WiFi? Лампа перезагрузится.");
    else
      upd.update(WEB_ID_CONFIRM_WIFI, "Сохранить настройки WiFi? Лампа перезагрузится.");
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
    case WEB_ACT_SLEEP:
      if (webSleepMin) fadeDown((uint32_t)webSleepMin * 60000ul);
      else turnoffTmr.stop();
      break;
    case WEB_ACT_SLEEP_CANCEL:
      turnoffTmr.stop();
      break;
    case WEB_ACT_PHOTO_MIN:
      cfg.minLight = (int16_t)phot.getRaw();
      if (cfg.minLight < 0) cfg.minLight = 0;
      if (cfg.minLight > 1023) cfg.minLight = 1023;
      break;
    case WEB_ACT_PHOTO_MAX:
      cfg.maxLight = (int16_t)phot.getRaw();
      if (cfg.maxLight < 0) cfg.maxLight = 0;
      if (cfg.maxLight > 1023) cfg.maxLight = 1023;
      break;
    case WEB_ACT_WIFI_RESTART:
      storage_saveCfg();
      ESP.restart();
      break;
    default:
      break;
  }
}

// -------------------- запуск / тикер --------------------
void webui_begin() {
  webui_copyStr(webSsid, cfg.ssid, sizeof(webSsid));
  webui_copyStr(webPass, cfg.pass, sizeof(webPass));
  webSleepMin = 30;

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
