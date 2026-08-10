// Copyright 2026 The flutter-libnx Authors.
//
// Eventhandler für Horizon OS auf Basis von `poll`.
// Aufbau und Ablauf folgen eventhandler_linux.cc; die Unterschiede sind in
// eventhandler_horizon.h begründet.

#include "platform/globals.h"
#if defined(DART_HOST_OS_HORIZON)

#include "bin/eventhandler.h"
#include "bin/eventhandler_horizon.h"

#include <errno.h>       // NOLINT
#include <fcntl.h>       // NOLINT
#include <poll.h>        // NOLINT
#include <stdio.h>       // NOLINT
#include <stdlib.h>      // NOLINT
#include <string.h>      // NOLINT
#include <sys/socket.h>  // NOLINT
#include <unistd.h>      // NOLINT

#include "bin/dartutils.h"
#include "bin/fdutils.h"
#include "bin/lockers.h"
#include "bin/socket.h"
#include "bin/thread.h"
#include "bin/utils.h"
#include "platform/signal_blocker.h"  // TEMP_FAILURE_RETRY_NO_SIGNAL_BLOCKER
#include "platform/syslog.h"
#include "platform/utils.h"

namespace dart {
namespace bin {

intptr_t DescriptorInfo::GetPollEvents() {
  // POLLERR und POLLHUP werden von poll ohnehin gemeldet und muessen nicht
  // angefordert werden.
  intptr_t events = 0;
  if ((Mask() & (1 << kInEvent)) != 0) {
    events |= POLLIN;
  }
  if ((Mask() & (1 << kOutEvent)) != 0) {
    events |= POLLOUT;
  }
  return events;
}

EventHandlerImplementation::EventHandlerImplementation()
    : socket_map_(&SimpleHashMap::SamePointerValue, 16) {
  // libnx kennt kein pipe(), wohl aber socketpair(). Ein verbundenes Paar
  // erfuellt denselben Zweck: Ein Deskriptor, auf den poll wartet und den ein
  // anderer Thread beschreibt.
  int result = socketpair(AF_UNIX, SOCK_STREAM, 0, interrupt_fds_);
  if (result != 0) {
    FATAL("Failed creating socketpair for event handler: %d", errno);
  }
  if (!FDUtils::SetNonBlocking(interrupt_fds_[0])) {
    FATAL("Failed to set event handler socket non blocking");
  }
  shutdown_ = false;
}

static void DeleteDescriptorInfo(void* info) {
  DescriptorInfo* di = reinterpret_cast<DescriptorInfo*>(info);
  di->Close();
  delete di;
}

EventHandlerImplementation::~EventHandlerImplementation() {
  socket_map_.Clear(DeleteDescriptorInfo);
  close(interrupt_fds_[0]);
  close(interrupt_fds_[1]);
}

void EventHandlerImplementation::UpdateEpollInstance(intptr_t old_mask,
                                                     DescriptorInfo* di) {
  // Leer: Die Deskriptorliste wird vor jedem poll aus den Masken neu
  // aufgebaut, es gibt also keinen Zustand nachzuführen.
}

DescriptorInfo* EventHandlerImplementation::GetDescriptorInfo(
    intptr_t fd,
    bool is_listening) {
  ASSERT(fd >= 0);
  SimpleHashMap::Entry* entry = socket_map_.Lookup(
      GetHashmapKeyFromFd(fd), GetHashmapHashFromFd(fd), true);
  ASSERT(entry != nullptr);
  DescriptorInfo* di = reinterpret_cast<DescriptorInfo*>(entry->value);
  if (di == nullptr) {
    if (is_listening) {
      di = new DescriptorInfoMultiple(fd);
    } else {
      di = new DescriptorInfoSingle(fd);
    }
    entry->value = di;
  }
  ASSERT(fd == di->fd());
  return di;
}

void EventHandlerImplementation::WakeupHandler(intptr_t id,
                                               Dart_Port dart_port,
                                               int64_t data) {
  InterruptMessage msg;
  msg.id = id;
  msg.dart_port = dart_port;
  msg.data = data;
  intptr_t result =
      FDUtils::WriteToBlocking(interrupt_fds_[1], &msg, kInterruptMessageSize);
  if (result != kInterruptMessageSize) {
    if (result == -1) {
      FATAL("Interrupt message failure: %s", strerror(errno));
    } else {
      FATAL("Interrupt message failure: expected to write %" Pd
            " bytes, but wrote %" Pd ".",
            kInterruptMessageSize, result);
    }
  }
}

void EventHandlerImplementation::HandleInterruptFd() {
  const intptr_t kMaxMessages = kInterruptMessageSize;
  InterruptMessage msg[kMaxMessages];
  ssize_t bytes = TEMP_FAILURE_RETRY_NO_SIGNAL_BLOCKER(
      read(interrupt_fds_[0], msg, kMaxMessages * kInterruptMessageSize));
  for (ssize_t i = 0; i < bytes / kInterruptMessageSize; i++) {
    if (msg[i].id == kTimerId) {
      // Kein timerfd: Das Zeitlimit fliesst beim naechsten poll aus der
      // TimeoutQueue ein.
      timeout_queue_.UpdateTimeout(msg[i].dart_port, msg[i].data);
    } else if (msg[i].id == kShutdownId) {
      shutdown_ = true;
    } else {
      ASSERT((msg[i].data & COMMAND_MASK) != 0);
      Socket* socket = reinterpret_cast<Socket*>(msg[i].id);
      RefCntReleaseScope<Socket> rs(socket);
      if (socket->fd() == -1) {
        continue;
      }
      DescriptorInfo* di =
          GetDescriptorInfo(socket->fd(), IS_LISTENING_SOCKET(msg[i].data));
      if (IS_COMMAND(msg[i].data, kShutdownReadCommand)) {
        ASSERT(!di->IsListeningSocket());
        VOID_NO_RETRY_EXPECTED(shutdown(di->fd(), SHUT_RD));
      } else if (IS_COMMAND(msg[i].data, kShutdownWriteCommand)) {
        ASSERT(!di->IsListeningSocket());
        VOID_NO_RETRY_EXPECTED(shutdown(di->fd(), SHUT_WR));
      } else if (IS_COMMAND(msg[i].data, kCloseCommand)) {
        Dart_Port port = msg[i].dart_port;
        if (port != ILLEGAL_PORT) {
          di->RemovePort(port);
        }

        intptr_t fd = di->fd();
        ASSERT(fd == socket->fd());
        if (di->IsListeningSocket()) {
          ListeningSocketRegistry* registry =
              ListeningSocketRegistry::Instance();
          MutexLocker locker(registry->mutex());
          if (registry->CloseSafe(socket)) {
            socket_map_.Remove(GetHashmapKeyFromFd(fd),
                               GetHashmapHashFromFd(fd));
            di->Close();
            delete di;
          }
          socket->CloseFd();
        } else {
          socket_map_.Remove(GetHashmapKeyFromFd(fd), GetHashmapHashFromFd(fd));
          di->Close();
          delete di;
          socket->CloseFd();
        }
        DartUtils::PostInt32(port, 1 << kDestroyedEvent);
      } else if (IS_COMMAND(msg[i].data, kReturnTokenCommand)) {
        int count = TOKEN_COUNT(msg[i].data);
        di->ReturnTokens(msg[i].dart_port, count);
      } else if (IS_COMMAND(msg[i].data, kSetEventMaskCommand)) {
        intptr_t events = msg[i].data & EVENT_MASK;
        ASSERT(0 == (events & ~(1 << kInEvent | 1 << kOutEvent)));
        di->SetPortAndMask(msg[i].dart_port, msg[i].data & EVENT_MASK);
      } else {
        UNREACHABLE();
      }
    }
  }
}

intptr_t EventHandlerImplementation::GetPollEvents(intptr_t events,
                                                   DescriptorInfo* di) {
  // POLLNVAL bedeutet, dass der Deskriptor ungueltig ist - unter epoll faellt
  // das beim Registrieren auf, hier erst hier. In beiden Faellen soll die
  // Dart-Seite ein Close-Ereignis sehen.
  if ((events & POLLNVAL) != 0) {
    return (1 << kCloseEvent);
  }
  if ((events & POLLERR) != 0) {
    return ((events & POLLIN) != 0) ? (1 << kErrorEvent) : 0;
  }
  intptr_t event_mask = 0;
  if ((events & POLLIN) != 0) {
    event_mask |= (1 << kInEvent);
  }
  if ((events & POLLOUT) != 0) {
    event_mask |= (1 << kOutEvent);
  }
  if ((events & POLLHUP) != 0) {
    event_mask |= (1 << kCloseEvent);
  }
  return event_mask;
}

void EventHandlerImplementation::Poll(uword args) {
  EventHandler* handler = reinterpret_cast<EventHandler*>(args);
  EventHandlerImplementation* handler_impl = &handler->delegate_;
  ASSERT(handler_impl != nullptr);

  // Waechst nach Bedarf und wird zwischen den Durchlaeufen wiederverwendet.
  intptr_t capacity = 8;
  struct pollfd* fds =
      static_cast<struct pollfd*>(malloc(capacity * sizeof(struct pollfd)));
  DescriptorInfo** infos =
      static_cast<DescriptorInfo**>(malloc(capacity * sizeof(DescriptorInfo*)));
  if ((fds == nullptr) || (infos == nullptr)) {
    FATAL("Failed allocating poll buffers");
  }

  while (!handler_impl->shutdown_) {
    // Erst zaehlen, dann fuellen. Das erspart ein Vergroessern mitten im
    // Durchlauf – realloc ist in dieser Uebersetzungseinheit nicht eindeutig
    // aufloesbar, weil Dart eigene Speicherfunktionen mitbringt.
    intptr_t needed = 1;  // Weckdeskriptor
    for (SimpleHashMap::Entry* entry = handler_impl->socket_map_.Start();
         entry != nullptr; entry = handler_impl->socket_map_.Next(entry)) {
      DescriptorInfo* di = reinterpret_cast<DescriptorInfo*>(entry->value);
      if ((di != nullptr) && (di->GetPollEvents() != 0)) {
        needed++;
      }
    }

    if (needed > capacity) {
      free(fds);
      free(infos);
      capacity = needed * 2;
      fds = static_cast<struct pollfd*>(
          malloc(capacity * sizeof(struct pollfd)));
      infos = static_cast<DescriptorInfo**>(
          malloc(capacity * sizeof(DescriptorInfo*)));
      if ((fds == nullptr) || (infos == nullptr)) {
        FATAL("Failed growing poll buffers");
      }
    }

    // Der Weckdeskriptor steht immer an erster Stelle.
    fds[0].fd = handler_impl->interrupt_fds_[0];
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    infos[0] = nullptr;
    intptr_t count = 1;

    for (SimpleHashMap::Entry* entry = handler_impl->socket_map_.Start();
         entry != nullptr; entry = handler_impl->socket_map_.Next(entry)) {
      DescriptorInfo* di = reinterpret_cast<DescriptorInfo*>(entry->value);
      if (di == nullptr) {
        continue;
      }
      const intptr_t events = di->GetPollEvents();
      if (events == 0) {
        continue;
      }
      fds[count].fd = static_cast<int>(di->fd());
      fds[count].events = static_cast<short>(events);
      fds[count].revents = 0;
      infos[count] = di;
      count++;
    }

    // Ohne timerfd kommt das Zeitlimit aus der TimeoutQueue. CurrentTimeout()
    // ist ein absoluter Zeitpunkt in Millisekunden.
    int timeout_millis = -1;
    if (handler_impl->timeout_queue_.HasTimeout()) {
      const int64_t remaining = handler_impl->timeout_queue_.CurrentTimeout() -
                                TimerUtils::GetCurrentMonotonicMillis();
      timeout_millis = (remaining <= 0) ? 0 : static_cast<int>(remaining);
    }

    const int result = TEMP_FAILURE_RETRY_NO_SIGNAL_BLOCKER(
        poll(fds, static_cast<nfds_t>(count), timeout_millis));

    if (result < 0) {
      if (errno != EWOULDBLOCK) {
        Syslog::PrintErr("poll failed: %s\n", strerror(errno));
      }
      continue;
    }

    // Zeitlimit abgelaufen bzw. faellige Timer bedienen.
    if (handler_impl->timeout_queue_.HasTimeout() &&
        (handler_impl->timeout_queue_.CurrentTimeout() <=
         TimerUtils::GetCurrentMonotonicMillis())) {
      DartUtils::PostNull(handler_impl->timeout_queue_.CurrentPort());
      handler_impl->timeout_queue_.RemoveCurrent();
    }

    if (result == 0) {
      continue;
    }

    // Erst die Socket-Ereignisse, dann die Weckmeldungen: Sonst koennte ein
    // Socket geschlossen werden, bevor seine anstehenden Ereignisse bearbeitet
    // sind. Dieselbe Reihenfolge wie in der Linux-Fassung.
    bool interrupt_seen = false;
    for (intptr_t i = 0; i < count; i++) {
      if (fds[i].revents == 0) {
        continue;
      }
      if (infos[i] == nullptr) {
        interrupt_seen = true;
        continue;
      }
      DescriptorInfo* di = infos[i];
      const intptr_t event_mask = handler_impl->GetPollEvents(fds[i].revents, di);
      if ((event_mask & (1 << kErrorEvent)) != 0) {
        di->NotifyAllDartPorts(event_mask);
      } else if (event_mask != 0) {
        Dart_Port port = di->NextNotifyDartPort(event_mask);
        ASSERT(port != 0);
        DartUtils::PostInt32(port, event_mask);
      }
    }
    if (interrupt_seen) {
      handler_impl->HandleInterruptFd();
    }
  }

  free(fds);
  free(infos);

  DEBUG_ASSERT(ReferenceCounted<Socket>::instances() == 0);
  handler->NotifyShutdownDone();
}

void EventHandlerImplementation::Start(EventHandler* handler) {
  Thread::Start("dart:io EventHandler", &EventHandlerImplementation::Poll,
                reinterpret_cast<uword>(handler));
}

void EventHandlerImplementation::Shutdown() {
  SendData(kShutdownId, 0, 0);
}

void EventHandlerImplementation::SendData(intptr_t id,
                                          Dart_Port dart_port,
                                          int64_t data) {
  WakeupHandler(id, dart_port, data);
}

void* EventHandlerImplementation::GetHashmapKeyFromFd(intptr_t fd) {
  // Der Schluessel 0 ist in der Hashmap nicht erlaubt.
  return reinterpret_cast<void*>(fd + 1);
}

uint32_t EventHandlerImplementation::GetHashmapHashFromFd(intptr_t fd) {
  return dart::Utils::WordHash(fd + 1);
}

}  // namespace bin
}  // namespace dart

#endif  // defined(DART_HOST_OS_HORIZON)
