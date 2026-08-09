// Plattformschicht: Framebuffer, Eingabe und Applet-Lebenszyklus der Switch.
//
// Bewusst ohne jede Flutter-Abhängigkeit. Der spätere FlutterHost bekommt diese
// Klasse gereicht und braucht selbst kein libnx zu kennen.
#pragma once

#include <cstdint>

#include <switch.h>

namespace flutter_libnx {

// Handheld-Auflösung. Docked (1920x1080) kommt später; solange kein natives
// 720p-Bild steht, wird kein Scaling implementiert.
inline constexpr uint32_t kDefaultWidth = 1280;
inline constexpr uint32_t kDefaultHeight = 720;

class SwitchPlatform {
 public:
  SwitchPlatform() = default;
  ~SwitchPlatform();

  SwitchPlatform(const SwitchPlatform&) = delete;
  SwitchPlatform& operator=(const SwitchPlatform&) = delete;

  // Richtet Framebuffer und Eingabe ein. Bei false ist nichts allokiert.
  bool Initialize(uint32_t width = kDefaultWidth,
                  uint32_t height = kDefaultHeight);
  void Shutdown();

  // Einmal pro Frame aufrufen: liest Eingaben und wertet Applet-Nachrichten aus.
  void PollEvents();

  // true, sobald Horizon das Beenden verlangt oder RequestQuit() gerufen wurde.
  bool ShouldQuit() const { return quit_requested_ || !applet_running_; }
  void RequestQuit() { quit_requested_ = true; }

  // Liefert einen linearen RGBA8888-Puffer. out_stride ist der Zeilenabstand in
  // Bytes und ist nicht zwingend width * 4 – immer den zurückgegebenen Wert nutzen.
  // Genau dieser Puffer ist später das Ziel des Flutter-Software-Renderers.
  uint8_t* BeginFrame(uint32_t* out_stride);
  void EndFrame();

  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }

  const PadState& pad() const { return pad_; }

 private:
  Framebuffer framebuffer_{};
  PadState pad_{};
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  bool initialized_ = false;
  bool framebuffer_ready_ = false;
  bool frame_active_ = false;
  bool applet_running_ = true;
  bool quit_requested_ = false;
};

}  // namespace flutter_libnx
