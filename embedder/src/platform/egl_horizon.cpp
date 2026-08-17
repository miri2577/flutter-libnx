// EGL-Kontext für GPU-Rendering über Mesa/nouveau.
//
// Der GL-Embedder der Flutter-Engine linkt selbst kein GL: Skia holt sich
// alle Funktionen zur Laufzeit über den proc resolver des Embedders. Diese
// Datei stellt die vier Bausteine, die die FlutterOpenGLRendererConfig
// braucht - Kontext an/aus, Präsentieren, Auflösen - über Mesas EGL auf
// dem Standardfenster der Konsole (dasselbe NWindow, das sonst der
// Software-Framebuffer belegt hätte; beides zugleich geht nicht).
//
// Threading-Vertrag der Engine: make_current/present laufen auf dem
// Raster-Thread. Die Initialisierung hier läuft auf dem Hauptthread und
// gibt den Kontext am Ende wieder frei - EGL erlaubt das Wandern eines
// Kontexts, solange er nirgendwo gebunden ist.
//
// Profilwahl wie in den devkitPro-GL-Beispielen: Desktop-OpenGL Core 4.3
// (das kann nouveau auf dem Tegra X1); Skia erkennt GL-gegen-GLES selbst
// am Versionsstring. Stencil 8 ist Pflicht - ohne es verweigert Skias
// GL-Surface den Dienst.

#include <switch.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <GL/gl.h>

#include <atomic>
#include <stdlib.h>
#include <string.h>

#include "flutter_libnx/log.h"

// Mesas GL/gl.h deckt nur den klassischen Kern ab (siehe
// media_kit_video_horizon.cpp) - fehlende Konstanten selbst definieren.
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#endif

