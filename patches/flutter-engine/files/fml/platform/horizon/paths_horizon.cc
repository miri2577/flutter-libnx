// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Pfadauskuenfte fuer Horizon.
//
// Die uebrigen Funktionen aus fml/paths.h (AbsolutePath, GetDirectoryName,
// FromURI, GetCurrentDirectory) kommen unveraendert aus paths_posix.cc; sie
// arbeiten rein auf Zeichenketten beziehungsweise auf getcwd, das libnx
// anbietet. Nur diese beiden brauchen eine eigene Fassung.

#include "flutter/fml/paths.h"

namespace fml {
namespace paths {

std::pair<bool, std::string> GetExecutablePath() {
  // Horizon fuehrt kein /proc, und libnx bietet keinen Weg, den Pfad der
  // laufenden NRO zu erfragen. Was hbmenu beim Start uebergibt, landet in
  // argv und damit ausserhalb der Reichweite von fml. Ein geratener Pfad
  // waere schlechter als eine klare Absage.
  return {false, ""};
}

fml::UniqueFD GetCachesDirectory() {
  // Bewusst leer, wie unter Linux und QNX auch.
  //
  // Einziger Aufrufer im Softwarepfad ist der PersistentCache, der damit
  // vorkompilierte Shader ablegt. Wir rendern ohne GPU; der Cache haette
  // nichts zu speichern und wuerde nur Dateien auf der SD-Karte anlegen.
  // Sollte spaeter ein GPU-Backend dazukommen, ist hier ein Verzeichnis
  // unterhalb von sdmc:/switch/ der naheliegende Ort.
  return {};
}

}  // namespace paths
}  // namespace fml
