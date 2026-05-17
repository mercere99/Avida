#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  This module configures and runs an EC based on lexicase selection.
 *  Handles its own population management.
 */

#include <cstddef>    // for size_t
#include <filesystem> // for path joining
#include <iostream>
#include <numeric>    // for std::iota
#include <span>

#include "emp/base/vector.hpp"
#include "emp/data/DataOutput.hpp"
#include "emp/tools/String.hpp"

#include "../core/Avida.hpp"

namespace fs = std::filesystem;

template <typename AVIDA_T>
class DriverLexicase : public ModuleBase<AVIDA_T> {
private:
  AVIDA_T & avida;

  emp::DataOutput output;
  size_t output_frequency = 100;

  size_t max_generations = 10000;             // How many updates should the run go for?
  size_t pop_size = 1000;
  emp::String scores_name = "trait_values";   // Which traits to test for lexicase?
  emp::String fitness_name = "fitness";       // Which traits to test for combined fitness? (output only)

  void PrintStats(size_t ud) {
    std::println("Generation: {} ; PopSize: {} ; Fitness0: {}\nGenome0:{}",
      ud, avida.GetNumOrgs(), avida.GetOrg(0).GetPhenotype().fitness,
      avida.GetFirstOrg().GetGenomeSequence()
    );
  }

public:
  DriverLexicase(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>("DriverLexicase", "Execution", "Run an EA with Lexicase Selection.")
    , avida(avida), output("Lexicase.csv") { }
  ~DriverLexicase() { }

  // === Phenotypic Traits ===

  struct Phenotype {
  };

  void RegisterTraits() {
    // AVIDA_REQUIRE_TRAIT(size_t, generation);
  }

  void RegisterSettings() {
    avida.AddSetting("base.data_filename",
      [this](){ return output.GetFilename(); },
      [this](emp::String in){ output.SetFilename(in); },
      "File to output lexicase data (placed in default data directory)");
    avida.AddSetting("base.output_frequency", output_frequency, "Updates between data outputs");
    avida.AddSetting("base.pop_size", pop_size, "How big should populations be?", 'P');
    avida.AddSetting("base.max_generations", max_generations, "Maximum umber of generations to run", 'G');
    avida.AddSetting("base.scores_name", scores_name, "Name of trait to use for scores");
    avida.AddSetting("base.fitness_name", fitness_name, "Name of trait to use for fitness");
  }

  size_t GetOrgReserveCount() const { return pop_size * 2; }

  // === Signal Listeners ===

  void BeforeStart() {
    // If we have a filename, set up the date file columns.
    if (output.GetFilename().size()) {
      output.SetFilepath(avida.GetDataDir());
      output.AddColumn("Update", [this](){ return avida.GetUpdate(); });
      output.AddColumn("Max Fitness", [this](){ return avida.CalcTraitMax(fitness_name); });
      output.AddColumn("Average Fitness", [this](){ return avida.CalcTraitAve(fitness_name); });
    }
  }

  void OnStart() {
    PrintStats(0);  // Report initial state before any organisms run.
    output.DoOutput();
  }

  /// Select a parent via lexicase selection from the candidates and test cases provided.
  /// @param score_accessor Direct (non-virtual) accessor for the scores trait.
  /// @param parent_bits    Bit vector of candidate org IDs (copied; modifications are local).
  /// @param test_case_ids  Indices into each organism's score vector; shuffled in place.
  size_t SelectParent(const auto & score_accessor,
                      emp::BitVector parent_bits, std::span<size_t> test_case_ids) {
    emp_assert(parent_bits.CountOnes() > 0);
    emp_assert(test_case_ids.size() > 0);
    emp::Random & random = avida.GetRandom();
    size_t next_pos = 0;  // Fisher-Yates frontier: test_case_ids[0..next_pos) already used.

    while (parent_bits.CountOnes() > 1 && next_pos < test_case_ids.size()) {
      // Randomly select the next test case from the remaining ones (Fisher-Yates step).
      std::swap(test_case_ids[next_pos],
                test_case_ids[random.GetUInt32(next_pos, test_case_ids.size())]);
      const size_t test_case_id = test_case_ids[next_pos++];  // Index into each org's score vector.
      auto score_fun = [&](size_t org_id) {
        return score_accessor(avida.GetOrg(org_id))[test_case_id];
      };

      // Find highest score on this test case, then eliminate all candidates below it.
      const double max_score = score_fun(parent_bits.MaxIndex(score_fun));
      parent_bits.ClearIf([&](size_t org_id){ return score_fun(org_id) < max_score; });
    }

    // Choose randomly from the surviving candidates.
    return parent_bits.FindNthOne(random.GetUInt32(parent_bits.CountOnes()));
  }

  void OnUpdate(size_t /*update*/) {
    // Look up the typed scores accessor once per update (avoids per-offspring map lookups).
    const auto & score_accessor =
      avida.template GetTypedTrait<emp::vector<double>>(scores_name).GetConstAccessFun();

    // Collect the parent population.
    const emp::BitVector parent_bits = avida.GetActiveBits();
    emp_assert(parent_bits.CountOnes() > 0);

    // Build test case IDs [0, num_test_cases); size is read from the first organism's scores.
    const size_t num_test_cases = score_accessor(avida.GetFirstOrg()).size();
    emp::vector<size_t> test_case_ids(num_test_cases);
    std::iota(test_case_ids.begin(), test_case_ids.end(), 0);

    // Run pop_size rounds of lexicase selection to generate the next generation.
    for (size_t round = 0; round < pop_size; ++round) {
      avida.DivideOrg(SelectParent(score_accessor, parent_bits, test_case_ids));
    }

    // Remove the parent generation now that offspring have been placed.
    for (size_t id : parent_bits) avida.DeleteOrg(id);
  }

  void OnUpdateEnd(size_t update) {
    if (update % output_frequency == 0) {
      PrintStats(update);
      output.DoOutput();
    }
    if (update >= max_generations) avida.Exit();
  }

};