namespace {

EGLDisplay g_display = EGL_NO_DISPLAY;
EGLSurface g_surface = EGL_NO_SURFACE;
EGLContext g_context = EGL_NO_CONTEXT;
EGLConfig g_config = nullptr;

// Geteilter Kontext fuer den mpv-Render-Thread (media_kit_video_horizon.cpp):
// teilt sich die Texturnamen mit g_context, damit der Raster-Thread die von
// mpv gefuellten Texturen direkt abtasten kann. Angelegt wird er lazy auf dem
// Thread, der ihn benutzt; EGL erlaubt eglCreateContext von jedem Thread.
EGLContext g_shared_context = EGL_NO_CONTEXT;
EGLSurface g_shared_surface = EGL_NO_SURFACE;  // 1x1-Pbuffer, nur als Rueckfall

// --- Virtueller Cursor -------------------------------------------------
// Fuer Controller-Bedienung reiner Touch-Apps: Der Plattform-Thread setzt
// Position/Zustand (flutter_libnx_cursor_state), gezeichnet wird auf dem
// Raster-Thread direkt vor eglSwapBuffers. Bewusst nur Scissor+Clear -
// kein Shader, kein Vertex-Zustand. Angefasst und wiederhergestellt werden
// ausschliesslich Scissor, ClearColor, ColorMask und das Framebuffer-
// Binding; alles andere setzt Skia vor seinem naechsten Frame selbst.
std::atomic<uint32_t> g_cursor_pos{0};      // x im High-, y im Low-Halbwort
std::atomic<bool> g_cursor_visible{false};
std::atomic<bool> g_cursor_pressed{false};

// GL-Funktionen zur Laufzeit aufloesen (die Engine linkt kein GL; Mesa
// loest auch Kernfunktionen ueber eglGetProcAddress auf).
struct CursorGl {
  void (GLAPIENTRY* getIntegerv)(GLenum, GLint*);
  void (GLAPIENTRY* getFloatv)(GLenum, GLfloat*);
  void (GLAPIENTRY* getBooleanv)(GLenum, GLboolean*);
  GLboolean (GLAPIENTRY* isEnabled)(GLenum);
  void (GLAPIENTRY* enable)(GLenum);
  void (GLAPIENTRY* disable)(GLenum);
  void (GLAPIENTRY* scissor)(GLint, GLint, GLsizei, GLsizei);
  void (GLAPIENTRY* clearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
  void (GLAPIENTRY* clear)(GLbitfield);
  void (GLAPIENTRY* colorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
  void (GLAPIENTRY* bindFramebuffer)(GLenum, GLuint);
  bool ready = false;
};

CursorGl g_cursor_gl;

bool ResolveCursorGl() {
  if (g_cursor_gl.ready) {
    return true;
  }
  auto resolve = [](const char* name) {
    return eglGetProcAddress(name);
  };
  g_cursor_gl.getIntegerv = reinterpret_cast<void (GLAPIENTRY*)(GLenum, GLint*)>(resolve("glGetIntegerv"));
  g_cursor_gl.getFloatv = reinterpret_cast<void (GLAPIENTRY*)(GLenum, GLfloat*)>(resolve("glGetFloatv"));
  g_cursor_gl.getBooleanv = reinterpret_cast<void (GLAPIENTRY*)(GLenum, GLboolean*)>(resolve("glGetBooleanv"));
  g_cursor_gl.isEnabled = reinterpret_cast<GLboolean (GLAPIENTRY*)(GLenum)>(resolve("glIsEnabled"));
  g_cursor_gl.enable = reinterpret_cast<void (GLAPIENTRY*)(GLenum)>(resolve("glEnable"));
  g_cursor_gl.disable = reinterpret_cast<void (GLAPIENTRY*)(GLenum)>(resolve("glDisable"));
  g_cursor_gl.scissor = reinterpret_cast<void (GLAPIENTRY*)(GLint, GLint, GLsizei, GLsizei)>(resolve("glScissor"));
  g_cursor_gl.clearColor = reinterpret_cast<void (GLAPIENTRY*)(GLfloat, GLfloat, GLfloat, GLfloat)>(resolve("glClearColor"));
  g_cursor_gl.clear = reinterpret_cast<void (GLAPIENTRY*)(GLbitfield)>(resolve("glClear"));
  g_cursor_gl.colorMask = reinterpret_cast<void (GLAPIENTRY*)(GLboolean, GLboolean, GLboolean, GLboolean)>(resolve("glColorMask"));
  g_cursor_gl.bindFramebuffer = reinterpret_cast<void (GLAPIENTRY*)(GLenum, GLuint)>(resolve("glBindFramebuffer"));
  g_cursor_gl.ready = g_cursor_gl.getIntegerv && g_cursor_gl.getFloatv &&
                      g_cursor_gl.getBooleanv && g_cursor_gl.isEnabled &&
                      g_cursor_gl.enable && g_cursor_gl.disable &&
                      g_cursor_gl.scissor && g_cursor_gl.clearColor &&
                      g_cursor_gl.clear && g_cursor_gl.colorMask &&
                      g_cursor_gl.bindFramebuffer;
  if (!g_cursor_gl.ready) {
    LOG_ERROR("Cursor: GL-Funktionen nicht aufloesbar");
  }
  return g_cursor_gl.ready;
}

void DrawCursorOverlay() {
  if (!g_cursor_visible.load(std::memory_order_relaxed) || !ResolveCursorGl()) {
    return;
  }
  const CursorGl& gl = g_cursor_gl;

  EGLint surface_w = 0;
  EGLint surface_h = 0;
  eglQuerySurface(g_display, g_surface, EGL_WIDTH, &surface_w);
  eglQuerySurface(g_display, g_surface, EGL_HEIGHT, &surface_h);
  if (surface_w <= 0 || surface_h <= 0) {
    return;
  }

  const uint32_t pos = g_cursor_pos.load(std::memory_order_relaxed);
  const int cx = static_cast<int>(pos >> 16);
  const int cy = static_cast<int>(pos & 0xffff);
  const bool pressed = g_cursor_pressed.load(std::memory_order_relaxed);

  // Zustand sichern.
  GLint prev_fbo = 0;
  gl.getIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
  if (prev_fbo != 0) {
    gl.bindFramebuffer(GL_FRAMEBUFFER, 0);
  }
  const GLboolean scissor_was_on = gl.isEnabled(GL_SCISSOR_TEST);
  GLint prev_box[4] = {};
  gl.getIntegerv(GL_SCISSOR_BOX, prev_box);
  GLfloat prev_clear[4] = {};
  gl.getFloatv(GL_COLOR_CLEAR_VALUE, prev_clear);
  GLboolean prev_mask[4] = {};
  gl.getBooleanv(GL_COLOR_WRITEMASK, prev_mask);

  gl.colorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  gl.enable(GL_SCISSOR_TEST);

  // GL zaehlt y von unten; Cursor-Koordinaten kommen von oben.
  auto rect = [&](int x, int y, int w, int h, float r, float g, float b) {
    gl.scissor(x, surface_h - y - h, w, h);
    gl.clearColor(r, g, b, 1.0f);
    gl.clear(GL_COLOR_BUFFER_BIT);
  };

  // Einfacher Zeiger: dunkler Rahmen, helle Fuellung; beim Klick tuerkis.
  rect(cx - 9, cy - 9, 18, 18, 0.05f, 0.05f, 0.05f);
  if (pressed) {
    rect(cx - 6, cy - 6, 12, 12, 0.20f, 0.85f, 1.00f);
  } else {
    rect(cx - 6, cy - 6, 12, 12, 0.95f, 0.95f, 0.95f);
  }

  // Zustand wiederherstellen.
  gl.scissor(prev_box[0], prev_box[1], prev_box[2], prev_box[3]);
  if (!scissor_was_on) {
    gl.disable(GL_SCISSOR_TEST);
  }
  gl.clearColor(prev_clear[0], prev_clear[1], prev_clear[2], prev_clear[3]);
  gl.colorMask(prev_mask[0], prev_mask[1], prev_mask[2], prev_mask[3]);
  if (prev_fbo != 0) {
    gl.bindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev_fbo));
  }
}

}  // namespace

