// media_kit_video: der Kanal com.alexmercerind/media_kit_video und die
// Render-Bruecke von libmpv in Flutters externe Texturen.
//
// Protokoll (nachgelesen in media_kit_video-2.0.1, native_video_controller/
// real.dart - StandardMethodCodec, nicht JSON):
//   Dart -> Embedder:
//     VideoOutputManager.Create  {handle: String, configuration:
//         {width: String|"null", height: String|"null",
//          enableHardwareAcceleration: bool}}
//     VideoOutputManager.SetSize {handle: String, width: String, height: String}
//     VideoOutputManager.Dispose {handle: String}
//   Embedder -> Dart (Methodenaufruf ohne Antwort):
//     VideoOutput.Resize {handle: int, id: int,
//                         rect: {left,top,width,height: int}}
//   `handle` ist die Adresse des mpv_handle (media_kit reicht sie als
//   Dezimalstring). `id` ist die Flutter-Textur-ID fuer das Texture-Widget.
//   Dart wartet nach Create, bis die erste Resize-Meldung die id setzt.
//
// Architektur - ALLE GL-Arbeit auf dem Raster-Thread:
//
// Die erste Fassung hatte einen eigenen mpv-Render-Thread mit geteiltem
// Kontext. Auf Hardware starb sie im nouveau-Treiber (Data Abort in
// nvc0_bufctx_fence aus nvc0_state_validate_3d), sobald Skia und mpv
// gleichzeitig Kommandos absetzten - Mesa auf diesem Port traegt keine
// nebenlaeufige Submission aus zwei Threads. Deshalb jetzt das Modell der
// GTK-Referenzimplementierung (texture_gl.cc in media_kit_video/linux):
//
//   * Gerendert wird IM Textur-Frame-Callback der Engine, also auf dem
//     Raster-Thread waehrend der Komposition: Skias Fensterkontext sichern,
//     den geteilten Kontext binden, mpv_render_context_render in die
//     FBO-Textur, glFlush, Bindung wiederherstellen, Texturnamen liefern.
//     Ein Puffer genuegt - Erzeuger und Verbraucher sind derselbe Thread.
//   * Der mpv-Render-Kontext entsteht lazy im selben Callback (mpv verlangt
//     einen gebundenen GL-Kontext bei create UND render).
//   * mpvs Update-Callback (mpv-interner Thread) setzt nur ein Flag; die
//     Hauptschleife pumpt es als MarkExternalTextureFrameAvailable zur
//     Engine (verlangt den Plattform-Thread) - das stoesst die naechste
//     Komposition an, die dann rendert.
//   * Dispose muss den mpv-Render-Kontext freigeben, BEVOR Dart
//     mpv_terminate_destroy ruft - und zwar auf dem Raster-Thread
//     (FlutterEnginePostRenderThreadTask), aus demselben Grund wie oben.
//   * Nach FlutterEngineShutdown ist der Raster-Thread Geschichte; der
//     Plattform-Thread ist dann der einzige GL-Thread und darf im
//     Shutdown-Pfad direkt aufraeumen.

#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <GL/gl.h>

#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>

#include "embedder.h"
#include "flutter_libnx/log.h"
#include "flutter_libnx/standard_message_codec.h"

// Mesas GL/gl.h deckt nur den klassischen Kern ab; die FBO-Konstanten kommen
// sonst aus glext.h. Selbst definieren ist hier billiger als eine weitere
// Header-Abhaengigkeit - die Werte sind Teil der GL-Spezifikation.
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

extern "C" bool flutter_libnx_egl_is_ready(void);
extern "C" bool flutter_libnx_egl_make_current(void);
extern "C" void* flutter_libnx_egl_resolve(const char* name);

