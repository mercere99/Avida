#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  Organisms the use the Rate Of Mutation Evolution Optimizer (ROMEO)
 * 
 * 
 *  DEVELOPER NOTES:
 *  - Allow changing environments.
 */

#include <cstddef>   // for size_t
#include <fstream>
#include <iostream>

#include "emp/base/notify.hpp"
#include "emp/base/vector.hpp"
#include "emp/data/DataOutput.hpp"
#include "emp/datastructs/Vector.hpp"
#include "emp/math/Random.hpp"
#include "emp/tools/String.hpp"

#include "../core/Avida.hpp"
#include "../core/Genome.hpp"

template <typename AVIDA_T>
class OrgTypeROMEO : public ModuleBase<AVIDA_T> {
private:
  using ModuleBase<AVIDA_T>::avida;

  emp::DataOutput output;

  // Configurable values
  size_t output_frequency = 100;
  size_t genome_length = 100;
  double max_value = 100.0;
  size_t starting_count = 1000;
  double init_mut_prob = 0.01;
  double mut_size = 1.0;
  double mut_rate_mut_prob = 0.01;
  double mut_rate_mut_size = 0.1;
  double target_change_per_update = 1.0;

  // Environment state
  emp::Vector<double> target_genome;
  double change_sum = 0.0;


  emp::Vector<double> & ErrorValues(concepts::Organism auto & org) {
    return org.GetPhenotype().error_values;
  }

  const emp::Vector<double> & ErrorValues(const concepts::ConstOrganism auto & org) {
    return org.GetPhenotype().error_values;
  }

  emp::Vector<double> & FitnessValues(concepts::Organism auto & org) {
    return org.GetPhenotype().fitness_values;
  }

  const emp::Vector<double> & FitnessValues(const concepts::ConstOrganism auto & org) {
    return org.GetPhenotype().fitness_values;
  }

  template <concepts::Organism ORG_T>
  void Evaluate(ORG_T & org) {
    auto & error_values = ErrorValues(org);
    auto & fitness_values = FitnessValues(org);
    error_values = org.GetGenome().Values() - target_genome;

    // fitness_values = 1.0 / error_values;
    fitness_values.resize(error_values.size());
    for (size_t i = 0; i < error_values.size(); ++i) {
      fitness_values[i] = 1.0 / error_values[i];
    }

    error_values = error_values * error_values;
    org.GetPhenotype().total_error = error_values.CalcSum();
    org.GetPhenotype().fitness = 1.0 / org.GetPhenotype().total_error;
  }

