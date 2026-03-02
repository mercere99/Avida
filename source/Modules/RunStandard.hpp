/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
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
class RunStandard : public ModuleBase<AVIDA_T> {
private:
  AVIDA_T & avida;

  size_t last_update = 10000; // How many updated should the run go for?
  // size_t last_update = 1000; // How many updated should the run go for?

  // CPU Execution Management
  emp::UnorderedIndexMap speed_map;                 // Relative speed of each virtual machine.
  static constexpr int32_t ave_cycles_per_org = 30; // Total cycles to execute per org per update.
  static constexpr int32_t CPU_chunk_size = 10;     // Num cycles executed each time org is picked.
  int64_t cycles_executed = 0;                      // How many CPU cycles have been run so far?

public:
  RunStandard(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>("RunStandard", "Execution", "Execute organisms based on metabolic rate.")
    , avida(avida) { }
  ~RunStandard() { }

  // === Signal Listeners ===

  bool OnUpdateStart(size_t new_update) {
    // Test if this run should finish.
    if (new_update > last_update) {
      avida.Exit();
      return true;
    }

    // Execute all organisms for this update.
    const int32_t cycles = avida.GetNumOrgs() * ave_cycles_per_org;
    for (int32_t rounds = cycles / CPU_chunk_size; rounds; --rounds) {
      const size_t id = speed_map.Index(avida.GetRandom().GetDouble(speed_map.GetWeight()));
      avida.ProcessOrg(id, CPU_chunk_size);
    }
    cycles_executed += cycles;

    return true;
  }

  bool OnUpdateEnd(size_t old_update) {
    if (old_update % 100 == 0) {
      std::cout << "UD:" << old_update
                << "  PopSize:" << avida.GetNumOrgs()
                << "  Generation: " << avida.GetAveTrait("generation")
                << "  Genome0:[" << avida.GetFirstOrg().GetGenomeSequence() << "]"
                << std::endl;
    }
    return true;
  }

  template <concepts::Organism ORG_T>
  bool OnPlacement(ORG_T & org) {
    speed_map.Set(org.GetBiotaID(), org.GetMetabolicRate());
    return true;
  }

  template <concepts::Organism ORG_T>
  bool BeforeDeath(ORG_T & org) {
    speed_map.Set(org.GetBiotaID(), 0.0);  // Set old index speed to 0; don't shrink speed_map
    return true;
  }
};
