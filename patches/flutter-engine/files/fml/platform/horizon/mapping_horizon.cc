// Copyright 2026 The flutter-libnx Authors.
//
// FileMapping für Horizon OS.
//
// Horizon bietet kein mmap. Statt einer Speicherabbildung wird die Datei
// vollständig in einen Heap-Puffer gelesen. Das ist semantisch nicht dasselbe:
//
//   * Der gesamte Inhalt liegt sofort im Speicher, nicht bedarfsweise
//     eingelagert. Bei großen Assets kostet das entsprechend RAM.
//   * Schreibbare Abbildungen lassen sich so nicht nachbilden, weil
//     Änderungen bei mmap in die Datei zurückfließen. Eine Anforderung mit
//     Protection::kWrite schlägt deshalb fehl und wird protokolliert, statt
//     stillschweigend eine abweichende Zusicherung zu liefern.
//
// Die Read-Execute-Variante ist auf Horizon ebenfalls nicht abbildbar. Für den
// Dart-AOT-Weg wird sie nicht gebraucht: Die Snapshot-Instruktionen werden in
// die .text der NRO gelinkt und über Symbolreferenzen übergeben.

#include "flutter/fml/mapping.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>

#include "flutter/fml/build_config.h"
#include "flutter/fml/eintr_wrapper.h"
#include "flutter/fml/logging.h"
#include "flutter/fml/unique_fd.h"

namespace fml {

namespace {

bool IsWritable(std::initializer_list<FileMapping::Protection> protection) {
  for (auto entry : protection) {
    if (entry == FileMapping::Protection::kWrite) {
      return true;
    }
  }
  return false;
}

}  // namespace

Mapping::Mapping() = default;

Mapping::~Mapping() = default;

FileMapping::FileMapping(const fml::UniqueFD& handle,
                         std::initializer_list<Protection> protection) {
  if (!handle.is_valid()) {
    return;
  }

  if (IsWritable(protection)) {
    FML_LOG(ERROR) << "Schreibbare FileMappings gibt es auf Horizon nicht: "
                      "ohne mmap fliessen Aenderungen nicht in die Datei "
                      "zurueck.";
    return;
  }

  struct stat stat_buffer = {};
  if (::fstat(handle.get(), &stat_buffer) != 0) {
    FML_LOG(ERROR) << "fstat fehlgeschlagen.";
    return;
  }

  if (stat_buffer.st_size == 0) {
    valid_ = true;
    return;
  }

  const size_t size = static_cast<size_t>(stat_buffer.st_size);
  auto* buffer = static_cast<uint8_t*>(std::malloc(size));
  if (buffer == nullptr) {
    FML_LOG(ERROR) << "Kein Speicher fuer FileMapping von " << size
                   << " Bytes.";
    return;
  }

  // Vom Anfang lesen: Der Deskriptor kann von einem vorherigen Zugriff
  // verstellt sein, und mmap wäre davon unabhängig gewesen.
  if (::lseek(handle.get(), 0, SEEK_SET) < 0) {
    FML_LOG(ERROR) << "lseek fehlgeschlagen.";
    std::free(buffer);
    return;
  }

  size_t total_read = 0;
  while (total_read < size) {
    const ssize_t bytes_read = FML_HANDLE_EINTR(
        ::read(handle.get(), buffer + total_read, size - total_read));
    if (bytes_read <= 0) {
      FML_LOG(ERROR) << "Datei unvollstaendig gelesen: " << total_read
                     << " von " << size << " Bytes.";
      std::free(buffer);
      return;
    }
    total_read += static_cast<size_t>(bytes_read);
  }

  mapping_ = buffer;
  size_ = size;
  valid_ = true;
}

FileMapping::~FileMapping() {
  if (mapping_ != nullptr) {
    std::free(mapping_);
  }
}

size_t FileMapping::GetSize() const {
  return size_;
}

const uint8_t* FileMapping::GetMapping() const {
  return mapping_;
}

bool FileMapping::IsDontNeedSafe() const {
  // Bei mmap darf der Bereich verworfen werden, weil er aus der Datei neu
  // eingelagert werden kann. Ein Heap-Puffer kann das nicht – wird er
  // verworfen, ist der Inhalt weg. Deshalb konservativ false.
  return false;
}

bool FileMapping::IsValid() const {
  return valid_;
}

}  // namespace fml
