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

  public:
    BasicModule(AVIDA_T & avida) : avida(avida) { }
    ~BasicModule() { }

    // === Signal Listeners ===

    bool BeforeUpdate(size_t old_update) {
      return true;
    }

    bool OnUpdate(size_t new_update) {
      return true;
    }

    template <concepts::Organism ORG_T>
    bool BeforeRepro(ORG_T & parent) {
      return true;
    }

    template <concepts::Organism ORG_T>
    bool OnOffspringReady(ORG_T & offspring, ORG_T & parent) {
      return true;
    }

    template <concepts::Organism ORG_T>
    bool OnInjectReady(ORG_T & inject_org) {
      return true;
    }

    template <concepts::Organism ORG_T>
    bool BeforePlacement(ORG_T & org) {
      return true;
    }

    template <concepts::Organism ORG_T>
    bool OnPlacement(ORG_T & org) {
      return true;
    }

    template <concepts::Organism ORG_T>
    bool BeforeMutate(ORG_T & org) {
      return true;
    }

    template <concepts::Organism ORG_T>
    bool OnMutate(ORG_T & org) {
      return true;
    }

    template <concepts::Organism ORG_T>
    bool BeforeDeath(ORG_T & org) {
      return true;
    }

    bool BeforeExit() {      
      return true;
    }
    
    bool OnHelp() {
      return true;
      
    }
  };


} // namespace avida