extern "C" {

void flutter_libnx_egl_shutdown(void);

bool flutter_libnx_egl_init(void) {
  // Aus den offiziellen Beispielen: Mesas Fehlerprüfungen kosten messbar
  // Zeit; auf einer festen Plattform mit durchgetestetem Treiber ist der
  // schnelle Pfad die richtige Wahl.
  setenv("MESA_NO_ERROR", "1", 1);

  g_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (g_display == EGL_NO_DISPLAY) {
    LOG_ERROR("EGL: kein Display");
    return false;
  }
  if (eglInitialize(g_display, nullptr, nullptr) == EGL_FALSE) {
    LOG_ERROR("EGL: eglInitialize fehlgeschlagen (0x%x)", eglGetError());
    return false;
  }
  if (eglBindAPI(EGL_OPENGL_API) == EGL_FALSE) {
    LOG_ERROR("EGL: eglBindAPI(OPENGL) fehlgeschlagen (0x%x)", eglGetError());
    flutter_libnx_egl_shutdown();
    return false;
  }

  const EGLint framebuffer_attributes[] = {
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE,       8,
      EGL_GREEN_SIZE,     8,
      EGL_BLUE_SIZE,      8,
      EGL_ALPHA_SIZE,     8,
      EGL_DEPTH_SIZE,     24,
      EGL_STENCIL_SIZE,   8,  // Pflicht fuer Skias GL-Surface
      EGL_NONE};
  EGLConfig config = nullptr;
  EGLint config_count = 0;
  if (eglChooseConfig(g_display, framebuffer_attributes, &config, 1,
                      &config_count) == EGL_FALSE ||
      config_count == 0) {
    LOG_ERROR("EGL: keine passende Konfiguration (0x%x)", eglGetError());
    flutter_libnx_egl_shutdown();
    return false;
  }
  g_config = config;

  g_surface = eglCreateWindowSurface(g_display, config, nwindowGetDefault(),
                                     nullptr);
  if (g_surface == EGL_NO_SURFACE) {
    LOG_ERROR("EGL: keine Fensteroberflaeche (0x%x)", eglGetError());
    flutter_libnx_egl_shutdown();
    return false;
  }

  const EGLint context_attributes[] = {
      EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
      EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
      EGL_CONTEXT_MAJOR_VERSION_KHR, 4,
      EGL_CONTEXT_MINOR_VERSION_KHR, 3,
      EGL_NONE};
  g_context = eglCreateContext(g_display, config, EGL_NO_CONTEXT,
                               context_attributes);
  if (g_context == EGL_NO_CONTEXT) {
    LOG_ERROR("EGL: kein Kontext (0x%x)", eglGetError());
    flutter_libnx_egl_shutdown();
    return false;
  }

  // Einmal binden, um das Swap-Intervall (Vsync) zu setzen, dann wieder
  // freigeben - den Kontext uebernimmt spaeter der Raster-Thread.
  if (eglMakeCurrent(g_display, g_surface, g_surface, g_context) ==
      EGL_TRUE) {
    eglSwapInterval(g_display, 1);
    eglMakeCurrent(g_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                   EGL_NO_CONTEXT);
  }

  LOG_INFO("EGL bereit: OpenGL Core 4.3 ueber Mesa/nouveau");
  return true;
}

void flutter_libnx_egl_shutdown(void) {
  if (g_display == EGL_NO_DISPLAY) {
    return;
  }
  eglMakeCurrent(g_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  if (g_shared_context != EGL_NO_CONTEXT) {
    eglDestroyContext(g_display, g_shared_context);
    g_shared_context = EGL_NO_CONTEXT;
  }
  if (g_shared_surface != EGL_NO_SURFACE) {
    eglDestroySurface(g_display, g_shared_surface);
    g_shared_surface = EGL_NO_SURFACE;
  }
  if (g_context != EGL_NO_CONTEXT) {
    eglDestroyContext(g_display, g_context);
    g_context = EGL_NO_CONTEXT;
  }
  if (g_surface != EGL_NO_SURFACE) {
    eglDestroySurface(g_display, g_surface);
    g_surface = EGL_NO_SURFACE;
  }
  eglTerminate(g_display);
  g_display = EGL_NO_DISPLAY;
}

bool flutter_libnx_egl_make_current(void) {
  return eglMakeCurrent(g_display, g_surface, g_surface, g_context) ==
         EGL_TRUE;
}

bool flutter_libnx_egl_clear_current(void) {
  return eglMakeCurrent(g_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                        EGL_NO_CONTEXT) == EGL_TRUE;
}

bool flutter_libnx_egl_present(void) {
  DrawCursorOverlay();
  return eglSwapBuffers(g_display, g_surface) == EGL_TRUE;
}

// Plattform-Thread -> Raster-Thread: Position (Pixel, Ursprung oben links),
// Sichtbarkeit und Klickzustand des virtuellen Cursors.
void flutter_libnx_cursor_state(int x, int y, bool visible, bool pressed) {
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x > 0xffff) x = 0xffff;
  if (y > 0xffff) y = 0xffff;
  g_cursor_pos.store((static_cast<uint32_t>(x) << 16) |
                     static_cast<uint32_t>(y), std::memory_order_relaxed);
  g_cursor_pressed.store(pressed, std::memory_order_relaxed);
  g_cursor_visible.store(visible, std::memory_order_relaxed);
}

void* flutter_libnx_egl_resolve(const char* name) {
  // Mesas EGL kann auch Kernfunktionen aufloesen
  // (EGL_KHR_get_all_proc_addresses).
  return reinterpret_cast<void*>(eglGetProcAddress(name));
}

bool flutter_libnx_egl_is_ready(void) {
  return g_context != EGL_NO_CONTEXT;
}

// Bindet den geteilten Kontext auf dem AUFRUFENDEN Thread; legt ihn beim
// ersten Aufruf an. Bevorzugt surfaceless (Mesa kann EGL_KHR_surfaceless_
// context), sonst ein 1x1-Pbuffer - der braucht eine eigene Config, weil die
// Fenster-Config nur EGL_WINDOW_BIT traegt.
bool flutter_libnx_egl_shared_make_current(void) {
  if (g_context == EGL_NO_CONTEXT) {
    LOG_ERROR("EGL: geteilter Kontext angefordert, aber kein Hauptkontext");
    return false;
  }

  // eglBindAPI ist PER-THREAD-Zustand: Der Hauptthread hat OPENGL gebunden,
  // dieser Thread steht noch auf dem Default (GLES) - und fuer einen
  // GLES-Kontext ist das Core-Profile-Attribut ungueltig. Ohne diese Zeile
  // scheiterte eglCreateContext hier mit 0x3004 (EGL_BAD_ATTRIBUTE).
  if (eglBindAPI(EGL_OPENGL_API) == EGL_FALSE) {
    LOG_ERROR("EGL: eglBindAPI(OPENGL) auf dem Render-Thread scheitert (0x%x)",
              eglGetError());
    return false;
  }

  if (g_shared_context == EGL_NO_CONTEXT) {
    const EGLint context_attributes[] = {
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
        EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
        EGL_CONTEXT_MAJOR_VERSION_KHR, 4,
        EGL_CONTEXT_MINOR_VERSION_KHR, 3,
        EGL_NONE};
    g_shared_context = eglCreateContext(g_display, g_config, g_context,
                                        context_attributes);
    if (g_shared_context == EGL_NO_CONTEXT) {
      LOG_ERROR("EGL: geteilter Kontext scheitert (0x%x)", eglGetError());
      return false;
    }

    const char* extensions = eglQueryString(g_display, EGL_EXTENSIONS);
    const bool surfaceless =
        extensions != nullptr &&
        strstr(extensions, "EGL_KHR_surfaceless_context") != nullptr;
    if (!surfaceless) {
      const EGLint pbuffer_config_attributes[] = {
          EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
          EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
          EGL_RED_SIZE,        8,
          EGL_GREEN_SIZE,      8,
          EGL_BLUE_SIZE,       8,
          EGL_ALPHA_SIZE,      8,
          EGL_NONE};
      EGLConfig pbuffer_config = nullptr;
      EGLint count = 0;
      const EGLint pbuffer_attributes[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1,
                                           EGL_NONE};
      if (eglChooseConfig(g_display, pbuffer_config_attributes,
                          &pbuffer_config, 1, &count) == EGL_TRUE &&
          count > 0) {
        g_shared_surface = eglCreatePbufferSurface(g_display, pbuffer_config,
                                                   pbuffer_attributes);
      }
      if (g_shared_surface == EGL_NO_SURFACE) {
        LOG_ERROR("EGL: weder surfaceless noch Pbuffer verfuegbar (0x%x)",
                  eglGetError());
        eglDestroyContext(g_display, g_shared_context);
        g_shared_context = EGL_NO_CONTEXT;
        return false;
      }
    }
    LOG_INFO("EGL: geteilter mpv-Kontext bereit (%s)",
             surfaceless ? "surfaceless" : "1x1-Pbuffer");
  }

  return eglMakeCurrent(g_display, g_shared_surface, g_shared_surface,
                        g_shared_context) == EGL_TRUE;
}

bool flutter_libnx_egl_shared_clear_current(void) {
  return eglMakeCurrent(g_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                        EGL_NO_CONTEXT) == EGL_TRUE;
}

// Sichern/Wiederherstellen der aktuellen Bindung - fuer den mpv-Umweg AUF
// DEM RASTER-THREAD (media_kit_video_horizon.cpp): Skias Fensterkontext
// merken, den geteilten Kontext binden, danach exakt zurueck. Bewusst kein
// Thread-Local: Es gibt zu jedem Zeitpunkt nur einen GL-Thread (Raster;
// nach dem Engine-Abbau der Plattform-Thread) - genau das ist die Lehre aus
// dem nouveau-Absturz bei nebenlaeufiger Submission.
namespace {
EGLDisplay g_saved_display = EGL_NO_DISPLAY;
EGLContext g_saved_context = EGL_NO_CONTEXT;
EGLSurface g_saved_draw = EGL_NO_SURFACE;
EGLSurface g_saved_read = EGL_NO_SURFACE;
}  // namespace

bool flutter_libnx_egl_shared_bind_saving(void) {
  g_saved_display = eglGetCurrentDisplay();
  g_saved_context = eglGetCurrentContext();
  g_saved_draw = eglGetCurrentSurface(EGL_DRAW);
  g_saved_read = eglGetCurrentSurface(EGL_READ);
  return flutter_libnx_egl_shared_make_current();
}

void flutter_libnx_egl_shared_restore(void) {
  if (g_saved_context == EGL_NO_CONTEXT) {
    eglMakeCurrent(g_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    return;
  }
  eglMakeCurrent(g_saved_display, g_saved_draw, g_saved_read, g_saved_context);
}

}  // extern "C"
