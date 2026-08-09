// Copyright 2026 The flutter-libnx Authors.
//
// CpuInfo für Horizon OS.
//
// Es gibt kein /proc/cpuinfo und keine vergleichbare Abfrage. Die Felder
// bleiben deshalb leer, und `HasField` meldet das ehrlich – worauf
// `CpuInfo::GetCpuModel()` von sich aus auf "Unknown" zurückfällt (siehe
// vm/cpuinfo.h). Die Angaben dienen ausschließlich der Anzeige, etwa im
// Observatory-Protokoll; die VM trifft keine Entscheidungen darauf.
//
// Die Prozessoreigenschaften der Switch sind ohnehin fest bekannt: vier
// Cortex-A57-Kerne, ARMv8-A mit crc und crypto. Sollte die Anzeige je zählen,
// wäre das hier mit Konstanten zu füllen statt mit einer Abfrage.

#include "platform/globals.h"

#if defined(DART_HOST_OS_HORIZON)

#include "platform/assert.h"
#include "platform/utils.h"
#include "vm/cpuinfo.h"

namespace dart {

const char* CpuInfo::fields_[kCpuInfoMax] = {};

void CpuInfo::Init() {
  // Nicht-leere Namen, damit FieldName() gültige Zeichenketten liefert.
  fields_[kCpuInfoProcessor] = "Processor";
  fields_[kCpuInfoModel] = "model name";
  fields_[kCpuInfoHardware] = "Hardware";
  fields_[kCpuInfoFeatures] = "Features";
  fields_[kCpuInfoArchitecture] = "CPU architecture";
}

void CpuInfo::Cleanup() {}

bool CpuInfo::HasField(const char* field) {
  // Kein Feld ist abfragbar. GetCpuModel() wertet das aus und liefert
  // daraufhin "Unknown".
  return false;
}

const char* CpuInfo::ExtractField(CpuInfoIndices idx) {
  ASSERT((idx >= 0) && (idx < kCpuInfoMax));
  // Der Aufrufer gibt das Ergebnis frei, also eine eigene Kopie liefern.
  return Utils::StrDup("");
}

}  // namespace dart

#endif  // defined(DART_HOST_OS_HORIZON)
