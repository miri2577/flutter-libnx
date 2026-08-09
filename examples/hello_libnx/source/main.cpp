// Meilenstein 1: minimaler libnx-Host, noch komplett ohne Flutter.
//
// Zweck ist nicht die Grafik, sondern der Nachweis, dass die Bausteine stehen,
// auf denen der Embedder später aufsetzt: Framebuffer in linearem RGBA8888,
// Logging über nxlink und SD-Karte, Eingabe, sauberes Beenden.

#include <switch.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "flutter_libnx/log.h"
#include "flutter_libnx/switch_platform.h"

namespace {

constexpr const char* kLogPath = "sdmc:/switch/flutter-libnx/hello_libnx.log";

// Gesammelte Messwerte. Werden nach dem Beenden auf dem Bildschirm angezeigt,
// weil die Logkanäle (nxlink, SD) nicht in jeder Umgebung verfügbar sind – der
// Bildschirm dagegen immer.
struct Diagnostics {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t stride = 0;
  uint32_t frames = 0;
  bool nxlink = false;
};

const char* AppletTypeName(AppletType type) {
  switch (type) {
    case AppletType_Application:       return "Application (Titeluebernahme)";
    case AppletType_SystemApplication: return "SystemApplication";
    case AppletType_LibraryApplet:     return "LibraryApplet (Applet-Modus)";
    case AppletType_SystemApplet:      return "SystemApplet";
    case AppletType_OverlayApplet:     return "OverlayApplet";
    case AppletType_None:              return "None";
    case AppletType_Default:           return "Default";
  }
  return "unbekannt";
}

// Zeigt die Messwerte auf der Konsole an. Das geht erst, nachdem der
// Framebuffer geschlossen wurde – beide beanspruchen dasselbe Fenster.
void ShowSummary(const Diagnostics& diag) {
  consoleInit(nullptr);

  std::printf("\n  flutter-libnx / hello_libnx – Messwerte\n");
  std::printf("  =======================================\n\n");

  std::printf("  Aufloesung      : %u x %u\n", diag.width, diag.height);
  std::printf("  Stride          : %u Bytes\n", diag.stride);
  std::printf("  erwartet (w*4)  : %u Bytes\n", diag.width * 4);
  if (diag.stride == diag.width * 4) {
    std::printf("  -> passt: Flutter-Puffer kann direkt durchgereicht werden\n");
  } else {
    std::printf("  -> ABWEICHUNG: zeilenweises Umkopieren noetig\n");
  }

  std::printf("\n  Frames gerendert: %u\n", diag.frames);
  std::printf("  nxlink verbunden: %s\n", diag.nxlink ? "ja" : "nein");
  std::printf("  Logdatei        : %s\n", kLogPath);

  std::printf("\n  Laufzeitumgebung: %s\n", AppletTypeName(appletGetAppletType()));

  u64 total = 0;
  u64 used = 0;
  const Result rc_total =
      svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
  const Result rc_used =
      svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
  if (R_SUCCEEDED(rc_total) && R_SUCCEEDED(rc_used)) {
    // u64 ist auf AArch64 ein unsigned long, nicht long long.
    std::printf("  Speicher gesamt : %lu MB\n", total / (1024 * 1024));
    std::printf("  davon belegt    : %lu MB\n", used / (1024 * 1024));
  } else {
    std::printf("  Speicher        : svcGetInfo fehlgeschlagen (0x%08x/0x%08x)\n",
                rc_total, rc_used);
  }

  std::printf("\n  Plus zum Beenden.\n");
  consoleUpdate(nullptr);

  PadState pad;
  padInitializeDefault(&pad);
  while (appletMainLoop()) {
    padUpdate(&pad);
    if (padGetButtonsDown(&pad) & HidNpadButton_Plus) {
      break;
    }
    consoleUpdate(nullptr);
  }

  consoleExit(nullptr);
}

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
  Diagnostics diag;
  diag.width = platform.width();
  diag.height = platform.height();
  diag.nxlink = flutter_libnx::LogHasNxlink();

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
      diag.stride = stride;
      first_frame_logged = true;
    }
    ++frame;
  }

  diag.frames = frame;
  LOG_INFO("Schleife beendet nach %u Frames", frame);

  // Framebuffer muss zuerst weg, sonst kann die Konsole das Fenster nicht belegen.
  platform.Shutdown();
  ShowSummary(diag);

  flutter_libnx::LogShutdown();
  return 0;
}
