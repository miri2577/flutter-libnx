// Meilenstein 1: minimaler libnx-Host, noch komplett ohne Flutter.
//
// Zweck ist nicht die Grafik, sondern der Nachweis, dass die Bausteine stehen,
// auf denen der Embedder später aufsetzt: Framebuffer in linearem RGBA8888,
// Logging über nxlink und SD-Karte, Eingabe, sauberes Beenden.

#include <switch.h>

#include <cstdint>
#include <cstring>

#include "flutter_libnx/log.h"
#include "flutter_libnx/switch_platform.h"

namespace {

constexpr const char* kLogPath = "sdmc:/switch/flutter-libnx/hello_libnx.log";

void FillRect(uint8_t* pixels, uint32_t stride, uint32_t x0, uint32_t y0,
              uint32_t w, uint32_t h, uint32_t rgba) {
  for (uint32_t y = y0; y < y0 + h; ++y) {
    auto* row = reinterpret_cast<uint32_t*>(pixels + y * stride);
    for (uint32_t x = x0; x < x0 + w; ++x) {
      row[x] = rgba;
    }
  }
}

void Clear(uint8_t* pixels, uint32_t stride, uint32_t height, uint32_t rgba) {
  for (uint32_t y = 0; y < height; ++y) {
    auto* row = reinterpret_cast<uint32_t*>(pixels + y * stride);
    // Zeilenweise setzen statt memset, weil der Wert 32 Bit breit ist.
    for (uint32_t x = 0; x < stride / 4; ++x) {
      row[x] = rgba;
    }
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  flutter_libnx::LogConfig log_config;
  log_config.to_nxlink = true;
  log_config.file_path = kLogPath;
  if (!flutter_libnx::LogInit(log_config)) {
    // Ohne jede Senke läuft die App weiter, aber blind. Das ist kein Grund
    // abzubrechen – auf Hardware ohne nxlink und ohne SD ist das der Normalfall.
  }

  LOG_INFO("hello_libnx startet");

  flutter_libnx::SwitchPlatform platform;
  if (!platform.Initialize()) {
    LOG_ERROR("Plattform konnte nicht initialisiert werden – Abbruch");
    flutter_libnx::LogShutdown();
    return 1;
  }

  bool first_frame_logged = false;
  uint32_t frame = 0;

  while (!platform.ShouldQuit()) {
    platform.PollEvents();

    const uint64_t pressed = padGetButtonsDown(&platform.pad());
    if (pressed & HidNpadButton_Plus) {
      LOG_INFO("Plus gedrückt – Beenden angefordert");
      platform.RequestQuit();
    }
    if (pressed & HidNpadButton_A) {
      LOG_INFO("A gedrückt (Frame %u)", frame);
    }

    uint32_t stride = 0;
    uint8_t* pixels = platform.BeginFrame(&stride);
    if (pixels == nullptr) {
      LOG_ERROR("Kein Framebuffer – Schleife wird beendet");
      break;
    }

    Clear(pixels, stride, platform.height(), RGBA8_MAXALPHA(20, 24, 40));

    // Wandernder Balken: zeigt auf einen Blick, ob wirklich neue Frames
    // ankommen oder ob nur ein Standbild liegen bleibt.
    const uint32_t box = 96;
    const uint32_t travel = platform.width() - box;
    const uint32_t phase = frame % (2 * travel);
    const uint32_t x = phase < travel ? phase : (2 * travel - phase);
    FillRect(pixels, stride, x, platform.height() / 2 - box / 2, box, box,
             RGBA8_MAXALPHA(80, 180, 255));

    // Zustandsanzeige unten links, solange A gehalten wird.
    if (padGetButtons(&platform.pad()) & HidNpadButton_A) {
      FillRect(pixels, stride, 48, platform.height() - 128, 64, 64,
               RGBA8_MAXALPHA(255, 200, 60));
    }

    platform.EndFrame();

    if (!first_frame_logged) {
      LOG_INFO("first frame presented (stride=%u bytes, erwartet %u)", stride,
               platform.width() * 4);
      first_frame_logged = true;
    }
    ++frame;
  }

  LOG_INFO("Schleife beendet nach %u Frames", frame);
  platform.Shutdown();
  flutter_libnx::LogShutdown();
  return 0;
}
