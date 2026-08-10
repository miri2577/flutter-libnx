// Copyright 2026 The flutter-libnx Authors.
//
// Socket-Systemheader für Horizon OS.
//
// Entspricht socket_base_linux.h ohne <sys/un.h>: libnx definiert zwar die
// Konstante AF_UNIX, liefert aber keinen Header und damit keinen Typ
// sockaddr_un.
//
// Darts RawAddr führt sockaddr_un als Mitglied seiner Union (socket_base.h),
// der Typ muss also existieren, damit der Code übersetzt. Die Definition unten
// ist die übliche und ausschließlich dafür da.
//
// Unix-Domain-Sockets funktionieren damit nicht – der Versuch scheitert zur
// Laufzeit im Betriebssystem. Das ist die ehrliche Antwort: Ein Dateisystempfad
// als Socket-Adresse hat auf Horizon keine Entsprechung.

#ifndef RUNTIME_BIN_SOCKET_BASE_HORIZON_H_
#define RUNTIME_BIN_SOCKET_BASE_HORIZON_H_

#if !defined(RUNTIME_BIN_SOCKET_BASE_H_)
#error Do not include socket_base_horizon.h directly. Use socket_base.h.
#endif

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>

// Übliche Ausprägung; sun_path ist auf den Plattformen, die den Typ kennen,
// 104 oder 108 Byte groß. Der genaue Wert ist hier ohne Belang, weil die
// Struktur nie an das Betriebssystem gereicht wird.
struct sockaddr_un {
  sa_family_t sun_family;
  char sun_path[104];
};

#endif  // RUNTIME_BIN_SOCKET_BASE_HORIZON_H_
