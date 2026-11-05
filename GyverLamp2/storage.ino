#include <LittleFS.h>
#include <SaverFile.h>
#include <EEPROM.h>

// блоки настроек: версия 'A', автосохранение через 30 с, атомарная запись
SaverFile cfgStore(LittleFS, "/cfg.bin", cfg, 'A', 30, FileStore::Atomic);
SaverFile dawnStore(LittleFS, "/dawn.bin", dawn, 'A', 30, FileStore::Atomic);
SaverFile palStore(LittleFS, "/pal.bin", pal, 'A', 30, FileStore::Atomic);
SaverFile presetStore(LittleFS, "/presets.bin", preset, 'A', 30, FileStore::Atomic);

bool storageReady = false;

void storage_begin() {
  if (!LittleFS.begin()) {           // первый запуск: пробуем разметить файловую систему
    DEBUGLN("LittleFS format");
    LittleFS.format();
    if (LittleFS.begin()) storageReady = true;
    else DEBUGLN("LittleFS error");
  } else {
    storageReady = true;
  }

  bool legacy = false;
  if (storageReady) {
    // одноразовая миграция из старого raw-EEPROM (магия в байте 0):
    // старые значения подставляем в RAM до begin(), файл создаётся только если его ещё нет
    EEPROM.begin(1000);
    delay(100);
    legacy = (EEPROM.read(0) == EE_KEY);
    bool ok = true;

    if (!LittleFS.exists("/cfg.bin") && legacy) EEPROM.get(1, cfg);
    ok = (cfgStore.begin(false) != Saver::Error) && ok;

    if (!LittleFS.exists("/dawn.bin") && legacy) EEPROM.get(1 + sizeof(cfg), dawn);
    ok = (dawnStore.begin(false) != Saver::Error) && ok;

    if (!LittleFS.exists("/pal.bin") && legacy) EEPROM.get(1 + sizeof(cfg) + sizeof(dawn), pal);
    ok = (palStore.begin(false) != Saver::Error) && ok;

    if (!LittleFS.exists("/presets.bin") && legacy) EEPROM.get(1 + sizeof(cfg) + sizeof(dawn) + sizeof(pal), preset);
    ok = (presetStore.begin(false) != Saver::Error) && ok;

    if (legacy && ok) {                // маркер: миграция завершена, EEPROM больше не нужен
      EEPROM.write(0, 0);
      EEPROM.commit();
      DEBUGLN("EEPROM migrated");
    }
    EEPROM.end();
  }

  // запускаем всё как раньше
  cfgValidate();   // проверка конфига на допустимые значения
  if (cfg.deviceType == GL_TYPE_STRIP) {
    if (cfg.length > MAX_LEDS) cfg.length = MAX_LEDS;
    cfg.width = 1;
  }
  if (cfg.length * cfg.width > MAX_LEDS) cfg.width = MAX_LEDS / cfg.length;
  FastLED.setMaxPowerInVoltsAndMilliamps(STRIP_VOLT, cfg.maxCur * 100);
  updPal();
  DEBUG("Storage ready, legacy: ");
  DEBUGLN(legacy);
}

void storage_tick() {
  if (!storageReady) return;
  if (cfgStore.tick() == Saver::Error) DEBUGLN("cfg write error");
  if (dawnStore.tick() == Saver::Error) DEBUGLN("dawn write error");
  if (palStore.tick() == Saver::Error) DEBUGLN("pal write error");
  if (presetStore.tick() == Saver::Error) DEBUGLN("preset write error");
}

void storage_saveCfg() {
  if (storageReady) cfgStore.write();
}
void storage_saveDawn() {
  if (storageReady) dawnStore.write();
}
void storage_savePal() {
  if (storageReady) palStore.write();
}
void storage_savePreset() {
  if (storageReady) presetStore.write();
}

void storage_saveAll(bool force) {
  if (!storageReady) return;
  cfgStore.write(force);
  dawnStore.write(force);
  palStore.write(force);
  presetStore.write(force);
}

void storage_resetAll() {
  if (!storageReady) return;
  cfgStore.reset();
  dawnStore.reset();
  palStore.reset();
  presetStore.reset();
}

bool storage_ready() {
  return storageReady;
}
