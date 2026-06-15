#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  Track the current number of offspring produced by each organism.
 */

#include <cstddef>   // for size_t

#include "../core/Avida.hpp"

AVIDA_DEFINE_MODULE(TrackOffspringCount, "Analysis", "Monitor number of offspring produced.",
  void Serialize(emp::SerialPod & /* pod */) { } // Nothing extra to serialize

  // === Phenotypic Traits ===
  struct Phenotype {
    size_t offspring_count = 0;
  };

  void RegisterTraits() {
    AVIDA_REGISTER_TRAIT(offspring_count, "Number of offspring in chain since inject");
  }

  // === Signal Listeners ===
  template <concepts::Organism ORG_T>
  void OnOffspringReady(ORG_T & /*offspring*/, ORG_T & parent) {
    ++parent.GetPhenotype().offspring_count;
  }
);
