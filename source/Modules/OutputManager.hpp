#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  Output-interface module:
 *  Collect functions announced via the RegisterOutput signal into CSV data files.
 *  Each distinct filename becomes one CSV with an automatic leading "Update" column.
 *  Rows are written every `fileout.frequency` updates.
 */

#include <cstddef>        // for size_t
#include <unordered_map>

#include "emp/base/Ptr.hpp"
#include "emp/base/vector.hpp"
#include "emp/data/DataOutput.hpp"
#include "emp/tools/String.hpp"

#include "../core/Avida.hpp"

template <typename AVIDA_T>
class OutputManager : public ModuleBase<AVIDA_T> {
private:
  using ModuleBase<AVIDA_T>::avida;

  std::unordered_map<emp::String, emp::DataOutput> file_map;
  emp::DataOutput std_out;          // Output that should go to the terminal.
  size_t file_frequency = 100;      // Updates between rows (applies to all files for now).
  size_t terminal_frequency = 100;  // Updates between outputs to the terminal.
  emp::String prev_filename = "";   // Track previous filename to simplify continued use of it.

  // Find the file with this name, creating it (with a leading "Update" column) if new.
  emp::DataOutput & GetFile(const emp::String & filename) {
    if (file_map.contains(filename)) return file_map[filename];

    emp::DataOutput & new_file = file_map[filename];
    new_file.SetFilename(filename);
    new_file.SetFilepath(avida.GetDataDir());
    new_file.AddColumn("Update", [this](){ return avida.GetUpdate(); });
    return new_file;
  }

  void DoFileOutputs() {
    for (auto & [_, output] : file_map) output.DoOutput();
  }

  void DoTerminalOutputs() {
    std::cout << ">>> ";
    std_out.DoTerminalOutput();
  }

public:
  OutputManager(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>(avida, "OutputManager", "IO",
        "Collect announced output columns into CSV data files.")
  {
    // At minumum, print current update to terminal.
    std_out.AddColumn("Update", [&avida](){ return avida.GetUpdate(); });
  }
  ~OutputManager() { }

  void Serialize(emp::SerialPod & /* pod */) {
    // Columns are reset each run; nothing here needs saving.
  }

  void RegisterSettings() {
    avida.AddSetting("outputs.file_frequency", file_frequency,
      "Updates between output-file rows (0 = off).");
    avida.AddSetting("outputs.terminal_frequency", terminal_frequency,
      "Updates between terminal outputs (0 = off).");
  }

  // === Signal Listeners ===

  // A module announced a column for `file`; route its text form into that file's CSV.
  // A filename of '^' indicates the previous file.
  // A filename of '>' indicates output to standard out.
  void OnDeclareOutput(emp::String filename, const emp::String & output_name, auto fun) {
    auto filename_chars = emp::AlphanumericCharSet() + emp::CharSet("/._-");

    if (filename == "^") filename = prev_filename;
    prev_filename = filename;

      // If this is a regular filename, use it.
    if (filename_chars.Has(filename)) {
      GetFile(filename).AddColumn(output_name, [fun](){ return emp::MakeString(fun()); });
    }
    else if (filename == ">") {
      std_out.AddColumn(output_name, [fun](){ return emp::MakeString(fun()); });
    }
    else {
      emp::notify::Error("Unknown filename for data output ", filename.AsLiteral(), ".");
    }
  }

  // Write headers and the initial (update 0) row.
  void OnStart() {
    if (file_frequency != 0) DoFileOutputs();
    if (terminal_frequency != 0) DoTerminalOutputs();
  }

  void OnUpdateEnd(size_t update) {
    if (file_frequency > 0 && update % file_frequency == 0) DoFileOutputs();
    if (terminal_frequency > 0 && update % terminal_frequency == 0) DoTerminalOutputs();
  }

};
