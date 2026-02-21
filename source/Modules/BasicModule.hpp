/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  This module configures and runs a standard avida population.
 *  - One well-mixed population
 */

#include <cstddef>   // for size_t
#include <fstream>
#include <iostream>

#include "../core/Avida.hpp"
#include "../core/concepts.hpp"

namespace avida {

  template <typename AVIDA_T>
  class BasicModule {
  private:
    AVIDA_T & avida;

    int32_t pop_cap = 10000;    // Population size limit (default: 10,000 orgs)
    size_t last_update = 10000; // How many updated should the run go for?

    // CPU Execution Management
    emp::UnorderedIndexMap speed_map;                 // Relative speed of each virtual machine.
    static constexpr int32_t ave_cycles_per_org = 30; // Total cycles to execute per org per update.
    static constexpr int32_t CPU_chunk_size = 10;     // Num cycles executed each time org is picked.
    int64_t cycles_executed = 0;                      // How many CPU cycles have been run so far?

  public:
    BasicModule(AVIDA_T & avida) : avida(avida) { }
    ~BasicModule() { }

    // === Signal Listeners ===

    bool OnUpdateStart(size_t new_update) {
      // Test if this run should finish.
      if (new_update > last_update) {
        avida.Exit();
        exit(0);
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
                  << "  Generation: " << avida.GetAveGeneration()
                  << "  Genome0:[" << avida.GetFirstOrg().GetGenomeSequence() << "]"
                  << std::endl;
      }
      return true;
    }

    template <concepts::Organism ORG_T>
    bool BeforeRepro([[maybe_unused]] ORG_T & parent) {
      return true;
    }

    template <concepts::Organism ORG_T>
    bool OnOffspringReady([[maybe_unused]] ORG_T & offspring, [[maybe_unused]] ORG_T & parent) {
      return true;
    }

    template <concepts::Organism ORG_T>
    bool OnInjectReady([[maybe_unused]] ORG_T & inject_org) {
      return true;
    }

    template <concepts::Organism ORG_T>
    bool BeforePlacement([[maybe_unused]] ORG_T & org) {
      // See if we must delete an organism to make room for the new one.
      if (avida.GetNumOrgs() == pop_cap) avida.DeleteOrg();

      return true;
    }

    template <concepts::Organism ORG_T>
    bool OnPlacement(ORG_T & org) {
      speed_map.Set(org.GetPosition(), org.GetMetabolicRate());
      return true;
    }

    template <concepts::Organism ORG_T>
    bool BeforeMutate([[maybe_unused]] ORG_T & org) {
      return true;
    }

    template <concepts::Organism ORG_T>
    bool OnMutate([[maybe_unused]] ORG_T & org) {
      return true;
    }

    template <concepts::Organism ORG_T>
    bool BeforeDeath(ORG_T & org) {
      speed_map.Set(org.GetPosition(), 0.0);  // Set old index speed to 0; don't shrink speed_map
      return true;
    }

    bool BeforeExit() {      
      std::cout << "Final Pop Size = " << avida.GetNumOrgs() << std::endl;
//      std::cout << "Total Cycles = "   << cycles_executed << std::endl;
      return true;
    }
    
    bool OnHelp() {
      return true;
      
    }
  };


} // namespace avida