namespace flutter_libnx {

namespace {

constexpr const char* kChannel = "com.alexmercerind/media_kit_video";

// Die Engine linkt kein GL - alle Funktionen kommen wie bei Skia zur
// Laufzeit ueber den EGL-Resolver. Aufgeloest beim ersten Gebrauch.
struct GlFunctions {
  void (*GenTextures)(GLsizei, GLuint*);
  void (*DeleteTextures)(GLsizei, const GLuint*);
  void (*BindTexture)(GLenum, GLuint);
  void (*TexParameteri)(GLenum, GLenum, GLint);
  void (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum,
                     GLenum, const void*);
  void (*GenFramebuffers)(GLsizei, GLuint*);
  void (*DeleteFramebuffers)(GLsizei, const GLuint*);
  void (*BindFramebuffer)(GLenum, GLuint);
  void (*FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
  GLenum (*CheckFramebufferStatus)(GLenum);
  void (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
  void (*Clear)(GLbitfield);
  void (*Flush)(void);
  void (*Finish)(void);
  void (*ColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
  void (*Disable)(GLenum);
  void (*ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);

  bool loaded = false;

  bool Load() {
    if (loaded) {
      return true;
    }
    auto get = [](const char* name) { return flutter_libnx_egl_resolve(name); };
    GenTextures = reinterpret_cast<decltype(GenTextures)>(get("glGenTextures"));
    DeleteTextures =
        reinterpret_cast<decltype(DeleteTextures)>(get("glDeleteTextures"));
    BindTexture = reinterpret_cast<decltype(BindTexture)>(get("glBindTexture"));
    TexParameteri =
        reinterpret_cast<decltype(TexParameteri)>(get("glTexParameteri"));
    TexImage2D = reinterpret_cast<decltype(TexImage2D)>(get("glTexImage2D"));
    GenFramebuffers =
        reinterpret_cast<decltype(GenFramebuffers)>(get("glGenFramebuffers"));
    DeleteFramebuffers = reinterpret_cast<decltype(DeleteFramebuffers)>(
        get("glDeleteFramebuffers"));
    BindFramebuffer =
        reinterpret_cast<decltype(BindFramebuffer)>(get("glBindFramebuffer"));
    FramebufferTexture2D = reinterpret_cast<decltype(FramebufferTexture2D)>(
        get("glFramebufferTexture2D"));
    CheckFramebufferStatus = reinterpret_cast<decltype(CheckFramebufferStatus)>(
        get("glCheckFramebufferStatus"));
    ClearColor = reinterpret_cast<decltype(ClearColor)>(get("glClearColor"));
    Clear = reinterpret_cast<decltype(Clear)>(get("glClear"));
    Flush = reinterpret_cast<decltype(Flush)>(get("glFlush"));
    Finish = reinterpret_cast<decltype(Finish)>(get("glFinish"));
    ColorMask = reinterpret_cast<decltype(ColorMask)>(get("glColorMask"));
    Disable = reinterpret_cast<decltype(Disable)>(get("glDisable"));
    ReadPixels = reinterpret_cast<decltype(ReadPixels)>(get("glReadPixels"));
    loaded = GenTextures && DeleteTextures && BindTexture && TexParameteri &&
             TexImage2D && GenFramebuffers && DeleteFramebuffers &&
             BindFramebuffer && FramebufferTexture2D &&
             CheckFramebufferStatus && ClearColor && Clear && Flush &&
             Finish && ColorMask && Disable && ReadPixels;
    return loaded;
  }
};

GlFunctions g_gl = {};

struct VideoOutput {
  int64_t handle = 0;       // Adresse des mpv_handle
  int64_t texture_id = 0;   // Flutter-Textur-ID (Texture-Widget)
  mpv_render_context* render_context = nullptr;

  // Ein Puffer genuegt: gerendert und abgetastet wird auf demselben Thread.
  GLuint texture = 0;
  GLuint framebuffer = 0;
  int width = 0;    // aktuell angelegte Texturgroesse
  int height = 0;
  int pending_width = 0;   // von Create/SetSize gewuenscht
  int pending_height = 0;

  // mpv hat einen neuen Frame; gesetzt vom mpv-internen Thread, von der
  // Pumpe (Plattform-Thread) in MarkExternalTextureFrameAvailable uebersetzt.
  std::atomic<bool> frame_pending{false};
  bool dispose_requested = false;
  bool create_failed = false;

  // DIAGNOSE (kein Bild trotz laufender Kette): zaehlt Callback-Laeufe und
  // tatsaechliche Rendervorgaenge. Nach der Stabilisierung ausduennen.
  uint32_t callback_count = 0;
  uint32_t render_count = 0;
  bool delivered_logged = false;
};

// Schuetzt die Liste und alle nicht-atomaren Felder. Kontrahenten sind der
// Plattform-Thread (Kanal, Pumpe) und der Raster-Thread (Frame-Callback,
// Dispose-Task) - mpvs Threads fassen nur das atomare Flag an.
std::mutex g_mutex;
std::vector<std::unique_ptr<VideoOutput>> g_outputs;
int64_t g_next_texture_id = 1;

void MpvUpdateCallback(void* user_data) {
  // Laeuft auf einem mpv-internen Thread: nur das Flag setzen.
  static_cast<VideoOutput*>(user_data)->frame_pending = true;
}

void* MpvGetProcAddress(void*, const char* name) {
  return flutter_libnx_egl_resolve(name);
}

// mpv-Render-Kontext anlegen. Einziger GL-Thread, geteilter Kontext wird
// gebunden und wieder geloest, g_mutex gehalten.
bool CreateRenderContextLocked(VideoOutput* output) {
  if (output->render_context != nullptr) {
    return true;
  }
  // Fensterkontext, nicht der geteilte: mpv lebt im selben Kontext wie
  // Skia (Begruendung am Frame-Callback). Auf dem Raster-Thread ist er
  // ohnehin meist gebunden; das erneute Binden ist billig.
  if (!flutter_libnx_egl_make_current()) {
    LOG_ERROR("media_kit_video: EGL-Fensterkontext nicht bindbar - "
              "Video bleibt aus");
    output->create_failed = true;
    return false;
  }
  bool ok = false;
  if (!g_gl.Load()) {
    LOG_ERROR("media_kit_video: GL-Funktionen nicht aufloesbar");
    output->create_failed = true;
  } else {
    auto* mpv =
        reinterpret_cast<mpv_handle*>(static_cast<intptr_t>(output->handle));
    mpv_opengl_init_params gl_init = {};
    gl_init.get_proc_address = MpvGetProcAddress;
    int advanced_control = 0;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE,
         const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init},
        {MPV_RENDER_PARAM_ADVANCED_CONTROL, &advanced_control},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    const int rc =
        mpv_render_context_create(&output->render_context, mpv, params);
    if (rc < 0) {
      LOG_ERROR("media_kit_video: mpv_render_context_create: %s",
                mpv_error_string(rc));
      output->create_failed = true;
      output->render_context = nullptr;
    } else {
      mpv_render_context_set_update_callback(output->render_context,
                                             MpvUpdateCallback, output);
      LOG_INFO("media_kit_video: Render-Kontext fuer Handle %lld bereit "
               "(Textur %lld)",
               static_cast<long long>(output->handle),
               static_cast<long long>(output->texture_id));
      ok = true;
    }
  }
  // Der Fensterkontext bleibt gebunden - der Raster-Thread bindet ihn zu
  // Frame-Beginn ohnehin selbst.
  return ok;
}

// Eager-Anlage des Render-Kontexts nach Create, als Task auf dem Raster-
// Thread. WICHTIG: Ohne existierenden Render-Kontext waehlt mpv gar keine
// Videospur - dann kommen nie videoParams, media_kit schickt nie SetSize,
// das Rechteck bleibt 0x0, das Widget wird nie komponiert, und der
// Frame-Callback (der den Kontext lazy anlegen wuerde) laeuft nie. Genau
// dieses Henne-Ei hat den Intro-Lauf ohne Bild erklaert. Der Task traegt
// den Handle, keinen Zeiger - ein Dispose dazwischen macht ihn zum No-Op.
void CreateOnRasterThread(void* user_data) {
  const int64_t handle = reinterpret_cast<intptr_t>(user_data);
  std::lock_guard<std::mutex> lock(g_mutex);
  VideoOutput* output = nullptr;
  for (auto& candidate : g_outputs) {
    if (candidate->handle == handle) {
      output = candidate.get();
      break;
    }
  }
  if (output == nullptr || output->dispose_requested ||
      output->create_failed) {
    return;
  }
  CreateRenderContextLocked(output);
}

// FBO+Textur (neu) anlegen. Geteilter Kontext gebunden, g_mutex gehalten.
bool AllocateBuffer(VideoOutput* output, int width, int height) {
  if (output->texture != 0) {
    g_gl.DeleteFramebuffers(1, &output->framebuffer);
    g_gl.DeleteTextures(1, &output->texture);
  }
  g_gl.GenTextures(1, &output->texture);
  g_gl.GenFramebuffers(1, &output->framebuffer);
  g_gl.BindTexture(GL_TEXTURE_2D, output->texture);
  g_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  g_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  g_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  g_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  g_gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                  GL_UNSIGNED_BYTE, nullptr);
  g_gl.BindFramebuffer(GL_FRAMEBUFFER, output->framebuffer);
  g_gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_TEXTURE_2D, output->texture, 0);
  const bool complete =
      g_gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  if (complete) {
    // Schwarz statt Speichermuell, bis der erste echte Frame da ist.
    g_gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    g_gl.Clear(0x00004000 /* GL_COLOR_BUFFER_BIT */);
  } else {
    LOG_ERROR("media_kit_video: FBO %dx%d unvollstaendig", width, height);
  }
  g_gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
  g_gl.BindTexture(GL_TEXTURE_2D, 0);
  if (!complete) {
    return false;
  }
  output->width = width;
  output->height = height;
  LOG_INFO("media_kit_video: Puffer %dx%d fuer Textur %lld", width, height,
           static_cast<long long>(output->texture_id));
  return true;
}

// mpv-Render-Kontext und GL-Objekte eines Outputs freigeben. Voraussetzung:
// Aufrufer ist der einzige GL-Thread und haelt g_mutex. Gearbeitet wird im
// FENSTERKONTEXT - demselben, in dem auch gerendert wird (Begruendung am
// Frame-Callback).
void DestroyOutputGlLocked(VideoOutput* output) {
  if (output->render_context == nullptr && output->texture == 0) {
    return;
  }
  if (!flutter_libnx_egl_make_current()) {
    LOG_ERROR("media_kit_video: Abbau ohne bindbaren Kontext - GL-Objekte "
              "bleiben zurueck");
    return;
  }
  if (output->render_context != nullptr) {
    // Erst den Weckkanal kappen: Der Update-Callback haelt einen rohen
    // Zeiger auf das Output-Objekt.
    mpv_render_context_set_update_callback(output->render_context, nullptr,
                                           nullptr);
    mpv_render_context_free(output->render_context);
    output->render_context = nullptr;
  }
  if (output->texture != 0) {
    g_gl.DeleteFramebuffers(1, &output->framebuffer);
    g_gl.DeleteTextures(1, &output->texture);
    output->texture = 0;
    output->framebuffer = 0;
  }
}

// --- Kanal-Seite (Plattform-Thread) -----------------------------------------

const StdValue* MapGet(const StdValue& map, const char* key) {
  if (map.type != StdValue::Type::kMap) {
    return nullptr;
  }
  for (const auto& entry : map.as_map) {
    if (entry.first.type == StdValue::Type::kString &&
        entry.first.as_string == key) {
      return &entry.second;
    }
  }
  return nullptr;
}

// media_kit schickt Zahlen als Dezimalstrings, fehlende Werte als "null".
int64_t ParseIntString(const StdValue* value, int64_t fallback) {
  if (value == nullptr) {
    return fallback;
  }
  if (value->type == StdValue::Type::kInt) {
    return value->as_int;
  }
  if (value->type != StdValue::Type::kString || value->as_string == "null") {
    return fallback;
  }
  return strtoll(value->as_string.c_str(), nullptr, 10);
}

void Respond(FLUTTER_API_SYMBOL(FlutterEngine) engine,
             const FlutterPlatformMessage* message,
             const std::vector<uint8_t>& payload) {
  if (message->response_handle == nullptr) {
    return;
  }
  FlutterEngineSendPlatformMessageResponse(engine, message->response_handle,
                                           payload.data(), payload.size());
}

// VideoOutput.Resize an die Dart-Seite: setzt dort id (Textur) und rect.
void SendResize(FLUTTER_API_SYMBOL(FlutterEngine) engine,
                int64_t handle,
                int64_t texture_id,
                int width,
                int height) {
  StdValue rect = StdValue::Map();
  rect.as_map.emplace_back(StdValue::String("left"), StdValue::Int(0));
  rect.as_map.emplace_back(StdValue::String("top"), StdValue::Int(0));
  rect.as_map.emplace_back(StdValue::String("width"), StdValue::Int(width));
  rect.as_map.emplace_back(StdValue::String("height"), StdValue::Int(height));
  StdValue args = StdValue::Map();
  args.as_map.emplace_back(StdValue::String("handle"), StdValue::Int(handle));
  args.as_map.emplace_back(StdValue::String("id"), StdValue::Int(texture_id));
  args.as_map.emplace_back(StdValue::String("rect"), std::move(rect));

  // Ein Methodenaufruf im StandardMethodCodec ist Name und Argumente direkt
  // hintereinander kodiert - kein Umschlag.
  std::vector<uint8_t> payload;
  EncodeStdValue(StdValue::String("VideoOutput.Resize"), &payload);
  EncodeStdValue(args, &payload);

  FlutterPlatformMessage message = {};
  message.struct_size = sizeof(FlutterPlatformMessage);
  message.channel = kChannel;
  message.message = payload.data();
  message.message_size = payload.size();
  const FlutterEngineResult sent =
      FlutterEngineSendPlatformMessage(engine, &message);
  LOG_INFO("media_kit_video: Resize(id=%lld, %dx%d) -> Dart: %d",
           static_cast<long long>(texture_id), width, height,
           static_cast<int>(sent));
}

VideoOutput* FindByHandleLocked(int64_t handle) {
  for (auto& output : g_outputs) {
    if (output->handle == handle) {
      return output.get();
    }
  }
  return nullptr;
}

bool HandleCreate(FLUTTER_API_SYMBOL(FlutterEngine) engine,
                  const FlutterPlatformMessage* message,
                  const StdValue& args) {
  if (!flutter_libnx_egl_is_ready()) {
    // Software-Renderer aktiv: ehrlich ablehnen statt still schwarz bleiben.
    Respond(engine, message,
            EncodeStdError("no-gl", "Video braucht den GPU-Modus (EGL)"));
    return true;
  }

  const int64_t handle = ParseIntString(MapGet(args, "handle"), 0);
  if (handle == 0) {
    Respond(engine, message, EncodeStdError("bad-args", "handle fehlt"));
    return true;
  }
  int width = 0;
  int height = 0;
  if (const StdValue* config = MapGet(args, "configuration")) {
    width = static_cast<int>(ParseIntString(MapGet(*config, "width"), 0));
    height = static_cast<int>(ParseIntString(MapGet(*config, "height"), 0));
  }

  int64_t texture_id = 0;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    VideoOutput* existing = FindByHandleLocked(handle);
    if (existing != nullptr) {
      // Doppeltes Create (z. B. Hot-Restart-Pfad): vorhandene Textur melden.
      texture_id = existing->texture_id;
      width = existing->width;
      height = existing->height;
    } else {
      auto output = std::make_unique<VideoOutput>();
      output->handle = handle;
      output->texture_id = g_next_texture_id++;
      output->pending_width = width;
      output->pending_height = height;
      texture_id = output->texture_id;

      const FlutterEngineResult registered =
          FlutterEngineRegisterExternalTexture(engine, texture_id);
      if (registered != kSuccess) {
        Respond(engine, message,
                EncodeStdError("register", "RegisterExternalTexture scheitert"));
        return true;
      }
      g_outputs.push_back(std::move(output));
    }
  }

