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

  /// Select a parent from the list of choices available and the specific traits provided.
  /// @param parent_bits A bit vector specifying which org IDs we should be considering as parents.
  /// @param test_case_ids Indices into organism score vectors indicating which tests to apply.
  ///   test_case_ids may be shuffled in place, but the set of IDs will not change.
  size_t SelectParent(emp::BitVector parent_bits, std::span<size_t> test_case_ids) {
    emp_assert(parent_bits.CountOnes() > 0);
    emp_assert(test_case_ids.size() > 0);
    emp::Random & random = avida.GetRandom();
    const auto & scores_trait = avida.GetTrait(scores_name);
    size_t next_pos = 0;  // Fisher-Yates frontier: test_case_ids[0..next_pos) already used.

    while (parent_bits.CountOnes() > 1 && next_pos < test_case_ids.size()) {
      // Randomly select the next test case from the remaining ones (Fisher-Yates step).
      std::swap(test_case_ids[next_pos],
                test_case_ids[random.GetUInt32(next_pos, test_case_ids.size())]);
      const size_t test_case_id = test_case_ids[next_pos++];
      auto score_fun = [&](size_t org_id){
        return scores_trait(avida.GetOrg(org_id))[test_case_id];
      };

      // Find highest score on this test case.
      const size_t max_id = parent_bits.MaxIndex(score_fun);
      const double max_score = score_fun(max_id);

      parent_bits.ClearIf([&](size_t org_id){ return score_fun(org_id) < max_score; });
    }

    // Choose parent to return.
    const size_t num_remaining = parent_bits.CountOnes();
    const size_t parent_pick = random.GetUInt32(num_remaining);
    return parent_bits.FindNthOne(parent_pick);
  }

  void OnUpdate(size_t /*update*/) {
    // Get the trait that we need for determining fitness.
    const auto & scores_trait = avida.GetTrait(scores_name);

    // Collect the initial organisms in the population
    const emp::BitVector parent_bits = avida.GetActiveBits();
    emp_assert(parent_bits.CountsOnes() > 0);

    // Build a set of all of the trait IDs.
    emp::vector<size_t> test_case_ids = 

    auto vec = std::views::iota(0, num_test_cases) 
             | std::ranges::to<std::vector<int>>()

    // Generate offspring (do pop_size rounds)
    for (size_t round = 0; round < pop_size; ++round) {
      // Select parent to produce an offspring.
      avida.DivideOrg( SelectParent(parent_bits, test_case_ids) );
    }




    // Run Tournaments.
    emp::vector<size_t> tourny_ids(tourny_size); // Org IDs in a tournament.
    for (size_t tourny_round = 0; tourny_round < pop_size; ++tourny_round) {
      // Collect orgs for this tournament.
      for (size_t & id : tourny_ids) {
        id = parent_ids[avida.GetRandom().GetUInt(parent_ids.size())];
      }

      // Determine winner of this tournament.
      size_t best_id = tourny_ids[0];
      double best_fit = fit_trait.AsDouble(avida.GetOrg(tourny_ids[0]).GetPhenotype());

      for (size_t i = 1; i < tourny_size; ++i) {
        const double cur_fit = fit_trait.AsDouble(avida.GetOrg(tourny_ids[i]).GetPhenotype());
        if (best_fit < cur_fit) {
          best_id = tourny_ids[i];
          best_fit = cur_fit;
        }
      }

      avida.DivideOrg(best_id);  // Have the winner produce an offspring.
    }

    // Now that the offspring have been produced, remove the parents.
    for (size_t id : parent_ids) avida.DeleteOrg(id);
  }

  void OnUpdateEnd(size_t update) {
    if (update % output_frequency == 0) {
      PrintStats(update);
      output.DoOutput();
    }
    if (update >= max_generations) avida.Exit();
  }

};
