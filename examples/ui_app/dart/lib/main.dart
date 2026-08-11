// Erste echte Flutter-Anwendung auf der Switch.
//
// Der Vorgänger dieser Datei kam bewusst ohne Framework aus und sprach nur
// dart:ui an – Szene bauen, rendern, fertig. Damit war die unterste Schicht
// geprüft: Isolate, PlatformDispatcher, Software-Renderer, Framebuffer.
//
// Hier kommt alles darüber dazu: `runApp`, der Widget-Baum, Layout, Material,
// und vor allem der Ticker, mit dem das Framework seine Frames anfordert. Wenn
// das läuft, unterscheidet sich eine Anwendung für diese Konsole in nichts mehr
// von einer für Android – abgesehen von der Eingabe, die als Nächstes kommt.

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

void main() {
  runApp(const SwitchDemoApp());
}

class SwitchDemoApp extends StatelessWidget {
  const SwitchDemoApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'flutter-libnx',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: const Color(0xFF4CC2FF)),
        useMaterial3: true,
      ),
      home: const HomePage(),
    );
  }
}

class HomePage extends StatefulWidget {
  const HomePage({super.key});

  @override
  State<HomePage> createState() => _HomePageState();
}

// Die Animation ist nicht Zierde, sondern Nachweis: Ein AnimationController
// hängt am Ticker des Frameworks, und der wiederum an den Frames, die die
// Engine anfordert. Steht das Bild still, ist die Kette unterbrochen – und
// zwar an einer anderen Stelle, als wenn gar nichts erschiene.
class _HomePageState extends State<HomePage>
    with SingleTickerProviderStateMixin {
  late final AnimationController _controller = AnimationController(
    duration: const Duration(seconds: 3),
    vsync: this,
  )..repeat(reverse: true);

  // Der erste Platform Channel des Ports. JSONMethodCodec statt des
  // voreingestellten StandardMethodCodec: Das Binärformat des Standards
  // müsste der Embedder erst nachbauen, JSON kann er mit Bordmitteln
  // beantworten. Die Gegenseite steht in examples/ui_app/source/main.cpp
  // (PlatformMessageCallback).
  static const MethodChannel _system =
      MethodChannel('flutter_libnx/system', JSONMethodCodec());

  int _frames = 0;
  int _taps = 0;
  int _second = 0;
  String _battery = 'nicht abgefragt';

  Future<void> _queryBattery() async {
    // Absichtlich mit sichtbarem Fehlerbild statt stillem Schlucken: Ob die
    // Antwort, ein PlatformException-Fehler aus dem Embedder oder eine
    // MissingPluginException kommt, unterscheidet drei verschiedene
    // Fehlerquellen im Kanal.
    try {
      final int? level = await _system.invokeMethod<int>('getBatteryLevel');
      setState(() => _battery = '$level %');
    } on PlatformException catch (e) {
      setState(() => _battery = 'Fehler: ${e.code}');
    } on MissingPluginException {
      setState(() => _battery = 'Kanal nicht implementiert');
    }
  }

  // Wer gerade den Fokus hat – oder ob überhaupt jemand.
  String _focusName() {
    final FocusNode? node = FocusManager.instance.primaryFocus;
    if (node == null) {
      return 'kein Fokus';
    }
    final String label = node.debugLabel ?? node.runtimeType.toString();
    return '$label (Kontext: ${node.context != null})';
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final ColorScheme colors = Theme.of(context).colorScheme;

    return Scaffold(
      backgroundColor: colors.surface,
      body: Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: <Widget>[
            Text(
              'Flutter auf der Nintendo Switch',
              style: Theme.of(context).textTheme.headlineMedium?.copyWith(
                    color: colors.primary,
                    fontWeight: FontWeight.bold,
                  ),
            ),
            const SizedBox(height: 12),
            Text(
              'runApp() – Widgets, Layout, Material, Ticker',
              style: Theme.of(context).textTheme.titleMedium,
            ),
            const SizedBox(height: 48),

            // Der wandernde Balken aus der dart:ui-Fassung, diesmal vom
            // Framework gelayoutet statt von Hand gezeichnet. Beide Bilder
            // lassen sich damit unmittelbar vergleichen.
            SizedBox(
              width: 900,
              height: 80,
              child: AnimatedBuilder(
                animation: _controller,
                builder: (BuildContext context, Widget? child) {
                  _frames++;
                  return Align(
                    alignment: Alignment(_controller.value * 2 - 1, 0),
                    child: child,
                  );
                },
                child: Container(
                  width: 160,
                  height: 80,
                  decoration: BoxDecoration(
                    color: colors.primary,
                    borderRadius: BorderRadius.circular(12),
                  ),
                ),
              ),
            ),
            const SizedBox(height: 48),

            // Ein paar Widgets, die je eine eigene Fähigkeit belegen:
            // Icons kommen aus einer Schriftart, der Fortschrittsbalken
            // zeichnet fortlaufend neu, die Karte wirft einen Schatten.
            Card(
              child: Padding(
                padding: const EdgeInsets.all(24),
                child: Row(
                  mainAxisSize: MainAxisSize.min,
                  children: <Widget>[
                    Icon(Icons.videogame_asset, size: 40, color: colors.primary),
                    const SizedBox(width: 20),
                    SizedBox(
                      width: 200,
                      child: LinearProgressIndicator(value: _controller.value),
                    ),
                    const SizedBox(width: 20),
                    Text('Frames: $_frames'),
                  ],
                ),
              ),
            ),
            const SizedBox(height: 40),

            // Der Prüfstein für die Eingabe. Ein FilledButton ist dafür besser
            // geeignet als ein blanker GestureDetector: Er durchläuft die
            // vollständige Kette aus Treffererkennung, Gestenerkennung,
            // Zustandswechsel und Neuzeichnen – wer nur auf kDown lauscht,
            // würde einen halb funktionierenden Eingabepfad nicht bemerken.
            // Zwei Ziele, damit der Fokus etwas zum Wandern hat. Die Umrandung
            // des fokussierten Knopfes zeichnet Flutter selbst – folgt sie dem
            // Steuerkreuz, funktioniert die Tastenzustellung samt Fokussystem.
            Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: <Widget>[
                FilledButton.icon(
                  // Ohne autofocus hat anfangs nichts den Fokus, und das
                  // Steuerkreuz liefe ins Leere.
                  autofocus: true,
                  onPressed: () => setState(() => _taps++),
                  icon: const Icon(Icons.touch_app),
                  label: Text(
                    _taps == 0 ? 'Berühren oder A' : 'Gedrückt: $_taps',
                    style: const TextStyle(fontSize: 22),
                  ),
                  style: FilledButton.styleFrom(
                    padding: const EdgeInsets.symmetric(
                      horizontal: 32,
                      vertical: 24,
                    ),
                  ),
                ),
                const SizedBox(width: 24),
                OutlinedButton.icon(
                  onPressed: () => setState(() => _second++),
                  icon: const Icon(Icons.refresh),
                  label: Text(
                    _second == 0 ? 'Zweites Ziel' : 'Zweites: $_second',
                    style: const TextStyle(fontSize: 22),
                  ),
                  style: OutlinedButton.styleFrom(
                    padding: const EdgeInsets.symmetric(
                      horizontal: 32,
                      vertical: 24,
                    ),
                  ),
                ),
                const SizedBox(width: 24),
                // Der Platform-Channel-Nachweis: Druck auf den Knopf läuft
                // einmal komplett Dart -> Embedder -> psm-Dienst -> Dart.
                OutlinedButton.icon(
                  onPressed: _queryBattery,
                  icon: const Icon(Icons.battery_full),
                  label: Text(
                    'Akku: $_battery',
                    style: const TextStyle(fontSize: 22),
                  ),
                  style: OutlinedButton.styleFrom(
                    padding: const EdgeInsets.symmetric(
                      horizontal: 32,
                      vertical: 24,
                    ),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 24),
            Text(
              'Steuerkreuz oder linker Stick bewegt den Fokus, A drückt',
              style: Theme.of(context).textTheme.bodyMedium,
            ),
            const SizedBox(height: 8),

            // Gegenprobe zur Tastenzustellung: Die Ereignisse erreichen das
            // Framework (der Embedder bekommt eine Antwort), werden aber von
            // niemandem beansprucht. Wenn hier "kein Fokus" steht, liegt es
            // nicht an der Zustellung, sondern daran, dass das Fokussystem
            // keinen Empfänger kennt.
            Text(
              'Fokus: ${_focusName()}',
              style: Theme.of(context).textTheme.bodySmall,
            ),
          ],
        ),
      ),
    );
  }
}
