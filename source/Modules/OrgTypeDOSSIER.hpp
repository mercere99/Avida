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
#include "../core/Genome.hpp"

template <typename AVIDA_T>
class OrgTypeDOSSIER : public ModuleBase<AVIDA_T> {
private:
  using this_t = OrgTypeDOSSIER<AVIDA_T>;
  AVIDA_T & avida;

  size_t genome_length = 100;
  double target_value = 100.0;
  size_t starting_count = 1000;
  double mut_prob = 0.01;
  double mut_size = 1.0;

  // internal tool for faster location of mutated positions.
  double mut_scale{1.0 / emp::Log2(1.0 - mut_prob)};

  GenomeManager<double> genome_manager;

  enum class Landscape { // Deceptive? Diverse? Ordered?
    BASE,                // 0          0        0
    STRUCTURE,           // 0          0        1
    MULTIPATH,           // 0          1        0
    STRUCTURE_MULTIPATH, // 0          1        1
    VALLEYS,             // 1          0        0
    STRUCTURE_VALLEYS,   // 1          0        1
    MULTIPATH_VALLEYS,   // 1          1        0
    FULL,                // 1          1        1
    ERROR
  };

  Landscape landscape = Landscape::BASE;

  [[nodiscard]] static emp::String ToName(Landscape landscape) {
    switch (landscape) {
    case Landscape::BASE: return "base";
    case Landscape::STRUCTURE: return "structure";
    case Landscape::MULTIPATH: return "multipath";
    case Landscape::STRUCTURE_MULTIPATH: return "structure,multipath";
    case Landscape::VALLEYS: return "valleys";
    case Landscape::STRUCTURE_VALLEYS: return "structure,valleys";
    case Landscape::MULTIPATH_VALLEYS: return "multipath,valleys";
    case Landscape::FULL: return "full";
    case Landscape::ERROR: return "ERROR";    
    }
  }

  [[nodiscard]] static Landscape ToLandscape(emp::String name) {
    if (name.IsNumber()) {
      const int id = name.ConvertTo<int>();
      emp_always_assert(id > 0 && id < static_cast<int>(Landscape::ERROR), "invalid landscape", id);
      return static_cast<Landscape>(id);
    }
    name.SetLower();
    bool has_structure = name.contains("structure") || name == "full";
    bool has_multipath = name.contains("multipath") || name == "full";
    bool has_valleys = name.contains("valleys") || name == "full";
    if (!has_structure && !has_multipath && !has_valleys) return Landscape::BASE;
    if (has_structure  && !has_multipath && !has_valleys) return Landscape::STRUCTURE;
    if (!has_structure && has_multipath  && !has_valleys) return Landscape::MULTIPATH;
    if (has_structure  && has_multipath  && !has_valleys) return Landscape::STRUCTURE_MULTIPATH;
    if (!has_structure && !has_multipath && has_valleys) return Landscape::VALLEYS;
    if (has_structure  && !has_multipath && has_valleys) return Landscape::STRUCTURE_VALLEYS;
    if (!has_structure && has_multipath  && has_valleys) return Landscape::MULTIPATH_VALLEYS;
    if (has_structure  && has_multipath  && has_valleys) return Landscape::FULL;
    return Landscape::ERROR;
  }

  template <concepts::Organism ORG_T>
  void EvalBasic(ORG_T & org) {
    // Trait values are directly set as genome values in "base".
    emp::vector<double> & trait_values = org.GetPhenotype().trait_values;
    trait_values = org.GetGenome().Values();
  }

  template <concepts::Organism ORG_T>
  void EvalStructured(ORG_T & org) {
    const auto & genome = org.GetGenome();
    emp::vector<double> & trait_values = org.GetPhenotype().trait_values;
    trait_values.resize(genome.size());

    auto cut = std::is_sorted_until(genome.begin(), genome.end(), std::greater_equal<double>{});
    size_t cut_pos = cut - genome.begin();
    std::copy(genome.begin(), cut, trait_values.begin());
    std::fill(trait_values.begin() + cut_pos, trait_values.end(), 0.0);
  }

  template <concepts::Organism ORG_T>
  void EvalMultipath(ORG_T & org) {
    const auto & genome = org.GetGenome();
    emp::vector<double> & trait_values = org.GetPhenotype().trait_values;
    trait_values.assign(genome.size(), 0.0);
    size_t peak = genome.FindMaxID();
    trait_values[peak] = genome[peak];
  }

  template <concepts::Organism ORG_T>
  void EvalStructuredMultipath(ORG_T & org) {
    const auto & genome = org.GetGenome();
    emp::vector<double> & trait_values = org.GetPhenotype().trait_values;
    trait_values.resize(genome.size());

    size_t peak = genome.FindMaxID();
    auto peak_it = genome.begin() + peak;
    auto cut = std::is_sorted_until(peak_it, genome.end(), std::greater_equal<double>{});
    size_t cut_pos = cut - genome.begin();
    std::fill(trait_values.begin(), trait_values.begin() + peak, 0.0);
    std::copy(peak_it, cut, trait_values.begin() + peak);
    std::fill(trait_values.begin() + cut_pos, trait_values.end(), 0.0);
 }

