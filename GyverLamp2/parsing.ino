char buf[UDP_TX_PACKET_MAX_SIZE + 1];
void parsing() {
  if (Udp.parsePacket()) {
    int n = Udp.read(buf, UDP_TX_PACKET_MAX_SIZE);
    buf[n] = NULL;

    // ПРЕ-ПАРСИНГ (для данных АЦП)
    if (buf[0] != 'G' || buf[1] != 'L' || buf[2] != ',') return;  // защита от не наших данных
    if (buf[3] == '7') {   // АЦП GL,7,
      if (!cfg.role) {     // принимаем данные ацп если слейв
        int data[3];
        mString ints(buf + 5);
        ints.parseInts(data, 3);
        udpLength = data[0];
        udpScale = data[1];
        udpBright = data[2];
        effTmr.force();   // форсируем отрисовку эффекта
        gotADCtmr = millis();
      }
      return;   // выходим
    }

    if (millis() - udpTmr < 500) return;   // принимаем остальные посылки не чаще 2 раз в секунду
    udpTmr = millis();

    DEBUGLN(buf);   // пакет вида <GL>,<тип>,<дата1>,<дата2>...

    // ПАРСИНГ (настройки приходят только из web UI, по UDP - управление и синхронизация)
    byte data[MAX_PRESETS * PRES_SIZE + 10];
    memset(data, 0, MAX_PRESETS * PRES_SIZE + 10);
    int count = 0;
    char *str, *p = buf;

    while (count < (int)sizeof(data) && (str = strtok_r(p, ",", &p)) != NULL) {
      uint32_t thisInt = atoi(str);
      data[count++] = (byte)thisInt;  // парс байтов
    }

    // тип 0 - control, 4 - from master, 6 - time (конфигурация теперь только в web UI)
    switch (data[1]) {
      case 0: DEBUGLN("Control"); blinkTmr.restart();
        if (!cfg.state && data[2] != 1) return;   // если лампа выключена и это не команда на включение - не обрабатываем
        switch (data[2]) {
          case 0: controlHandler(0); break;               // выкл
          case 1: controlHandler(1); break;               // вкл
          case 2: cfg.minLight = phot.getRaw(); break;    // мин яркость
          case 3: cfg.maxLight = phot.getRaw(); break;    // макс яркость
          case 4: changePreset(-1); break;                // пред пресет
          case 5: changePreset(1); break;                 // след пресет
          case 6: setPreset(data[3] - 1); break;          // конкретный пресет data[3]
          case 11: EE_updCfgRst(); break;                 // рестарт
          case 12: if (gotNTP) {                          // OTA обновление, если есть интернет
              cfg.update = 1;
              EE_updCfg();
              FastLED.clear();
              FastLED.show();
              char OTA[60];
              mString ota(OTA);
              ota.clear();
              ota += OTAhost;
              ota += OTAfile[data[3]];
              DEBUG("Update to ");
              DEBUGLN(OTA);
              delay(100);
              WiFiClient client;
              ESPhttpUpdate.update(client, OTA);
            } break;
          case 13:                                        // выключить через
            if (data[3] == 0) turnoffTmr.stop();
            else {
              DEBUGLN("Fade");
              fadeDown((uint32_t)data[3] * 60000ul);
            }
            break;
        }
        if (data[2] < 7) setTime(data[3], data[4], data[5], data[6]);
        EE_updCfg();
        break;

      case 4: DEBUGLN("From master");
        if (cfg.role == GL_SLAVE) {
          switch (data[2]) {
            case 0: fade(data[3]); break;         // вкл выкл
            case 1: setPreset(data[3]); break;    // пресет
            case 2: cfg.bright = data[3]; break;  // яркость
          }
          EE_updateCfg();
        }
        break;

      case 6: DEBUGLN("Time from AP");
        if (cfg.WiFimode && !gotNTP) {   // время для local устройств в сети AP лампы (не получили время из интернета)
          now.day = data[2];
          now.hour = data[3];
          now.min = data[4];
          now.sec = 0;
          now.setMs(0);
          DEBUGLN("Got time from master");
        }
        break;
    }
    FastLED.clear();    // на всякий случай
  }
}

void sendToSlaves(byte data1, byte data2) {
  if (cfg.role == GL_MASTER) {
    char reply[15];
    mString packet(reply);
    packet.clear();
    packet = packet + "GL,4," + data1 + ',' + data2;

    DEBUG("Sending to Slaves: ");
    DEBUGLN(reply);

    FOR_i(0, 4) {
      sendUDP(reply);
      delay(8);
    }
  }
}
