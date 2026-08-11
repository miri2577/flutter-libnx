#include "flutter_libnx/switch_platform.h"

#include "flutter_libnx/log.h"

namespace flutter_libnx {

SwitchPlatform::~SwitchPlatform() {
  Shutdown();
}

bool SwitchPlatform::Initialize(uint32_t width, uint32_t height) {
  if (initialized_) {
    LOG_WARN("Initialize() doppelt aufgerufen – ignoriert");
    return true;
  }

  LOG_INFO("initializing platform (%ux%u)", width, height);

  // Eingabe zuerst: Sie hat keine Ressourcen, die wir bei einem Framebuffer-
  // Fehlschlag wieder freigeben müssten.
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  padInitializeDefault(&pad_);

  // Vorzustand einlesen, bevor irgendjemand Tastendrücke auswertet.
  //
  // Ein frisch angelegter PadState hat `buttons_old == 0`. Ist beim ersten
  // padUpdate eine Taste noch physisch gedrückt – und aus dem hbmenu heraus
  // ist das regelmäßig A –, meldet padGetButtonsDown sie als *neuen* Druck.
  // Beim Diagnoseschirm von hello_libnx hat das den Schirm sofort wieder
  // geschlossen; sobald A als Enter an Flutter geht, würde es hier den ersten
  // fokussierten Knopf auslösen, ohne dass jemand ihn gedrückt hat.
  padUpdate(&pad_);

  // Der Touchscreen ist im Dock-Betrieb wirkungslos, im Handheld-Betrieb aber
  // die naheliegendste Bedienung für eine Oberfläche, die für Berührung
  // entworfen wurde. Er meldet dieselben Koordinaten wie der Framebuffer
  // (1280x720), eine Umrechnung entfällt.
  hidInitializeTouchScreen();

  NWindow* window = nwindowGetDefault();
  if (window == nullptr) {
    LOG_ERROR("nwindowGetDefault() lieferte nullptr");
    return false;
  }

  Result rc = framebufferCreate(&framebuffer_, window, width, height,
                                PIXEL_FORMAT_RGBA_8888, 2);
  if (R_FAILED(rc)) {
    LOG_ERROR("framebufferCreate fehlgeschlagen: 0x%08x (Modul %u, Beschreibung %u)",
              rc, R_MODULE(rc), R_DESCRIPTION(rc));
    return false;
  }

  // Ohne diesen Schritt liegt der Puffer im Tegra-Block-Linear-Format vor.
  // Flutter liefert einen linearen RGBA-Puffer, deshalb brauchen wir den
  // Schattenpuffer – die Umwandlung übernimmt framebufferEnd().
  rc = framebufferMakeLinear(&framebuffer_);
  if (R_FAILED(rc)) {
    LOG_ERROR("framebufferMakeLinear fehlgeschlagen: 0x%08x", rc);
    framebufferClose(&framebuffer_);
    return false;
  }

  width_ = width;
  height_ = height;
  framebuffer_ready_ = true;
  initialized_ = true;
  applet_running_ = true;
  quit_requested_ = false;

  LOG_INFO("platform bereit");
  return true;
}

void SwitchPlatform::Shutdown() {
  if (!initialized_) {
    return;
  }

  if (frame_active_) {
    // Ein begonnener Frame muss abgeschlossen werden, sonst bleibt ein Puffer
    // in der NWindow-Queue hängen.
    LOG_WARN("Shutdown mit offenem Frame – wird abgeschlossen");
    framebufferEnd(&framebuffer_);
    frame_active_ = false;
  }

  if (framebuffer_ready_) {
    framebufferClose(&framebuffer_);
    framebuffer_ready_ = false;
  }

  initialized_ = false;
  LOG_INFO("platform heruntergefahren");
}

void SwitchPlatform::PollEvents() {
  // appletMainLoop() liefert false, sobald Horizon das Beenden anfordert.
  applet_running_ = appletMainLoop();
  padUpdate(&pad_);

  // Berührungen einlesen. hidGetTouchScreenStates liefert die jüngsten
  // Zustände; einer genügt, weil wir ohnehin jeden Frame abfragen.
  touch_count_ = 0;
  HidTouchScreenState state = {};
  if (hidGetTouchScreenStates(&state, 1) > 0) {
    const int count = static_cast<int>(state.count) < kMaxTouchPoints
                          ? static_cast<int>(state.count)
                          : kMaxTouchPoints;
    for (int i = 0; i < count; i++) {
      touches_[i].id = static_cast<int32_t>(state.touches[i].finger_id);
      touches_[i].x = static_cast<double>(state.touches[i].x);
      touches_[i].y = static_cast<double>(state.touches[i].y);
    }
    touch_count_ = count;
  }
}

uint8_t* SwitchPlatform::BeginFrame(uint32_t* out_stride) {
  if (!framebuffer_ready_) {
    LOG_ERROR("BeginFrame ohne initialisierten Framebuffer");
    return nullptr;
  }
  if (frame_active_) {
    LOG_ERROR("BeginFrame ohne vorheriges EndFrame");
    return nullptr;
  }

  uint32_t stride = 0;
  auto* pixels = static_cast<uint8_t*>(framebufferBegin(&framebuffer_, &stride));
  if (pixels == nullptr) {
    LOG_ERROR("framebufferBegin lieferte nullptr");
    return nullptr;
  }

  frame_active_ = true;
  if (out_stride != nullptr) {
    *out_stride = stride;
  }
  return pixels;
}

void SwitchPlatform::EndFrame() {
  if (!frame_active_) {
    LOG_ERROR("EndFrame ohne BeginFrame");
    return;
  }
  framebufferEnd(&framebuffer_);
  frame_active_ = false;
}

}  // namespace flutter_libnx
