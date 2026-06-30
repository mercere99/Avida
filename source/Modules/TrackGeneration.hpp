#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  Track the current generation of each organism in the population.
 */

#include <cstddef>   // for size_t
#include <iostream>

#include "../core/Avida.hpp"

AVIDA_DEFINE_MODULE(TrackGeneration, "Analysis", "Monitor lineage length.",
  void Serialize(emp::SerialPod & /* pod */) { } // Nothing extra to serialize

  struct Phenotype {
    size_t generation = 0;
  };

  void RegisterTraits() {
    AVIDA_REGISTER_TRAIT(generation, "Number of offspring in chain since inject");
  }

  // === Signal Listeners ===

  void BeforeStart() {
    avida.AddOutputTrait("stats.csv", "Average Generation", "generation:mean");
    avida.AddOutput(">", "Generation", [this](){ return std::format("{:.2f}", avida.CalcTraitAve("generation")); });
  }

  template <concepts::Organism ORG_T>
  void OnOffspringReady(ORG_T & offspring, ORG_T & parent) {
    offspring.GetPhenotype().generation = parent.GetPhenotype().generation + 1;
  }

  template <concepts::Organism ORG_T>
  void OnInjectReady(ORG_T & inject_org) {
    inject_org.GetPhenotype().generation = 0;
  }
);