  // A landscape with "valleys" is where higher values briefly decline in a growing sawtooth.
  // Valleys start at width 1 at trait level 8 (to 9), then 9-11 (2 wide), 11-14 (3 wide),
  // 14-18 (4 wide), 18-23 (5 wide),  23-29 (6 wide),  29-36 (7 wide),  36-44 (8 wide),
  // 44-53 (9 wide), 53-63 (10 wide), 63-74 (11 wide), 74-86 (12 wide), 86-99 (13 wide)
  static double ApplyValley(const double value) {
    if (value < 8.0 || value > 99.0) return value;  // Outside of valleys
    const double valley_id = std::floor((std::sqrt(8.0 * (value - 8.0) + 1.0) - 1.0) / 2.0);
    const double valley_start = 8.0 + valley_id * (valley_id + 1) / 2;
    const double drop = value - valley_start;
    return valley_start - drop;
  }

  template <concepts::Organism ORG_T>
  void ApplyValleys(ORG_T & org) {
    for (double & v : org.GetPhenotype().trait_values) v = ApplyValley(v);
  }
  
  template <concepts::Organism ORG_T>
  void Evaluate(ORG_T & org) {
    // For now, always use base landscape.
    switch (landscape) {
    case Landscape::BASE:                EvalBasic(org);               break;
    case Landscape::STRUCTURE:           EvalStructured(org);          break;
    case Landscape::MULTIPATH:           EvalMultipath(org);           break;
    case Landscape::STRUCTURE_MULTIPATH: EvalStructuredMultipath(org); break;
    case Landscape::VALLEYS:             EvalBasic(org);               ApplyValleys(org); break;
    case Landscape::STRUCTURE_VALLEYS:   EvalStructured(org);          ApplyValleys(org); break;
    case Landscape::MULTIPATH_VALLEYS:   EvalMultipath(org);           ApplyValleys(org); break;
    case Landscape::FULL:                EvalStructuredMultipath(org); ApplyValleys(org); break;
    default: emp::notify::Error("Invalid Landscape");
    }

    emp::vector<double> & trait_values = org.GetPhenotype().trait_values;
    org.GetPhenotype().fitness = std::accumulate(trait_values.begin(), trait_values.end(), 0.0);
  }

public:
  OrgTypeDOSSIER(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>("OrgTypeDOSSIER", "Representation",
      "Vector of doubles for DOSSIER diagnostics")
    , avida(avida)
    { }
  ~OrgTypeDOSSIER() {}

  // === Phenotypic Traits ===

  struct Phenotype {
    emp::vector<double> trait_values{};
    double fitness = 0;
  };

  struct GlobalTypes {
    using genome_t   = Genome<double>;
  };

  void RegisterTraits() {
    AVIDA_REGISTER_TRAIT(trait_values, "Distance of each genome position from target");
    AVIDA_REGISTER_TRAIT(fitness, "Target minus error summed across all positions");
  }

  void RegisterSettings() {
    avida.AddSetting("DOSSIER.genome_length", genome_length, "Maximum number of updates to run", 'l');
    avida.AddSetting("DOSSIER.target_value", target_value, "Target value for each genome position");
    avida.AddSetting("DOSSIER.starting_count", starting_count, "Initial number of organisms to inject");
    avida.AddSetting("DOSSIER.mut_prob",
      [this](){ return mut_prob; },
      [this](double p){ mut_prob = p; mut_scale = 1.0 / emp::Log2(1.0 - mut_prob); },
      "Per-site substitution probability", 'p');
    avida.AddSetting("DOSSIER.mut_size", mut_size, "standard deviation on mutation change");
    avida.AddSetting("DOSSIER.landscape",
      [this](){ return ToName(landscape); },
      [this](std::string s){ landscape = ToLandscape(s); },
      "Landscape type: 'structure', 'multipath', 'valleys', 'none', or combos ('multipath,valleys')",
      'L');
  }

  // === Signal Listeners ===

  void OnStart() {
    std::println("Using diagnostic: {}", ToName(landscape));

    genome_manager.SetSizeRange(genome_length, genome_length); // Fixed length.
    Genome<double> empty_genome{genome_manager, genome_length, 0.0};

    // Inject starting organisms...
    avida.Inject(empty_genome, starting_count);
  }

  template <concepts::Organism ORG_T>
  void OnInjectReady(ORG_T & org) {
    Evaluate(org);
  }

  template <concepts::Organism ORG_T>
  void OnOffspringInit(ORG_T & offspring, ORG_T & parent) {
    // Possibly mutate this genome.
    auto & genome = offspring.GetGenome();
    emp::Random & random = avida.GetRandom();
    size_t mut_pos = static_cast<size_t>(std::log2(random.GetDoubleNonZero()) * mut_scale);
    while (mut_pos < genome.size()) {
      double shift = random.GetNormal() * mut_size;
      genome[mut_pos] += shift;

      // Rebound if over limit.
      if (genome[mut_pos] < 0.0) genome[mut_pos] *= -1;
      else if (genome[mut_pos] > target_value) {
        genome[mut_pos] = 2.0 * target_value - genome[mut_pos];
      }
      offspring.SetMutated();

      // Find next mutation, if any.
      mut_pos += static_cast<size_t>(std::log2(random.GetDoubleNonZero()) * mut_scale) + 1;
    }

    if (offspring.IsMutated()) Evaluate(offspring);
    else {
      offspring.GetPhenotype() = parent.GetPhenotype();
    }
  }

  void BeforeExit() {
    const auto & org = avida.GetFirstOrg();
    const auto & genome = org.GetGenome();
    const auto & traits = org.GetPhenotype().trait_values;
    for (size_t i = 0; i < genome.size(); ++i) {
      std::println("{} -> {}", genome[i], traits[i]);
    }
  }
};
