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
    size_t last_update = 1000; // How many updated should the run go for?

  public:
    BasicModule(AVIDA_T & avida) : avida(avida) { }
    ~BasicModule() { }

    // === Signal Listeners ===

    bool OnUpdateStart(size_t new_update) {
      if (new_update > last_update) {
        avida.Exit();
        exit(0);
      }
      return true;
    }

    bool OnUpdateEnd([[maybe_unused]] size_t old_update) {
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
    bool OnPlacement([[maybe_unused]] ORG_T & org) {
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
    bool BeforeDeath([[maybe_unused]] ORG_T & org) {
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