  // Render-Kontext eager auf dem Raster-Thread anlegen (Begruendung am
  // Task). Das Ergebnis wird nicht abgewartet - Dart braucht nur die id.
  FlutterEnginePostRenderThreadTask(engine, CreateOnRasterThread,
                                    reinterpret_cast<void*>(
                                        static_cast<intptr_t>(handle)));

  Respond(engine, message, EncodeStdSuccess(StdValue::Null()));
  // Die id muss zur Dart-Seite, sonst wartet deren create() fuer immer.
  // rect darf dabei noch 0x0 sein - media_kit setzt die echte Groesse per
  // SetSize nach, sobald die Videoparameter bekannt sind.
  SendResize(engine, handle, texture_id, width, height);
  return true;
}

bool HandleSetSize(FLUTTER_API_SYMBOL(FlutterEngine) engine,
                   const FlutterPlatformMessage* message,
                   const StdValue& args) {
  const int64_t handle = ParseIntString(MapGet(args, "handle"), 0);
  const int width = static_cast<int>(ParseIntString(MapGet(args, "width"), 0));
  const int height =
      static_cast<int>(ParseIntString(MapGet(args, "height"), 0));

  int64_t texture_id = 0;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    VideoOutput* output = FindByHandleLocked(handle);
    if (output == nullptr) {
      Respond(engine, message, EncodeStdSuccess(StdValue::Null()));
      return true;
    }
    texture_id = output->texture_id;
    if (width > 0 && height > 0) {
      output->pending_width = width;
      output->pending_height = height;
      // Der naechste Frame-Callback legt den Puffer neu an; die Komposition
      // dafuer stoesst die Pumpe an (frame_pending).
      output->frame_pending = true;
    }
  }

