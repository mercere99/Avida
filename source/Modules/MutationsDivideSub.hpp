/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  This is a starter plug-in module, with all of the signals listed, but none set to do anything.
 */

#include <cstddef>   // for size_t
#include <fstream>
#include <iostream>

#include "../core/Avida.hpp"

template <typename AVIDA_T>
class MutationsDivideSub : public ModuleBase<AVIDA_T> {
private:
  AVIDA_T & avida;

  double sub_prob{0.0075};
  double mut_scale{1.0 / emp::Log2(1.0 - sub_prob)};

public:
  MutationsDivideSub(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>("MutationsDivideSub", "Mutation", "Handle substitution mutations on birth."), avida(avida) { }
  ~MutationsDivideSub() { }

  void RegisterSettings() {
    avida.AddSetting("mutations.sub_prob",
      [this](){ return sub_prob; },
      [this](double p){ sub_prob = p; mut_scale = 1.0 / emp::Log2(1.0 - sub_prob); },
      "Per-site substitution probability", 'p');
  }

  // === Signal Listeners ===

  template <concepts::Organism ORG_T>
  void OnOffspringInit(ORG_T & org, [[maybe_unused]] ORG_T & parent) {
    auto & genome = org.GetGenome();
    emp::Random & random = avida.GetRandom();
    size_t mut_pos = static_cast<size_t>(std::log2(random.GetDoubleNonZero()) * mut_scale);
    while (mut_pos < genome.size()) {
      genome[mut_pos] = avida.GetInstSet().GetRandom(random);
      org.SetMutated();
      mut_pos += static_cast<size_t>(std::log2(random.GetDoubleNonZero()) * mut_scale) + 1;
    }
  }

};
