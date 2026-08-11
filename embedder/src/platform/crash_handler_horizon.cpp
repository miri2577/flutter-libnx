// Absturzbericht für Horizon.
//
// Warum es das braucht: Ein Data Abort beendet die Anwendung sofort, und der
// Fehlerbildschirm der Konsole (`2162-0002`) benennt keine Ursache. Die
// Adressen, die er anzeigt, ließen sich gegen unsere ELF nicht auflösen – die
// dort genannte Startadresse ist nicht die Basis der NRO. Damit war er als
// Werkzeug wertlos.
//
// libnx sieht dafür einen eigenen Weg vor: `__libnx_exception_handler` ist
// schwach gebunden (`nx/source/runtime/init.c:33`) und wird bei einer
// Ausnahme mit dem vollständigen Registerzustand aufgerufen. Der Stack dafür
// kommt ebenfalls von libnx (`__nx_exception_stack`, 1 KB).
//
// Entscheidend ist die letzte Zeile des Berichts: Sie gibt die Laufzeitadresse
// einer bekannten Funktion aus. Zusammen mit ihrer Adresse in der ELF ergibt
// das die Modulbasis – und damit lässt sich jeder Wert aus dem Bericht in eine
// Quellzeile übersetzen:
//
//     Basis  = Laufzeitadresse - ELF-Adresse (aus `nm`)
//     Offset = PC - Basis
//     aarch64-none-elf-addr2line -f -C -e ui_app.elf 0x<Offset>
//
// Gedacht ist das für Fehler, die sich der Instrumentierung entziehen: Jede
// Logzeile verschiebt das Zeitverhalten, ein Ausnahmebehandler nicht.

#include <switch.h>

#include <cstdio>

#include "flutter_libnx/log.h"

extern "C" {

// libnx stellt für den Ausnahmebehandler einen eigenen Stack bereit – schwach
// gebunden und **1 KB groß** (`nx/source/runtime/init.c:28`). Das reicht für
// eine knappe Meldung, nicht aber für ein Dutzend formatierter Zeilen: Der
// Logger legt seinen Formatierpuffer auf dem Stack an, und ein Überlauf hier
// beendet den Prozess still – ausgerechnet in dem Moment, in dem der Bericht
// entstehen soll. Genau das ist beim ersten Touch-Absturz passiert: kein
// Bericht, obwohl der Behandler eingebunden war.
//
// 16 KB kosten nichts, solange sie nicht gebraucht werden.
alignas(16) u8 __nx_exception_stack[0x4000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

// Ohne diese Kennzeichnung entfernt der Linker die Funktion, weil sie im
// Programm nirgends aufgerufen wird – der Aufrufer sitzt in libnx' Assembler.
__attribute__((used)) void __libnx_exception_handler(ThreadExceptionDump* ctx) {
  if (ctx == nullptr) {
    LOG_ERROR("Ausnahme ohne Kontext");
    return;
  }

  // Die Art des Fehlers. Die Werte stehen in ThreadExceptionDesc; die
  // häufigsten sind Speicherzugriffsfehler (Data Abort) und ungültige
  // Instruktionen.
  const char* art = "unbekannt";
  switch (ctx->error_desc) {
    case ThreadExceptionDesc_InstructionAbort: art = "Instruction Abort"; break;
    case ThreadExceptionDesc_MisalignedPC:     art = "PC nicht ausgerichtet"; break;
    case ThreadExceptionDesc_MisalignedSP:     art = "SP nicht ausgerichtet"; break;
    case ThreadExceptionDesc_SError:           art = "SError"; break;
    case ThreadExceptionDesc_BadSVC:           art = "ungueltiger SVC"; break;
    case ThreadExceptionDesc_Trap:             art = "Trap"; break;
    case ThreadExceptionDesc_Other:            art = "Data Abort o.ae."; break;
    default: break;
  }

  LOG_ERROR("=== ABSTURZ: %s (error_desc=0x%x) ===", art, ctx->error_desc);
  LOG_ERROR("  PC = 0x%016lx", static_cast<unsigned long>(ctx->pc.x));
  LOG_ERROR("  LR = 0x%016lx", static_cast<unsigned long>(ctx->lr.x));
  LOG_ERROR("  SP = 0x%016lx", static_cast<unsigned long>(ctx->sp.x));
  LOG_ERROR("  FAR = 0x%016lx (angefasste Adresse)",
            static_cast<unsigned long>(ctx->far.x));

  // Die ersten Register reichen meist, um den Aufruf zu erkennen. Mehr würde
  // den knappen Ausnahme-Stack strapazieren.
  for (int i = 0; i < 8; i++) {
    LOG_ERROR("  X%d = 0x%016lx", i,
              static_cast<unsigned long>(ctx->cpu_gprs[i].x));
  }

  // Der Bezugspunkt für addr2line. Ohne ihn sind alle Adressen oben wertlos,
  // weil die Ladeadresse der NRO bei jedem Start eine andere ist.
  LOG_ERROR("  Bezugspunkt: __libnx_exception_handler laeuft auf 0x%016lx",
            reinterpret_cast<unsigned long>(&__libnx_exception_handler));
  LOG_ERROR("=== Ende des Berichts ===");
}

}  // extern "C"
