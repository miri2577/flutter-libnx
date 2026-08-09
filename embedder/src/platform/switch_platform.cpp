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