  Respond(engine, message, EncodeStdSuccess(StdValue::Null()));
  if (width > 0 && height > 0) {
    SendResize(engine, handle, texture_id, width, height);
  }
  return true;
}

// Fuer den Dispose-Umweg ueber den Raster-Thread.
struct DisposeTask {
  VideoOutput* output = nullptr;
  std::mutex mutex;
  std::condition_variable cv;
  bool done = false;
};

void DisposeOnRasterThread(void* user_data) {
  auto* task = static_cast<DisposeTask*>(user_data);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    DestroyOutputGlLocked(task->output);
  }
  std::lock_guard<std::mutex> lock(task->mutex);
  task->done = true;
  task->cv.notify_all();
}

bool HandleDispose(FLUTTER_API_SYMBOL(FlutterEngine) engine,
                   const FlutterPlatformMessage* message,
                   const StdValue& args) {
  const int64_t handle = ParseIntString(MapGet(args, "handle"), 0);

  VideoOutput* output = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    output = FindByHandleLocked(handle);
    if (output != nullptr) {
      // Ab jetzt liefert der Frame-Callback fuer diese Textur nichts mehr
      // und legt auch keinen Render-Kontext mehr an.
      output->dispose_requested = true;
    }
  }

  if (output != nullptr) {
    // Freigeben MUSS vor mpv_terminate_destroy der Dart-Seite geschehen -
    // und auf dem Raster-Thread, dem einzigen erlaubten GL-Thread. Der Task
    // liegt auf dem Heap: Laeuft er erst nach dem Zeitlimit an, schreibt er
    // sonst in einen laengst abgebauten Stackrahmen.
    auto* task = new DisposeTask();
    task->output = output;
    bool done = false;
    const FlutterEngineResult posted =
        FlutterEnginePostRenderThreadTask(engine, DisposeOnRasterThread, task);
    if (posted == kSuccess) {
      std::unique_lock<std::mutex> lock(task->mutex);
      done = task->cv.wait_for(lock, std::chrono::seconds(2),
                               [&] { return task->done; });
    }
    if (!done) {
      // Raster-Thread antwortet nicht (Engine im Abbau?): letzter Ausweg
      // ist der direkte Abbau - besser ein theoretisches Rennen als ein
      // mpv_terminate_destroy mit lebendem Render-Kontext.
      LOG_WARN("media_kit_video: Dispose-Task ohne Antwort - baue direkt ab");
      std::lock_guard<std::mutex> glock(g_mutex);
      DestroyOutputGlLocked(output);
    }

    int64_t texture_id = 0;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      texture_id = output->texture_id;
      if (done) {
        for (auto it = g_outputs.begin(); it != g_outputs.end(); ++it) {
          if (it->get() == output) {
            g_outputs.erase(it);
            break;
          }
        }
      }
      // Im Zeitlimit-Fall bleiben Output (als inerter Zombie, dispose_
      // requested haelt ihn aus allem heraus) und Task absichtlich stehen:
      // Der spaete Task darf keinen freigegebenen Speicher anfassen;
      // DestroyOutputGlLocked ist idempotent und wird bei ihm zum No-Op.
    }
    if (done) {
      delete task;
    }
    FlutterEngineUnregisterExternalTexture(engine, texture_id);
    LOG_INFO("media_kit_video: Textur %lld abgebaut",
             static_cast<long long>(texture_id));
  }
  Respond(engine, message, EncodeStdSuccess(StdValue::Null()));
  return true;
}

}  // namespace

}  // namespace flutter_libnx

