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
  size_t output_frequency = 100;  // Updates between rows (applies to all files for now).

  // Find the file with this name, creating it (with a leading "Update" column) if new.
  emp::DataOutput & GetFile(const emp::String & filename) {
    if (file_map.contains(filename)) return file_map[filename];

    emp::DataOutput & new_file = file_map[filename];
    new_file.SetFilename(filename);
    new_file.SetFilepath(avida.GetDataDir());
    new_file.AddColumn("Update", [this](){ return avida.GetUpdate(); });
    return new_file;
  }

public:
  OutputManager(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>(avida, "OutputManager", "IO",
        "Collect announced output columns into CSV data files.") {}
  ~OutputManager() { }

  void Serialize(emp::SerialPod & /* pod */) {
    // Columns are reset each run; nothing here needs saving.
  }

  void RegisterSettings() {
    avida.AddSetting("fileout.frequency", output_frequency,
      "Updates between output-file rows (0 = off).");
  }

  // === Signal Listeners ===

  // A module announced a column for `file`; route its text form into that file's CSV.
  void OnDeclareOutput(const emp::String & filename, const emp::String & output_name, auto fun) {
    // using result_t = std::remove_cvref_t<std::invoke_result_t<FUN_T>>;
    // if constexpr (std::is_arithmetic_v<result_t>) {
    //   auto num_fun = [fun](){ return static_cast<double>(fun()); };
    // }
    GetFile(filename).AddColumn(output_name, [fun](){ return emp::MakeString(fun()); });
  }

  // Write headers and the initial (update 0) row.
  void OnStart() {
    if (output_frequency == 0) return;
    for (auto & [_, output] : file_map) output.DoOutput();
  }

  void OnUpdateEnd(size_t update) {
    if (output_frequency == 0 || update % output_frequency != 0) return;
    for (auto & [_, output] : file_map) output.DoOutput();
  }

};
