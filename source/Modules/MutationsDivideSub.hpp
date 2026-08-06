#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  This is a starter plug-in module, with all of the signals listed, but none set to do anything.
 */

#include <cmath>
#include <cstddef>   // for size_t
#include <fstream>
#include <iostream>

#include "../core/Avida.hpp"

template <typename AVIDA_T>
class MutationsDivideSub : public ModuleBase<AVIDA_T> {
private:
  using ModuleBase<AVIDA_T>::avida;

  double substitution_prob{0.0075};
  double mut_scale{1.0 / emp::Log2(1.0 - substitution_prob)};

  void SetSubstitutionProb(double probability) {
    if (!std::isfinite(probability) || probability < 0.0 || probability > 1.0) {
      emp::notify::Error(
        "mutations.substitution_prob must be between 0.0 and 1.0; received ",
        probability,
        "."
      );
    }

    substitution_prob = probability;
    if (substitution_prob > 0.0 && substitution_prob < 1.0) {
      mut_scale = 1.0 / emp::Log2(1.0 - substitution_prob);
    }
  }

public:
  MutationsDivideSub(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>(avida, "MutationsDivideSub", "Mutation",
        "Handle substitution mutations on birth.") { }
  ~MutationsDivideSub() { }

  void Serialize(emp::SerialPod & /* pod */) {
    // Nothing extra to serialize; everything should be in the SettingsManager
  }

  void RegisterSettings() {
    avida.AddSetting("mutations.substitution_prob",
      [this](){ return substitution_prob; },
      [this](double p){ SetSubstitutionProb(p); },
      "Per-site substitution probability", 'p');
  }

  // === Signal Listeners ===

  template <concepts::Organism ORG_T>
  void OnOffspringInit(ORG_T & org, [[maybe_unused]] ORG_T & parent) {
    if (substitution_prob == 0.0) return;

    auto & genome = org.GetGenome();
    emp::Random & random = avida.GetRandom();

    if (substitution_prob == 1.0) {
      for (size_t mut_pos = 0; mut_pos < genome.size(); ++mut_pos) {
        genome.RandomizeSite(random, mut_pos);
      }
      org.SetMutated();
      return;
    }

    size_t mut_pos = static_cast<size_t>(std::log2(random.GetDoubleNonZero()) * mut_scale);
    while (mut_pos < genome.size()) {
      genome.RandomizeSite(random, mut_pos);
      org.SetMutated();
      mut_pos += static_cast<size_t>(std::log2(random.GetDoubleNonZero()) * mut_scale) + 1;
    }
  }

};
