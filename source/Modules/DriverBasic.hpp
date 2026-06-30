#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  This module configures and runs a standard avida population.
 *  - A scheduler that allocates based on metabolic rate
 *  - A limit on the number of updates run
 */

#include <cstddef>    // for size_t
#include <iostream>

#include "../core/Avida.hpp"

#include "emp/datastructs/UnorderedIndexMap.hpp"

template <typename AVIDA_T>
class DriverBasic : public ModuleBase<AVIDA_T> {
private:
  using ModuleBase<AVIDA_T>::avida;

  size_t max_updates = 10000;         // Update to end run
  int32_t ave_cycles_per_org = 30;    // Average cycles to execute per org per update.
  int32_t CPU_chunk_size = 15;        // Num cycles executed each time org is picked.
  std::filesystem::path ancestor_filename{"ancestor.org"};  // Ancestor genome filename

  // CPU Execution Management
  emp::UnorderedIndexMap speed_map;                 // Relative speed of each virtual machine.
  int64_t cycles_executed = 0;                      // How many CPU cycles have been run so far?

public:
  DriverBasic(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>(avida, "DriverBasic", "Execution",
        "Execute organisms based on metabolic rate.") { }
  ~DriverBasic() { }

  void Serialize(emp::SerialPod & pod) {
    // All settings in the SettingsManager are automatically synced.
    pod(speed_map, cycles_executed);
  }

  // === Phenotypic Traits ===

  void RegisterSettings() {
    avida.AddSetting("base.max_updates", max_updates, "Maximum number of updates to run", 'U');
    avida.AddSetting("base.ave_cycles_per_org", ave_cycles_per_org, "Average number pf CPU cycles each update");
    avida.AddSetting("base.CPU_chunk_size", CPU_chunk_size, "Number of CPU cycles to execute in each chunk");
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
  }

  void OnUpdate(size_t /*update*/) {
    // Execute all organisms for this update.
    const int32_t total_cycles = avida.GetNumOrgs() * ave_cycles_per_org;
    for (int32_t rounds = total_cycles / CPU_chunk_size; rounds; --rounds) {
      const size_t id = speed_map.Index(avida.GetRandom().GetDouble(speed_map.GetWeight()));
      avida.ProcessOrg(id, CPU_chunk_size);
    }
    cycles_executed += total_cycles;
  }

  void OnUpdateEnd(size_t update) {
    if (update >= max_updates) avida.Exit();
  }

  template <concepts::Organism ORG_T>
  void OnPlacement(ORG_T & org) {
    // Lock in metabolic rate as organism speed.
    const double rate = org.GetPhenotype().MetabolicRate(org.GetGenome().size());
    speed_map.Set(org.GetBiotaID(), rate);
  }

  template <concepts::Organism ORG_T>
  void BeforeDeath(ORG_T & org) {
    speed_map.Set(org.GetBiotaID(), 0.0);  // Set old index speed to 0; don't shrink speed_map
  }
};
