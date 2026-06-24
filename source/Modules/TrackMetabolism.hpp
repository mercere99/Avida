#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  Track the "metabolic rate" of each organism, to know how many CPU cycles they should receive.
 * 
 *  Other modules may modify the metabolic rate by either adding to the metabolic_base or
 *  multiplying the metabolic_mult.  The final rate are these values multiplied together
 *  and then multiplied by the genome length (to make organisms size neutral.)
 */

#include <cstddef>    // for size_t
#include <iostream>

#include "../core/Avida.hpp"

template <typename AVIDA_T>
class TrackMetabolism : public ModuleBase<AVIDA_T> {
private:
  using ModuleBase<AVIDA_T>::avida;

public:
  TrackMetabolism(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>(avida, "TrackMetabolism", "Analysis",
        "Monitor the rate of CPU cycles that each organism should receive.") { }
  ~TrackMetabolism() { }

  void Serialize(emp::SerialPod &) { /* Nothing to save */  }

  // === Phenotypic Traits ===

  struct Phenotype {
    double metabolic_base = 1.0;
    double metabolic_mult = 1.0;
    double parent_bonus = 0.0;      // How much metabolic bonus did parent org earn?
    uint32_t gestation_cost = 0;    // How many CPU cycles to produce an offspring?
    uint32_t parent_gestation = 0;  // How much cost for parent to produce this offspring?

    double MetabolicBonus() const { return metabolic_base * metabolic_mult; }
    double MetabolicRate(size_t org_size) const {
      // Run at our own earned bonus, or HALF of what we inherited -- whichever is larger.  The
      // inherited floor halves each generation, so it decays unless the organism re-earns it.
      return std::max(MetabolicBonus(), parent_bonus / 2.0) * org_size;
    }
  };

  void RegisterTraits() {
    AVIDA_REGISTER_TRAIT(metabolic_base,   "Relative base speed of the virtual CPU for this organism.");
    AVIDA_REGISTER_TRAIT(metabolic_mult,   "Bonus speed multiple from tasks.");
    AVIDA_REGISTER_TRAIT(parent_bonus,     "Bonus received while producing this offspring.");
    AVIDA_REGISTER_TRAIT(gestation_cost,   "How many CPU cycles to produce an offspring?");
    AVIDA_REGISTER_TRAIT(parent_gestation, "CPU cycles parent used to produce this offspring?");
  }

  void RegisterSettings() {
  }

  void RegisterCallbacks() {
  }

  // === Signal Listeners ===

  void BeforeStart() {
    auto metabolic_fun = [](const concepts::Organism auto & org) {
      return org.GetPhenotype().MetabolicRate(org.GetGenome().size());
    };

    avida.AddOutputTrait("metabolism.csv", "Minimum Bonus", "parent_bonus:min");
    avida.AddOutputTrait("metabolism.csv", "Average Bonus", "parent_bonus:mean");
    avida.AddOutputTrait("metabolism.csv", "Maximum Bonus", "parent_bonus:max");
    avida.AddOutput("metabolism.csv", "Minimum Metabolic Rate",
      [&](){ return avida.GetBiota().CalcMinimum(metabolic_fun); });
    avida.AddOutput("metabolism.csv", "Average Metabolic Rate",
      [&](){ return avida.GetBiota().CalcAverage(metabolic_fun); });
    avida.AddOutput("metabolism.csv", "Maximum Metabolic Rate",
      [&](){ return avida.GetBiota().CalcMaximum(metabolic_fun); });
    avida.AddOutputTrait("metabolism.csv", "Minimum Gestation Cost", "parent_gestation:min");
    avida.AddOutputTrait("metabolism.csv", "Average Gestation Cost", "parent_gestation:mean");
    avida.AddOutputTrait("metabolism.csv", "Maximum Gestation Cost", "parent_gestation:max");
  }

  // Reset all phenotype traits on inject.
  template <concepts::Organism ORG_T>
  void OnInjectReady(ORG_T & org) {
    auto & phenotype = org.GetPhenotype();
    phenotype.metabolic_base   = 1.0;
    phenotype.metabolic_mult   = 1.0;
    phenotype.parent_bonus     = 0.0; // No parent info on inject.
    phenotype.gestation_cost   = 0;
    phenotype.parent_gestation = 0;   // No parent info on inject.
  }

  // A divide is symmetric fission into two fresh daughters: the new offspring (set up here) and
  // the dividing cell itself (reset in OnOffspringReady).  The offspring starts with the bonus its
  // parent earned this gestation as its starting floor (halved -- see MetabolicRate).
  template <concepts::Organism ORG_T>
  void OnOffspringInit(ORG_T & offspring, ORG_T & parent) {
    auto & phenotype = offspring.GetPhenotype();
    phenotype.metabolic_base = 1.0;
    phenotype.metabolic_mult = 1.0;
    phenotype.parent_bonus = parent.GetPhenotype().MetabolicBonus();  // Bonus the parent earned.
    phenotype.gestation_cost = 0;
    phenotype.parent_gestation = parent.GetPhenotype().gestation_cost;
  }

  // The other half of the fission: once the offspring has taken its copy of the earned bonus, the
  // parent also restarts as a fresh daughter -- it banks what it earned into parent_bonus and
  // resets its own rate, so it must re-earn its bonus from the same halved floor as the offspring.
  template <concepts::Organism ORG_T>
  void OnOffspringReady(ORG_T & /* offspring */, ORG_T & parent) {
    auto & p = parent.GetPhenotype();
    p.parent_bonus   = p.MetabolicBonus();  // Bank what it earned this gestation (the shared floor).
    p.metabolic_base = 1.0;
    p.metabolic_mult = 1.0;
  }
};

/**
 * DEVELOPER NOTES: What should this file look like in future AvidaScript mode?
 *  Track the "metabolic rate" of each organism, to know how many CPU cycles they should receive.
 * 
 *  Other modules may modify the metabolic rate by either adding to the metabolic_base or
 *  multiplying the metabolic_mult.  The final rate are these values multiplied together
 *  and then multiplied by the genome length (to make organisms size neutral.)

Module TrackMetabolism {
  desc: "Monitor the rate of CPU cycles that each organism should receive."
  type: Analysis

  trait metabolic_base : double(1.0) {
    desc: "Relative base speed of the virtual CPU for this organism."
  }

  trait metabolic_mult : double(1.0) {
    desc: "Bonus speed multiple from tasks."
  }

  trait parent_bonus : double(0.0) {
    desc:      "Bonus earned by parent while producing this offspring."
    offspring: metabolic_base * metabolic_mult // Inherit product of old base and mult
  }

  trait gestation_cost : uint32_t(0) {
    desc: "How many CPU cycles to produce an offspring?"
  }

  trait parent_gestation : uint32_t(0) {
    desc:      "CPU cycles parent used to produce this offspring?"
    offspring: gestation_cost
  }

  function MetabolicRate(org: Organism) : double {
    const cur_bonus:double = metabolic_base * metabolic_mult
    return max(parent_bonus, cur_bonus) * org.genome.size;
  }
}

 */