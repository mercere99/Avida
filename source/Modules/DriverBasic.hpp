/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  This module configures and runs a standard avida population.
 *  - A scheduler that allocates based on metabolic rate
 *  - A limit on the number of updates run
 */

#include <cstddef>   // for size_t
#include <iostream>

#include "../core/Avida.hpp"

template <typename AVIDA_T>
class DriverBasic : public ModuleBase<AVIDA_T> {
private:
  AVIDA_T & avida;

  size_t max_updates = 10000; // How many updates should the run go for?

  // CPU Execution Management
  emp::UnorderedIndexMap speed_map;                 // Relative speed of each virtual machine.
  static constexpr int32_t ave_cycles_per_org = 30; // Total cycles to execute per org per update.
  static constexpr int32_t CPU_chunk_size = 15;     // Num cycles executed each time org is picked.
  int64_t cycles_executed = 0;                      // How many CPU cycles have been run so far?

public:
  DriverBasic(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>("DriverBasic", "Execution", "Execute organisms based on metabolic rate.")
    , avida(avida) { }
  ~DriverBasic() { }

  // === Phenotypic Traits ===

  struct Phenotype {
    double metabolic_base = 1.0;
    double metabolic_mult = 1.0;
    double MetabolicBonus() { return metabolic_base * metabolic_mult; }
  };

  template <concepts::Organism ORG_T>
  static double CalcMetabolicRate(ORG_T & org) {
    return org.GetPhenotype().MetabolicBonus() * org.GetGenome().size();
  }

  void RegisterTraits() {
    AVIDA_REGISTER_TRAIT(metabolic_base, "Relative base speed of the virtual CPU for this organism.");
    AVIDA_REGISTER_TRAIT(metabolic_mult, "Bonus speed multiple from tasks.");
  }

  void RegisterSettings() {
    avida.AddSetting("base.max_updates", max_updates, "Maximum number of updates to run", 'U');
  }

  void RegisterCallbacks() {
    // Register callback for a cell to signal that it is ready to divide.
    avida.AddCallback("DivideCell", [this](size_t biota_id){ avida.DivideOrg(biota_id); });
  }

  // === Signal Listeners ===

  void OnStart() {
    std::println("Random seed = {}", avida.GetRandom().GetSeed());

    // Inject a single individual of the default ancestor.
    avida.Inject("../config/ancestor.org");
  }

  void OnUpdateStart(size_t new_update) {
    // Test if this run should finish.
    if (new_update > max_updates) {
      avida.Exit();
    }

    // Execute all organisms for this update.
    const int32_t total_cycles = avida.GetNumOrgs() * ave_cycles_per_org;
    for (int32_t rounds = total_cycles / CPU_chunk_size; rounds; --rounds) {
      const size_t id = speed_map.Index(avida.GetRandom().GetDouble(speed_map.GetWeight()));
      avida.ProcessOrg(id, CPU_chunk_size);
    }
    cycles_executed += total_cycles;
  }

  void OnUpdateEnd(size_t old_update) {
    if (old_update % 100 == 0) {
      std::cout << "UD:" << old_update
                << "  PopSize:" << avida.GetNumOrgs()
                << "  Generation: " << avida.GetAveTrait("generation")
                << "  Genome0:[" << avida.GetFirstOrg().GetGenomeSequence() << "]"
                << std::endl;
    }
  }

  template <concepts::Organism ORG_T>
  void OnPlacement(ORG_T & org) {
    // Lock in metabolic rate as organism speed.
    speed_map.Set(org.GetBiotaID(), CalcMetabolicRate(org));
  }

  template <concepts::Organism ORG_T>
  void BeforeDeath(ORG_T & org) {
    speed_map.Set(org.GetBiotaID(), 0.0);  // Set old index speed to 0; don't shrink speed_map
  }
};