// --- C-Schnittstelle fuer main.cpp ------------------------------------------

extern "C" bool flutter_libnx_handle_media_kit_video(
    FLUTTER_API_SYMBOL(FlutterEngine) engine,
    const FlutterPlatformMessage* message) {
  using namespace flutter_libnx;

  if (strcmp(message->channel, kChannel) != 0) {
    return false;
  }

  std::string method;
  StdValue args;
  if (!DecodeStdMethodCall(message->message, message->message_size, &method,
                           &args)) {
    LOG_WARN("media_kit_video: Methodenaufruf nicht dekodierbar");
    Respond(engine, message, EncodeStdError("codec", "nicht dekodierbar"));
    return true;
  }

  if (method == "VideoOutputManager.Create") {
    return HandleCreate(engine, message, args);
  }
  if (method == "VideoOutputManager.SetSize") {
    return HandleSetSize(engine, message, args);
  }
  if (method == "VideoOutputManager.Dispose") {
    return HandleDispose(engine, message, args);
  }
  LOG_WARN("media_kit_video: unbekannte Methode '%s'", method.c_str());
  // Leere Antwort -> MissingPluginException, ehrlich.
  if (message->response_handle != nullptr) {
    FlutterEngineSendPlatformMessageResponse(engine, message->response_handle,
                                             nullptr, 0);
  }
  return true;
}

