#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  Output-interface module: collect columns announced via Avida::AddOutputColumn /
 *  AddTraitColumn (the OnOutputColumn signal) into CSV data files.  Each distinct file name
 *  becomes one CSV with an automatic leading "Update" column.  Rows are written at the start of
 *  a run, every `fileout.frequency` updates, and once more on the final update.
 *
 *  Other interfaces (e.g. a web view) can listen for the same OnOutputColumn signal and render
 *  the columns differently; producers stay unaware of which sinks are present.
 *
 *  Files and their columns are kept in announcement order (parallel vectors, not a map) so the
 *  output layout is deterministic and controlled by the order modules add their columns.
 */

#include <cstddef>   // for size_t

#include "emp/base/Ptr.hpp"
#include "emp/base/vector.hpp"
#include "emp/data/DataOutput.hpp"
#include "emp/tools/String.hpp"

#include "../core/Avida.hpp"

template <typename AVIDA_T>
class FileOutput : public ModuleBase<AVIDA_T> {
private:
  using ModuleBase<AVIDA_T>::avida;

  struct FileData {
    emp::DataOutput output;
    size_t last_update = emp::MAX_SIZE_T;  // Last update whose row was written (avoid duplicates).
  };

  // Parallel vectors keyed by position so files stay in first-announced order.  FileData is
  // heap-allocated because emp::DataOutput owns a stream and is not safely moved on vector growth.
  emp::vector<emp::String> file_names;
  emp::vector<emp::Ptr<FileData>> file_data;
  size_t output_frequency = 100;  // Updates between rows (applies to all files for now).

  // Find the file with this name, creating it (with a leading "Update" column) if new.
  FileData & GetFile(const emp::String & file) {
    for (size_t i = 0; i < file_names.size(); ++i) {
      if (file_names[i] == file) return *file_data[i];
    }
    emp::Ptr<FileData> fd = emp::NewPtr<FileData>();
    fd->output.SetFilename(file);
    fd->output.SetFilepath(avida.GetDataDir());
    fd->output.AddColumn("Update", [this](){ return avida.GetUpdate(); });
    file_names.push_back(file);
    file_data.push_back(fd);
    return *fd;
  }

  void WriteRow(FileData & fd) {
    fd.output.DoOutput();
    fd.last_update = avida.GetUpdate();
  }

public:
  FileOutput(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>(avida, "FileOutput", "IO",
        "Collect announced output columns into CSV data files.") {}
  ~FileOutput() { for (emp::Ptr<FileData> fd : file_data) fd.Delete(); }

  void Serialize(emp::SerialPod & /* pod */) {
    // Columns are re-announced each run (BeforeStart) and files reopened on first write; nothing
    // here needs saving.
  }

  void RegisterSettings() {
    avida.AddSetting("fileout.frequency", output_frequency,
      "Updates between output-file rows (0 = off).");
  }

  // === Signal Listeners ===

  // A module announced a column for `file`; route its text form into that file's CSV.
  void OnOutputColumn(const emp::String & file, const OutputColumn & col) {
    GetFile(file).output.AddColumn(col.name, col.as_text);
  }

  // Write headers and the initial (update 0) row.
  void OnStart() {
    for (emp::Ptr<FileData> fd : file_data) WriteRow(*fd);
  }

  void OnUpdateEnd(size_t update) {
    if (output_frequency == 0 || update % output_frequency != 0) return;
    for (emp::Ptr<FileData> fd : file_data) WriteRow(*fd);
  }

  // Guarantee the final update is captured even if it did not land on a frequency boundary.
  // Runs during Shutdown(), before the biota is cleared, so trait-scan columns are still valid.
  void BeforeExit() {
    for (emp::Ptr<FileData> fd : file_data) {
      if (fd->last_update != avida.GetUpdate()) WriteRow(*fd);
    }
  }
};
