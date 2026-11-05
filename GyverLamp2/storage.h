#pragma once
#include <Arduino.h>

// ===================== Настройки: Saver + LittleFS =====================
// Блоки настроек лежат в файлах LittleFS, автосохранение через 30 с после изменения
void storage_begin();              // старт LittleFS, чтение настроек и миграция из EEPROM
void storage_tick();               // тикер автосохранения (вызывать в loop)
void storage_saveCfg();            // записать cfg
void storage_saveDawn();           // записать dawn
void storage_savePal();            // записать pal
void storage_savePreset();         // записать preset[]
void storage_saveAll(bool force);   // записать все блоки (force - даже без изменений)
void storage_resetAll();           // сбросить все блоки к заводским значениям
bool storage_ready();              // true, если файловая система доступна