// Der Textur-Frame-Callback der Engine - Raster-Thread, waehrend der
// Komposition. Hier passiert die eigentliche Arbeit: Render-Kontext lazy
// anlegen, Puffer (neu) anlegen, mpv rendern lassen, Texturnamen liefern.
// Skia hat vor diesem Aufruf flushAndSubmit() + resetContext() gemacht
// (embedder_external_texture_gl.cc) - der Kontextwechsel hier ist damit
// verabredet, nicht hinter Skias Ruecken.
extern "C" bool flutter_libnx_media_kit_texture_frame(
    void* /*user_data*/,
    int64_t texture_id,
    size_t /*width*/,
    size_t /*height*/,
    FlutterOpenGLTexture* texture_out) {
  using namespace flutter_libnx;
  std::lock_guard<std::mutex> lock(g_mutex);
  VideoOutput* output = nullptr;
  for (auto& candidate : g_outputs) {
    if (candidate->texture_id == texture_id) {
      output = candidate.get();
      break;
    }
  }
  if (output == nullptr || output->dispose_requested || output->create_failed) {
    return false;
  }

  const bool needs_create = output->render_context == nullptr;
  const bool needs_resize =
      output->pending_width > 0 && output->pending_height > 0 &&
      (output->pending_width != output->width ||
       output->pending_height != output->height);
  const bool wants_frame = output->frame_pending.exchange(false);

  if (needs_create || needs_resize || wants_frame) {
    // KEIN Kontextwechsel: Der Callback laeuft mitten in Skias Raster-
    // Frame, der Fensterkontext ist gebunden - und genau darin arbeitet
    // mpv. Die Engine hat davor resetContext(kAll_GrBackendState)
    // gerufen (embedder_external_texture_gl.cc), Zustandsaenderungen hier
    // sind also verabredet. Der Umweg ueber einen GETEILTEN Kontext war
    // die Ursache des schwarzen Bildes: nouveau auf diesem Port
    // propagiert Texturinhalte nicht zuverlaessig zwischen Kontexten -
    // die Pixelprobe sah im Schreibkontext ein weisses, volldeckendes
    // Bild, Skias Kontext sah schwarz.
    //
    // Der Kontext entsteht normalerweise schon eager per Task nach Create;
    // dieser Zweig ist der Rueckfall, falls der Task noch nicht lief.
    if (needs_create && !CreateRenderContextLocked(output)) {
      return false;
    }
    if (!g_gl.Load()) {
      output->create_failed = true;
      return false;
    }

    if (needs_resize) {
      if (!AllocateBuffer(output, output->pending_width,
                          output->pending_height)) {
        output->create_failed = true;
        return false;
      }
    }

    if (output->render_context != nullptr && output->width > 0) {
      const uint64_t flags = mpv_render_context_update(output->render_context);
      output->callback_count++;
      if (output->callback_count <= 5) {
        LOG_INFO("media_kit_video: Callback %u, update-flags=0x%llx",
                 output->callback_count,
                 static_cast<unsigned long long>(flags));
      }
      if ((flags & MPV_RENDER_UPDATE_FRAME) != 0) {
        mpv_opengl_fbo fbo = {};
        fbo.fbo = static_cast<int>(output->framebuffer);
        fbo.w = output->width;
        fbo.h = output->height;
        // flip_y=0: Auf Hardware entschieden - mit flip_y=1 stand das Bild
        // auf dem Kopf. Die Engine borgt die Textur zwar mit kTopLeft-
        // Origin (embedder_external_texture_gl.cc), aber mpvs "nicht
        // geflippte" FBO-Ausgabe liegt offenbar bereits zeilenrichtig fuer
        // diese Lesart.
        int flip_y = 0;
        mpv_render_param render_params[] = {
            {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
            {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        const int rc =
            mpv_render_context_render(output->render_context, render_params);
        if (rc < 0) {
          LOG_ERROR("media_kit_video: render: %s", mpv_error_string(rc));
        } else {
          output->render_count++;
          if (output->render_count <= 3 || output->render_count % 120 == 0) {
            LOG_INFO("media_kit_video: render #%u (%dx%d) ok",
                     output->render_count, output->width, output->height);
          }

          // Alpha erzwingen: Skia borgt die Textur als kPremul - schreibt
          // mpv Alpha=0, ist das Bild fuer Skia vollstaendig transparent
          // und der Seitenhintergrund (schwarz) gewinnt. Der Kanal wird
          // nach jedem Render auf 1 gesetzt; kostet einen Clear auf dem
          // Alphakanal und ist harmlos, falls mpv schon 1 schreibt.
          g_gl.BindFramebuffer(GL_FRAMEBUFFER, output->framebuffer);
          g_gl.Disable(0x0C11 /* GL_SCISSOR_TEST */);
          g_gl.ColorMask(0, 0, 0, 1);
          g_gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
          g_gl.Clear(0x00004000 /* GL_COLOR_BUFFER_BIT */);
          g_gl.ColorMask(1, 1, 1, 1);

          // DIAGNOSE: Was steht wirklich in der Textur? Ein Pixel aus der
          // Bildmitte, beim ersten und jedem 120. Frame. Beantwortet
          // Schwarz-vs-Alpha-vs-Sharing in einer Zeile.
          if (output->render_count == 1 || output->render_count % 120 == 0) {
            uint8_t px[4] = {0, 0, 0, 0};
            g_gl.ReadPixels(output->width / 2, output->height / 2, 1, 1,
                            GL_RGBA, GL_UNSIGNED_BYTE, px);
            LOG_INFO("media_kit_video: Pixel Mitte R=%u G=%u B=%u A=%u",
                     px[0], px[1], px[2], px[3]);
          }
          g_gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        // Ein Flush genuegt jetzt: Erzeuger und Verbraucher sind derselbe
        // Kontext auf demselben Thread, der Treiber ordnet die Kommandos.
        g_gl.Flush();
      }
    }
  }

  if (output->texture == 0 || output->width <= 0) {
    return false;
  }
  if (!output->delivered_logged) {
    output->delivered_logged = true;
    LOG_INFO("media_kit_video: liefere Textur %u (%dx%d) an Skia",
             output->texture, output->width, output->height);
  }
  texture_out->target = GL_TEXTURE_2D;
  texture_out->name = output->texture;
  texture_out->format = GL_RGBA8;
  texture_out->user_data = nullptr;
  texture_out->destruction_callback = nullptr;
  texture_out->width = static_cast<size_t>(output->width);
  texture_out->height = static_cast<size_t>(output->height);
  return true;
}

// Aus der Hauptschleife (Plattform-Thread): neue mpv-Frames als
// MarkExternalTextureFrameAvailable melden - das stoesst die Komposition an,
// in deren Frame-Callback dann gerendert wird.
extern "C" void flutter_libnx_media_kit_video_pump(
    FLUTTER_API_SYMBOL(FlutterEngine) engine) {
  using namespace flutter_libnx;
  if (engine == nullptr) {
    return;
  }
  // Kurze Sperre: nur IDs einsammeln, die Engine-Aufrufe danach. Das Flag
  // wird NICHT geloescht - das macht der Frame-Callback beim Rendern; bis
  // dahin haelt jede weitere Markierung die Komposition am Laufen.
  int64_t ready[8];
  int ready_count = 0;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto& output : g_outputs) {
      // pending_width > 0: Vor dem ersten SetSize gibt es nichts zu zeigen -
      // ohne die Bedingung wuerde jede Markierung eine Komposition anstossen,
      // die das 0x0-Widget gar nicht enthaelt, im 4-ms-Takt.
      if (output->frame_pending && output->pending_width > 0 &&
          !output->dispose_requested && !output->create_failed &&
          ready_count < 8) {
        ready[ready_count++] = output->texture_id;
      }
    }
  }
  for (int i = 0; i < ready_count; i++) {
    FlutterEngineMarkExternalTextureFrameAvailable(engine, ready[i]);
  }
}

// Nach FlutterEngineShutdown, vor flutter_libnx_egl_shutdown: Der Raster-
// Thread ist abgebaut, dieser Thread ist jetzt der einzige GL-Thread und
// darf die verbliebenen Render-Kontexte direkt freigeben.
extern "C" void flutter_libnx_media_kit_video_shutdown(void) {
  using namespace flutter_libnx;
  std::lock_guard<std::mutex> lock(g_mutex);
  for (auto& output : g_outputs) {
    DestroyOutputGlLocked(output.get());
  }
  g_outputs.clear();
}
