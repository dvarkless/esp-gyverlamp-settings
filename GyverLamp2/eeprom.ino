// Совместимость: старые вызовы EE_* теперь работают через Saver/LittleFS (storage.ino)

// проверка конфига на допустимые значения (после eeprom и пакетов)
void cfgValidate() {
  if (cfg.rotPeriod == 0) cfg.rotPeriod = 1;                    // защита от деления на 0
  cfg.presetAmount = constrain(cfg.presetAmount, 1, MAX_PRESETS);
  cfg.curPreset = constrain(cfg.curPreset, 0, cfg.presetAmount - 1);
}

void EE_startup() {
  storage_begin();     // чтение настроек и миграция из EEPROM (storage.ino)
}
void EE_updateCfg() {}     // запись выполнит storage_tick() после изменения
void EE_updateDawn() {}
void EE_updatePreset() {}
void EE_updatePal() {}
void EE_updCfg() {
  storage_saveCfg();   // запись сейчас, если настройки изменились
}
void EE_updCfgRst() {
  storage_saveCfg();
  delay(100);
  ESP.restart();
}
