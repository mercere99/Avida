/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  Track the current generation of each organism in the population.
 */

#include <cstddef>   // for size_t
#include <iostream>

#include "../core/Avida.hpp"

template <typename AVIDA_T>
class TrackGeneration : public ModuleBase<AVIDA_T> {
private:
  AVIDA_T & avida;

public:
  TrackGeneration(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>("TrackGeneration", "Analysis", "Monitor lineage length.")
    , avida(avida) {}
  ~TrackGeneration() {}

  // === Phenotypic Traits ===

  struct Phenotype {
    size_t generation = 0;
  };

  bool RegisterTraits() {
    AVIDA_REGISTER_TRAIT(generation, "Number of offspring in chain since inject");
    return true;
  }

  // === Signal Listeners ===

  template <concepts::Organism ORG_T>
  bool OnOffspringReady([[maybe_unused]] ORG_T & offspring, [[maybe_unused]] ORG_T & parent) {
    offspring.GetPhenotype().generation = parent.GetPhenotype().generation + 1;
    return true;
  }

  bool BeforeExit() {
    std::cout << "MODULE RESULT: " << avida.GetAveTrait("generation") << std::endl;
    return true;
  }
};
