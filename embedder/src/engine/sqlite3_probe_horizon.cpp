// Rauchprobe für das statisch gelinkte sqlite3, ohne Dart dazwischen.
//
// Anlass: rezkonv_app kommt bis zum ersten Statement und erntet
// SQLITE_IOERR_CORRUPTFS (8458) - der Unix-VFS von sqlite und unser
// devoptab-Dateisystem vertragen sich bei irgendeiner Dateioperation nicht.
// Diese Probe fährt dieselbe Kette (open_v2, CREATE, INSERT, SELECT, close)
// auf C-Ebene und protokolliert jeden Schritt einzeln - damit die fehlerhafte
// Operation einen Namen bekommt, statt hinter Darts Exception zu stecken.
//
// Läuft einmal beim Start, vor der Engine. Nach der Fehlersuche wieder
// ausbauen oder hinter ein Flag legen.

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../../third_party/sqlite3/sqlite3.h"
#include "flutter_libnx/log.h"

namespace {

// Dieselben POSIX-Aufrufe, die sqlites Unix-VFS benutzt, einzeln geprueft.
// Wenn hier etwas abweicht, ist es kein sqlite-Problem, sondern devoptab
// (oder unser fstat-Wrap).
void RawIoProbe(void) {
  const char* raw = "/switch/flutter_apps/ui_app/probe.raw";
  remove(raw);
  const int fd = open(raw, O_RDWR | O_CREAT, 0644);
  if (fd < 0) {
    LOG_ERROR("Roh-I/O: open fehlgeschlagen (errno %d)", errno);
    return;
  }
  const char data[] = "0123456789ABCDEF";
  const ssize_t written = write(fd, data, 16);
  struct stat st = {};
  const int frc = fstat(fd, &st);
  const off_t seek4 = lseek(fd, 4, SEEK_SET);
  char buf[9] = {};
  const ssize_t got = read(fd, buf, 8);
  const off_t seek_far = lseek(fd, 100, SEEK_SET);
  char buf2[4];
  const ssize_t got_eof = read(fd, buf2, 4);
  LOG_INFO("Roh-I/O: write=%d fstat=%d size=%d seek4=%d read8=%d '%s' "
           "seek100=%d readEOF=%d",
           static_cast<int>(written), frc, static_cast<int>(st.st_size),
           static_cast<int>(seek4), static_cast<int>(got), buf,
           static_cast<int>(seek_far), static_cast<int>(got_eof));

  // Pfadbasiertes stat(): sqlites getFileMode benutzt es beim Anlegen des
  // Journals und meldet einen Fehlschlag irrefuehrend als IOERR_FSTAT.
  struct stat st_path = {};
  errno = 0;
  const int stat_rc = stat(raw, &st_path);
  LOG_INFO("Roh-I/O: stat('%s') = %d (errno %d, size %d, mode 0%o)", raw,
           stat_rc, errno, static_cast<int>(st_path.st_size),
           static_cast<unsigned>(st_path.st_mode));

  close(fd);
  remove(raw);
}

}  // namespace

extern "C" void flutter_libnx_sqlite3_probe(void) {
  // Dasselbe Regime wie im echten Pfad: unix-none (keine Sperren).
  sqlite3_vfs* none = sqlite3_vfs_find("unix-none");
  if (none != nullptr) {
    sqlite3_vfs_register(none, 1);
  }
  LOG_INFO("sqlite3-Probe: Version %s, VFS unix-none=%d", sqlite3_libversion(),
           none != nullptr ? 1 : 0);

  mkdir("/switch", 0755);
  mkdir("/switch/flutter_apps", 0755);
  mkdir("/switch/flutter_apps/ui_app", 0755);

  RawIoProbe();

  const char* path = "/switch/flutter_apps/ui_app/probe.db";
  remove(path);
  // Auch das Journal: Ein liegen gebliebenes Hot-Journal eines frueheren
  // (abgestuerzten) Laufs wuerde beim Oeffnen zurueckgespielt - gegen eine
  // inzwischen geloeschte/neue Datenbank ergibt das genau "database disk
  // image is malformed". Dasselbe gilt fuer die Datenbanken der Apps.
  remove("/switch/flutter_apps/ui_app/probe.db-journal");

  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(
      path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  LOG_INFO("sqlite3-Probe: open_v2 = %d (%s)", rc,
           db != nullptr ? sqlite3_errmsg(db) : "kein Handle");
  if (rc != SQLITE_OK || db == nullptr) {
    return;
  }

  // Erweiterte Fehlercodes benennen die konkrete Dateioperation
  // (SQLITE_IOERR_FSYNC, _TRUNCATE, _DELETE, ...), der nackte Code 10 nicht.
  sqlite3_extended_result_codes(db, 1);

  char* errmsg = nullptr;
  rc = sqlite3_exec(db, "CREATE TABLE t(x INTEGER);", nullptr, nullptr,
                    &errmsg);
  LOG_INFO("sqlite3-Probe: CREATE = %d / erweitert %d (%s)", rc,
           sqlite3_extended_errcode(db),
           errmsg != nullptr ? errmsg : "ok");
  sqlite3_free(errmsg);
  errmsg = nullptr;

  rc = sqlite3_exec(db, "INSERT INTO t VALUES(42);", nullptr, nullptr,
                    &errmsg);
  LOG_INFO("sqlite3-Probe: INSERT = %d (%s)", rc,
           errmsg != nullptr ? errmsg : "ok");
  sqlite3_free(errmsg);
  errmsg = nullptr;

  sqlite3_stmt* stmt = nullptr;
  rc = sqlite3_prepare_v2(db, "SELECT sum(x) FROM t;", -1, &stmt, nullptr);
  if (rc == SQLITE_OK) {
    rc = sqlite3_step(stmt);
    LOG_INFO("sqlite3-Probe: SELECT step = %d, sum = %d", rc,
             rc == SQLITE_ROW ? sqlite3_column_int(stmt, 0) : -1);
    sqlite3_finalize(stmt);
  } else {
    LOG_INFO("sqlite3-Probe: prepare = %d (%s)", rc, sqlite3_errmsg(db));
  }

  rc = sqlite3_close(db);
  LOG_INFO("sqlite3-Probe: close = %d", rc);
}
