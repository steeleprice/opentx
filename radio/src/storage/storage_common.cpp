/*
 * Copyright (C) OpenTX
 *
 * Based on code named
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "opentx.h"
#include "pulses/multi.h"

uint8_t   storageDirtyMsk;
tmr10ms_t storageDirtyTime10ms;

#if defined(RTC_BACKUP_RAM)
uint8_t   rambackupDirtyMsk = EE_GENERAL | EE_MODEL;
tmr10ms_t rambackupDirtyTime10ms;
#endif

void storageDirty(uint8_t msk)
{
  storageDirtyMsk |= msk;
  storageDirtyTime10ms = get_tmr10ms();

#if defined(RTC_BACKUP_RAM)
  rambackupDirtyMsk = storageDirtyMsk;
  rambackupDirtyTime10ms = storageDirtyTime10ms;
#endif
}

void preModelLoad()
{
  watchdogSuspend(500/*5s*/);

#if defined(SDCARD)
  logsClose();
#endif

  if (pulsesStarted()) {
    pausePulses();
  }
  pauseMixerCalculations();

#if defined(INTMODULE_PWR_GPIO)
  INTERNAL_MODULE_OFF();
#endif
#if defined(EXTMODULE_PWR_GPIO)
  EXTERNAL_MODULE_OFF();
#endif

  stopTrainer();
}

void postRadioSettingsLoad()
{
#if defined(PXX2)
  if (is_memclear(g_eeGeneral.ownerRegistrationID, PXX2_LEN_REGISTRATION_ID)) {
    setDefaultOwnerId();
  }
#endif
}

#if defined(EXTERNAL_ANTENNA) && defined(INTERNAL_MODULE_PXX1)
void onAntennaSelection(const char * result)
{
  if (result == STR_USE_INTERNAL_ANTENNA) {
    globalData.externalAntennaEnabled = false;
  }
  else if (result == STR_USE_EXTERNAL_ANTENNA) {
    globalData.externalAntennaEnabled = true;
  }
  else {
    checkExternalAntenna();
  }
}

void onAntennaSwitchConfirm(const char * result)
{
  if (result == STR_OK) {
    // Switch to external antenna confirmation
    globalData.externalAntennaEnabled = true;
  }
}

void checkExternalAntenna()
{
  if (isModuleXJT(INTERNAL_MODULE)) {
    if (g_eeGeneral.antennaMode == ANTENNA_MODE_EXTERNAL) {
      globalData.externalAntennaEnabled = true;
    }
    else if (g_eeGeneral.antennaMode == ANTENNA_MODE_PER_MODEL && g_model.moduleData[INTERNAL_MODULE].pxx.antennaMode == ANTENNA_MODE_EXTERNAL) {
      if (!globalData.externalAntennaEnabled) {
        POPUP_CONFIRMATION(STR_ANTENNACONFIRM1, onAntennaSwitchConfirm);
        SET_WARNING_INFO(STR_ANTENNACONFIRM2, sizeof(TR_ANTENNACONFIRM2), 0);
      }
    }
    else if (g_eeGeneral.antennaMode == ANTENNA_MODE_ASK || (g_eeGeneral.antennaMode == ANTENNA_MODE_PER_MODEL && g_model.moduleData[INTERNAL_MODULE].pxx.antennaMode == ANTENNA_MODE_ASK)) {
      globalData.externalAntennaEnabled = false;
      POPUP_MENU_ADD_ITEM(STR_USE_INTERNAL_ANTENNA);
      POPUP_MENU_ADD_ITEM(STR_USE_EXTERNAL_ANTENNA);
      POPUP_MENU_START(onAntennaSelection);
    }
    else {
      globalData.externalAntennaEnabled = false;
    }
  }
  else {
    globalData.externalAntennaEnabled = false;
  }
}
#endif

void postModelLoad(bool alarms)
{
#if defined(PXX2)
  if (is_memclear(g_model.modelRegistrationID, PXX2_LEN_REGISTRATION_ID)) {
    memcpy(g_model.modelRegistrationID, g_eeGeneral.ownerRegistrationID, PXX2_LEN_REGISTRATION_ID);
  }
#endif

#if defined(HARDWARE_INTERNAL_MODULE)
  if (!isInternalModuleAvailable(g_model.moduleData[INTERNAL_MODULE].type)) {
    memclear(&g_model.moduleData[INTERNAL_MODULE], sizeof(ModuleData));
  }
#if defined(MULTIMODULE)
  else if (isModuleMultimodule(INTERNAL_MODULE))
    multiPatchCustom(INTERNAL_MODULE);
#endif
#endif

  if (!isExternalModuleAvailable(g_model.moduleData[EXTERNAL_MODULE].type)) {
    memclear(&g_model.moduleData[EXTERNAL_MODULE], sizeof(ModuleData));
  }
#if defined(MULTIMODULE)
  else if (isModuleMultimodule(EXTERNAL_MODULE))
    multiPatchCustom(EXTERNAL_MODULE);
#endif

  AUDIO_FLUSH();
  flightReset(false);

  customFunctionsReset();

  restoreTimers();

  for (int i=0; i<MAX_TELEMETRY_SENSORS; i++) {
    TelemetrySensor & sensor = g_model.telemetrySensors[i];
    if (sensor.type == TELEM_TYPE_CALCULATED && sensor.persistent) {
      telemetryItems[i].value = sensor.persistentValue;
      telemetryItems[i].timeout = 0; // make value visible even before the first new value is received)
    }
    else {
      telemetryItems[i].timeout = TELEMETRY_SENSOR_TIMEOUT_UNAVAILABLE;
    }
  }

  LOAD_MODEL_CURVES();

  resumeMixerCalculations();
  if (pulsesStarted()) {
#if defined(GUI)
    if (alarms) {
      checkAll();
      PLAY_MODEL_NAME();
    }
#endif
    resumePulses();
  }

#if defined(SDCARD)
  referenceModelAudioFiles();
#endif

#if defined(PCBHORUS)
  loadCustomScreens();
#endif

  LOAD_MODEL_BITMAP();
  LUA_LOAD_MODEL_SCRIPTS();

  SEND_FAILSAFE_1S();
}

void storageFlushCurrentModel()
{
  saveTimers();

  for (int i=0; i<MAX_TELEMETRY_SENSORS; i++) {
    TelemetrySensor & sensor = g_model.telemetrySensors[i];
    if (sensor.type == TELEM_TYPE_CALCULATED && sensor.persistent && sensor.persistentValue != telemetryItems[i].value) {
      sensor.persistentValue = telemetryItems[i].value;
      storageDirty(EE_MODEL);
    }
  }

  if (g_model.potsWarnMode == POTS_WARN_AUTO) {
    for (int i=0; i<NUM_POTS+NUM_SLIDERS; i++) {
      if (!(g_model.potsWarnEnabled & (1 << i))) {
        SAVE_POT_POSITION(i);
      }
    }
    storageDirty(EE_MODEL);
  }
}
