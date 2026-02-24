/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  This module configures and runs a standard avida population.
 *  - Well-mixed population
 *  - Optional population cap
 */

#include <cstddef>   // for size_t
#include <iostream>

#include "../core/concepts.hpp"

namespace avida {

  template <typename AVIDA_T>
  class PopWellMixed {
  private:
    AVIDA_T & avida;

    int32_t pop_cap = 10000;    // Population size limit (default: 10,000 orgs)

  public:
    PopWellMixed(AVIDA_T & avida) : avida(avida) { }
    ~PopWellMixed() { }

    constexpr static std::string GetName() { return "PopWellMixed"; }
    constexpr static std::string GetType() { return "Population Manager"; }

    // === Signal Listeners ===

    // If we have a population cap, delete organisms rather than let population get overfull.
    template <concepts::Organism ORG_T>
    bool BeforePlacement([[maybe_unused]] ORG_T & org) {
      // See if we must delete an organism to make room for the new one.
      if (avida.GetNumOrgs() == pop_cap) avida.DeleteOrg();

      return true;
    }

  };


} // namespace avida
