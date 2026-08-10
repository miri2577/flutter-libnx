// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_FML_PLATFORM_HORIZON_MESSAGE_LOOP_HORIZON_H_
#define FLUTTER_FML_PLATFORM_HORIZON_MESSAGE_LOOP_HORIZON_H_

#include <condition_variable>
#include <mutex>

#include "flutter/fml/macros.h"
#include "flutter/fml/message_loop_impl.h"
#include "flutter/fml/time/time_point.h"

namespace fml {

/// Nachrichtenschleife fuer Horizon.
///
/// Die Linux-Fassung baut auf `epoll` und `timerfd` auf; Horizon hat weder das
/// eine noch das andere. Was die Schleife wirklich braucht, ist aber nur
/// zweierlei: bis zu einem Zeitpunkt warten und vorzeitig geweckt werden
/// koennen. Eine Bedingungsvariable leistet genau das, ohne Umweg ueber
/// Dateideskriptoren.
class MessageLoopHorizon : public MessageLoopImpl {
 private:
  std::mutex mutex_;
  std::condition_variable cv_;

  // Wann die Schleife spaetestens wieder aufwachen soll. Gueltig nur, wenn
  // has_wakeup_ gesetzt ist - bewusst kein Sonderwert wie TimePoint::Max(),
  // damit die Bedeutung im Code sichtbar bleibt.
  fml::TimePoint wakeup_time_;
  bool has_wakeup_ = false;
  bool running_ = false;

  MessageLoopHorizon();

  ~MessageLoopHorizon() override;

  // |fml::MessageLoopImpl|
  void Run() override;

  // |fml::MessageLoopImpl|
  void Terminate() override;

  // |fml::MessageLoopImpl|
  void WakeUp(fml::TimePoint time_point) override;

  FML_FRIEND_MAKE_REF_COUNTED(MessageLoopHorizon);
  FML_FRIEND_REF_COUNTED_THREAD_SAFE(MessageLoopHorizon);
  FML_DISALLOW_COPY_AND_ASSIGN(MessageLoopHorizon);
};

}  // namespace fml

#endif  // FLUTTER_FML_PLATFORM_HORIZON_MESSAGE_LOOP_HORIZON_H_
