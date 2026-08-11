// Copyright 2026 The flutter-libnx Authors.
//
// Eventhandler-Interna für Horizon OS.
//
// Die Linux-Fassung stützt sich auf epoll und timerfd; Horizon hat beides
// nicht. Diese Fassung benutzt stattdessen `poll` und leitet das Zeitlimit aus
// der ohnehin vorhandenen TimeoutQueue ab.
//
// Zwei Unterschiede fallen dadurch weg bzw. an:
//
//   * Es gibt keine dauerhaft registrierte Instanz. Die Deskriptorliste wird
//     vor jedem `poll` aus den Interessensmasken neu aufgebaut. Bei der
//     Größenordnung, in der dart:io Deskriptoren führt, ist das unkritisch,
//     und es macht `UpdateEpollInstance` zu einer leeren Operation.
//   * `poll` kennt keine kantengesteuerte Meldung (EPOLLET). Das ist hier
//     unschädlich: Nach einer Benachrichtigung nimmt DescriptorInfo den Port
//     aus der Maske, bis Tokens zurückkommen – der Deskriptor fällt damit von
//     selbst aus der nächsten Liste heraus.
//
// Der Weckmechanismus hat zwei Fassungen, und welche greift, entscheidet die
// Hardware:
//
//   * Bevorzugt ein über 127.0.0.1 selbst geknüpftes TCP-Paar. Es leistet
//     dasselbe wie die Pipe der Linux-Fassung – ein Deskriptor, auf den `poll`
//     wartet und den ein anderer Thread beschreibt.
//   * Andernfalls gar kein Weckdeskriptor: Die Nachrichten laufen dann über ein
//     mutex-geschütztes Fach, und `poll` bekommt eine Obergrenze fürs
//     Zeitlimit, damit sie zeitnah gesehen werden.
//
// `socketpair` ist ausdrücklich kein Weg. libnx definiert es zwar, aber nur
// pro forma: `socket.c:812` setzt `ENOSYS` und gibt -1 zurück, mit dem
// Kommentar „Unimplementable". Dass ein Symbol in `libnx.a` steht, sagt hier
// nichts über seine Brauchbarkeit.

#ifndef RUNTIME_BIN_EVENTHANDLER_HORIZON_H_
#define RUNTIME_BIN_EVENTHANDLER_HORIZON_H_

#if !defined(RUNTIME_BIN_EVENTHANDLER_H_)
#error Do not include eventhandler_horizon.h directly; use eventhandler.h instead.
#endif

#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <vector>

#include "bin/lockers.h"
#include "platform/hashmap.h"

namespace dart {
namespace bin {

class DescriptorInfo : public DescriptorInfoBase {
 public:
  explicit DescriptorInfo(intptr_t fd) : DescriptorInfoBase(fd) {}

  virtual ~DescriptorInfo() {}

  intptr_t GetPollEvents();

  virtual void Close() {
    close(fd_);
    fd_ = -1;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(DescriptorInfo);
};

class DescriptorInfoSingle : public DescriptorInfoSingleMixin<DescriptorInfo> {
 public:
  explicit DescriptorInfoSingle(intptr_t fd) : DescriptorInfoSingleMixin(fd) {}
  virtual ~DescriptorInfoSingle() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(DescriptorInfoSingle);
};

class DescriptorInfoMultiple
    : public DescriptorInfoMultipleMixin<DescriptorInfo> {
 public:
  explicit DescriptorInfoMultiple(intptr_t fd)
      : DescriptorInfoMultipleMixin(fd) {}
  virtual ~DescriptorInfoMultiple() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(DescriptorInfoMultiple);
};

class EventHandlerImplementation {
 public:
  EventHandlerImplementation();
  ~EventHandlerImplementation();

  // Name aus der Linux-Fassung übernommen, damit sich beide Seiten leicht
  // vergleichen lassen. Hier eine leere Operation: Die Deskriptorliste
  // entsteht vor jedem poll neu aus den Masken.
  void UpdateEpollInstance(intptr_t old_mask, DescriptorInfo* di);

  DescriptorInfo* GetDescriptorInfo(intptr_t fd, bool is_listening);
  void SendData(intptr_t id, Dart_Port dart_port, int64_t data);
  void Start(EventHandler* handler);
  void Shutdown();

 private:
  static void Poll(uword args);
  void HandleInterruptFd();
  void HandleInterruptQueue();
  void ProcessInterruptMessage(const InterruptMessage& msg);
  void WakeupHandler(intptr_t id, Dart_Port dart_port, int64_t data);
  intptr_t GetPollEvents(intptr_t events, DescriptorInfo* di);
  static void* GetHashmapKeyFromFd(intptr_t fd);
  static uint32_t GetHashmapHashFromFd(intptr_t fd);

  // Knüpft ein verbundenes TCP-Paar über 127.0.0.1. Gelingt das nicht, bleibt
  // es beim Fach unten.
  static bool CreateLoopbackPair(int fds[2]);

  bool has_interrupt_fds() const { return interrupt_fds_[0] >= 0; }

  SimpleHashMap socket_map_;
  TimeoutQueue timeout_queue_;
  bool shutdown_;
  int interrupt_fds_[2];

  // Nur benutzt, wenn kein Weckdeskriptor zustande kam.
  Mutex queue_mutex_;
  std::vector<InterruptMessage> pending_messages_;

  DISALLOW_COPY_AND_ASSIGN(EventHandlerImplementation);
};

}  // namespace bin
}  // namespace dart

#endif  // RUNTIME_BIN_EVENTHANDLER_HORIZON_H_
