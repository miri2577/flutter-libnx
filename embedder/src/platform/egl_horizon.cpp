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

#include <stdlib.h>

#include "flutter_libnx/log.h"

namespace {

EGLDisplay g_display = EGL_NO_DISPLAY;
EGLSurface g_surface = EGL_NO_SURFACE;
EGLContext g_context = EGL_NO_CONTEXT;

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
  return eglSwapBuffers(g_display, g_surface) == EGL_TRUE;
}

void* flutter_libnx_egl_resolve(const char* name) {
  // Mesas EGL kann auch Kernfunktionen aufloesen
  // (EGL_KHR_get_all_proc_addresses).
  return reinterpret_cast<void*>(eglGetProcAddress(name));
}

}  // extern "C"
