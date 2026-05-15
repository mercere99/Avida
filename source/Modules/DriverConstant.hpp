#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  Module the runs each organism at a consistent, even pace.
 */

#include <cstddef>    // for size_t
#include <iostream>

#include "../core/Avida.hpp"

template <typename AVIDA_T>
class DriverConstant : public ModuleBase<AVIDA_T> {
private:
  AVIDA_T & avida;

  size_t max_updates = 10000;                               // Update to end run
  std::filesystem::path ancestor_filename{"ancestor.org"};  // Ancestor genome filename

  // CPU Execution Management
  static constexpr int32_t cycles_per_org = 30; // Average cycles to execute per org per update.
  int64_t cycles_executed = 0;                      // How many CPU cycles have been run so far?

  void PrintStats(size_t ud) {
    std::cout << "UD:" << ud
              << "  PopSize:" << avida.GetNumOrgs()
              << "  Generation: " << avida.GetAveTrait("generation")
              << "  Genome0:[" << avida.GetFirstOrg().GetGenomeSequence() << "]"
              << std::endl;
  }

public:
  DriverConstant(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>("DriverConstant", "Execution",
        "Execute organisms in order based on metabolic rate (buffering callbacks).")
    , avida(avida) { }
  ~DriverConstant() { }

  void RegisterSettings() {
    avida.AddSetting("base.max_updates", max_updates, "Maximum number of updates to run", 'U');
    avida.AddSetting("base.ancestor_filename",
      [this](){ return ancestor_filename.string(); },
      [this](std::string s){ ancestor_filename = s; },
      "Filename for the initial ancestor.");
  }

  void RegisterCallbacks() {
    // Register callback for a cell to signal that it is ready to divide.
    avida.AddCallback("DivideCell", [this](size_t biota_id){ avida.DivideOrg(biota_id); });
  }

  // === Signal Listeners ===

  void OnStart() {
    std::println("Random seed = {}", avida.GetRandom().GetSeed());
    avida.Inject(avida.GetSettings().GetConfigDir() / ancestor_filename);
    PrintStats(0);  // Report initial state before any organisms run.
  }

  void OnUpdate(size_t update) {
    // Loop through organisms in order.
    size_t ud_cycles = 0;
    for (size_t org_id = 0; org_id < avida.GetBiotaSize(); ++org_id) {
      if (!avida.IsOccupied(org_id)) continue;
      avida.ProcessOrg(org_id, cycles_per_org);
      ud_cycles += cycles_per_org;
    }
    cycles_executed += ud_cycles;

    if (update % 100 == 0) std::println("ud_cycles = {}", ud_cycles);
  }

  void OnUpdateEnd(size_t update) {
    if (update % 100 == 0) PrintStats(update);
    if (update >= max_updates) avida.Exit();
  }
};
