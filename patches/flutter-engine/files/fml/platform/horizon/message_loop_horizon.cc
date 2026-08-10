// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/fml/platform/horizon/message_loop_horizon.h"

namespace fml {

MessageLoopHorizon::MessageLoopHorizon() = default;

MessageLoopHorizon::~MessageLoopHorizon() = default;

// |fml::MessageLoopImpl|
void MessageLoopHorizon::Run() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = true;
  }

  while (true) {
    bool run_tasks = false;

    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (!running_) {
        break;
      }

      if (!has_wakeup_) {
        // Nichts steht an. Warten, bis jemand WakeUp oder Terminate ruft.
        cv_.wait(lock);
        continue;
      }

      const fml::TimeDelta remaining = wakeup_time_ - fml::TimePoint::Now();
      if (remaining <= fml::TimeDelta::Zero()) {
        has_wakeup_ = false;
        run_tasks = true;
      } else {
        // Bewusst eine Dauer statt eines absoluten Zeitpunkts: fml::TimePoint
        // und std::chrono::steady_clock muessen nicht dieselbe Uhr sein. Die
        // Restzeit wird in jedem Durchlauf neu berechnet, deshalb sind
        // vorzeitige Aufwacher unschaedlich.
        cv_.wait_for(lock,
                     std::chrono::nanoseconds(remaining.ToNanoseconds()));
        continue;
      }
    }

    if (run_tasks) {
      // Ausserhalb der Sperre: Die Aufgaben duerfen ihrerseits WakeUp rufen.
      RunExpiredTasksNow();
    }
  }
}

// |fml::MessageLoopImpl|
void MessageLoopHorizon::Terminate() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
  }
  cv_.notify_all();
}

// |fml::MessageLoopImpl|
void MessageLoopHorizon::WakeUp(fml::TimePoint time_point) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    // Der frueheste angemeldete Zeitpunkt gewinnt. Spaetere Aufgaben kommen
    // beim naechsten Durchlauf dran, wenn RunExpiredTasksNow sie erneut
    // anmeldet.
    if (has_wakeup_ && wakeup_time_ <= time_point) {
      return;
    }
    wakeup_time_ = time_point;
    has_wakeup_ = true;
  }
  cv_.notify_all();
}

}  // namespace fml
