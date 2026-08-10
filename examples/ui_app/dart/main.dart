// Erste Dart-Anwendung, die auf der Switch etwas zeichnen soll.
//
// Bewusst ohne das Flutter-Framework: nur dart:ui. Zwischen "die Engine
// startet die Isolate" und "eine Flutter-App laeuft" liegen sehr viele
// Schichten, und wenn dabei etwas schiefgeht, will man wissen, welche. Diese
// Datei prueft genau die unterste: Isolate startet, dart:ui ist gebunden,
// PlatformDispatcher liefert Frames, eine Szene erreicht den Software-Renderer
// und landet im Framebuffer.
//
// Wenn das steht, ist der Weg fuer runApp() frei - dann ist es nur noch mehr
// Dart-Code obendrauf.

import 'dart:ui';

// Etwas Bewegung, damit auf einem Foto der Konsole erkennbar ist, dass
// tatsaechlich fortlaufend gezeichnet wird und nicht ein einzelnes Bild
// stehenblieb.
int _frame = 0;

void _draw(Duration timestamp) {
  final FlutterView? view = PlatformDispatcher.instance.implicitView;
  if (view == null) {
    // Ohne Ausgabeflaeche gibt es nichts zu zeichnen. Das waere ein Fehler im
    // Embedder, nicht in dieser Datei.
    return;
  }

  final Size size = view.physicalSize;
  final SceneBuilder builder = SceneBuilder();
  final PictureRecorder recorder = PictureRecorder();
  final Canvas canvas = Canvas(recorder);

  // Hintergrund: ein Verlauf ueber die Zeit, damit jeder Frame anders aussieht.
  final double t = (_frame % 120) / 120.0;
  canvas.drawRect(
    Rect.fromLTWH(0, 0, size.width, size.height),
    Paint()..color = Color.fromARGB(255, (t * 60).toInt(), 20, 60),
  );

  // Ein wandernder Balken - dieselbe Anzeige wie im libnx-Beispiel, damit
  // sich beide unmittelbar vergleichen lassen.
  final double x = (size.width - 160) * t;
  canvas.drawRect(
    Rect.fromLTWH(x, size.height / 2 - 40, 160, 80),
    Paint()..color = const Color(0xFF4CC2FF),
  );

  // Text braucht FreeType und den Schriftmanager. Genau deshalb steht er hier:
  // Faellt dieser Teil aus, waehrend die Rechtecke erscheinen, ist die
  // Schriftkette das Problem und nicht das Rendern.
  final ParagraphBuilder paragraph = ParagraphBuilder(
    ParagraphStyle(textAlign: TextAlign.center, fontSize: 32),
  )
    ..pushStyle(TextStyle(color: const Color(0xFFFFFFFF)))
    ..addText('Flutter auf der Nintendo Switch\nFrame $_frame');
  final Paragraph built = paragraph.build()
    ..layout(ParagraphConstraints(width: size.width));
  canvas.drawParagraph(built, Offset(0, size.height / 2 + 80));

  builder.addPicture(Offset.zero, recorder.endRecording());
  view.render(builder.build());

  _frame++;
  PlatformDispatcher.instance.scheduleFrame();
}

void main() {
  // Ohne diesen ersten Anstoss ruft die Engine den Callback nie auf.
  PlatformDispatcher.instance.onBeginFrame = _draw;
  PlatformDispatcher.instance.scheduleFrame();
}