  void EvaluateAll() {
    avida.GetBiota().ForEachOrg([this](concepts::Organism auto & org){
      Evaluate(org);
    });
  }


public:
  OrgTypeROMEO(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>(avida, "OrgTypeROMEO", "Representation",
      "Vector of doubles for Rate Of Mutation Evolution Optimization (ROMEO)")
    , output("ROMEO.csv")
    { }
  ~OrgTypeROMEO() {}

  void Serialize(emp::SerialPod & pod) { pod(target_genome); }

  // === Phenotypic Traits ===

  struct Phenotype {
    emp::Vector<double> error_values{};
    emp::Vector<double> fitness_values{};  // 1/error_value
    double mut_prob = 0.0;
    double total_error = 0.0;
    double fitness = 0.0;
  };

  struct GlobalTypes {
    using genome_t = Genome<double>;
  };

  void RegisterTraits() {
    AVIDA_REGISTER_TRAIT(error_values, "Squared distance of each genome position from target");
    AVIDA_REGISTER_TRAIT(fitness_values, "Inverse of square error of each genome position from target");
    AVIDA_REGISTER_TRAIT(mut_prob, "Org-specific per-site mutation probability.");
    AVIDA_REGISTER_TRAIT(total_error, "Sum of all trait values.");
    AVIDA_REGISTER_TRAIT(fitness, "Inverse of total_error.");
  }

  void RegisterSettings() {
    avida.AddSetting("ROMEO.data_filename",
      [this](){ return output.GetFilename(); },
      [this](emp::String in){ output.SetFilename(in); },
      "File to output ROMEO data (placed in default data directory)");
    avida.AddSetting("ROMEO.output_frequency", output_frequency,
      "Updates between ROMEO stat outputs");
    avida.AddSetting("ROMEO.genome_length", genome_length, "Number of genes in the genomes");
    avida.AddSetting("ROMEO.max_value", max_value, "Highest allowed value for each genome position");
    avida.AddSetting("ROMEO.starting_count", starting_count, "Initial number of organisms to inject");
    avida.AddSetting("ROMEO.init_mut_prob",
      [this](){ return init_mut_prob; },
      [this](double p){ init_mut_prob = p; },
      "Per-site substitution probability", 'P');
    avida.AddSetting("ROMEO.mut_size", mut_size, "standard deviation on mutation change");
    avida.AddSetting("ROMEO.mut_rate_mut_prob", mut_rate_mut_prob, "Probability of a mutation rate changing.");
    avida.AddSetting("ROMEO.mut_rate_mut_size", mut_rate_mut_size, "Standard deviation on mutation rate change");
    avida.AddSetting("ROMEO.target_change_per_update", target_change_per_update, "How many environment sites should change per update (0.5 -> one every 2 updates)");
  }

  // === Signal Listeners ===

  void BeforeStart() {
    if (output.GetFilename().size()) {
      output.SetFilepath(avida.GetDataDir());
      output.AddColumn("Update", [this](){ return avida.GetUpdate(); });
      output.AddColumn("Fittest Organism ID", [this](){
        return avida.FindOrg_MaxTrait("fitness").GetBiotaID();
      });
      output.AddColumn("Fittest Organism Error", [this](){
        return avida.CalcTraitMin("total_error");
      });
      output.AddColumn("Fittest Organism Fitness", [this](){
        return avida.CalcTraitMax("fitness");
      });
      output.AddColumn("Min Mutation Rate", [this](){
        return avida.CalcTraitMin("mut_prob");
      });
      output.AddColumn("Average Mutation Rate", [this](){
        return avida.CalcTraitAve("mut_prob");
      });
      output.AddColumn("Max Mutation Rate", [this](){
        return avida.CalcTraitMax("mut_prob");
      });

      output.AddColumn("Fittest Organism Genotype", [this](){
        return avida.FindOrg_MaxTrait("fitness").GetGenomeSequence();
      });
      output.AddColumn("Fittest Organism Phenotype",  [this](){
        return avida.FindOrg_MaxTrait("fitness").GetPhenotype().fitness_values;
      });
    }
  }


  void OnStart() {
    Genome<double> empty_genome{100.0, genome_length, 0.0};

    target_genome.Resize(genome_length);
    for (auto & x : target_genome) {
      x = avida.GetRandom().GetDouble(max_value);
    }

    // Inject starting organisms...
    avida.Inject(empty_genome, starting_count);
    EvaluateAll();
    output.DoOutput();
  }

  template <concepts::Organism ORG_T>
  void OnInjectReady(ORG_T & org) {
    org.GetPhenotype().mut_prob = init_mut_prob;
    Evaluate(org);
  }

  template <concepts::Organism ORG_T>
  void OnOffspringInit(ORG_T & offspring, ORG_T & parent) {
    emp::Random & random = avida.GetRandom();
    double & mut_prob = offspring.GetPhenotype().mut_prob;

    mut_prob = parent.GetPhenotype().mut_prob;
    if (random.P(mut_rate_mut_prob)) {
      // double mut_factor = random.GetNormal() * mut_rate_mut_size + 1.0;
      double mut_factor = std::pow(1.0 + mut_rate_mut_size, random.GetNormal());
      mut_prob *= mut_factor;
      if (mut_prob > 1.0) mut_prob = 1.0; // Do not allow mutation probabilities > 1.0.
      offspring.SetMutated();
    }

    // If we are not mutating the genome, stop here.
    if (mut_prob == 0.0) return;

    auto & genome = offspring.GetGenome();
    const double mut_scale{1.0 / std::log2(1.0 - mut_prob)};
    size_t mut_pos = static_cast<size_t>(std::log2(random.GetDoubleNonZero()) * mut_scale);
    while (mut_pos < genome.size()) {
      double shift = random.GetNormal() * mut_size;
      genome[mut_pos] += shift;

      // Rebound if over limit.
      if (genome[mut_pos] < 0.0) genome[mut_pos] *= -1;
      else if (genome[mut_pos] > max_value) {
        genome[mut_pos] = 2.0 * max_value - genome[mut_pos];
      }
      offspring.SetMutated();

      // Find next mutation, if any.
      const size_t pos_skipped = static_cast<size_t>(std::log2(random.GetDoubleNonZero()) * mut_scale);
      mut_pos += pos_skipped + 1;
    }

    if (offspring.IsMutated()) Evaluate(offspring);
    else {
      offspring.GetPhenotype() = parent.GetPhenotype();
    }
  }

  void OnUpdateStart([[maybe_unused]] size_t update) {
    change_sum += target_change_per_update;
    if (change_sum < 1.0) return;

    while (change_sum >= 1.0) {
      size_t pos = avida.GetRandom().GetValue(target_genome.size());
      target_genome[pos] = avida.GetRandom().GetValue(max_value);
      --change_sum;
    }

    // Re-evaluate organisms now that the environment has changed.
    EvaluateAll();
  }

  void OnUpdateEnd(size_t update) {
    if (output_frequency && update % output_frequency == 0) {
      output.DoOutput();
      std::println("TARGET: {}", emp::MakeString(target_genome));
    }
  }

  void BeforeExit() {
    PrintPopulation();
  }

  void PrintPopulation() {
    emp::String filename = emp::MakeString("FullPop-", avida.GetUpdate());
    std::ofstream out_stream(avida.GetDataDir() / filename.str());
    out_stream << "fitness,genotype,phenotype\n";
    avida.GetBiota().ForEachOrg([&](const auto & org) {
      std::println(out_stream, "{},{},{}",
        org.GetPhenotype().fitness, org.GetGenomeSequence(), emp::MakeString(ErrorValues(org)));
    });
  }


};
