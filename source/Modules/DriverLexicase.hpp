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
#include "emp/math/random_utils.hpp"
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
  double downsample_frac = 1.0;               // Fraction of test cases to each generation
  bool informed_downsample = false;           // Should we use "informed" downsampling?
  double epsilon = 0.0;                       // Distance from max to not be eliminated
  emp::String scores_name = "trait_values";   // Which traits to test for lexicase?
  emp::String fitness_name = "fitness";       // Which traits to test for combined fitness? (output only)

  void PrintStats(size_t ud) {
    std::println("Generation: {} ; PopSize: {} ; Fitness0: {}\nGenome0:{}",
      ud, avida.GetNumOrgs(), avida.GetFirstOrg().GetPhenotype().fitness,
      avida.GetFirstOrg().GetGenomeSequence()
    );
  }

public:
  DriverLexicase(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>("DriverLexicase", "Execution", "Run an EA with Lexicase Selection.")
    , avida(avida), output("Lexicase.csv") { }
  ~DriverLexicase() { }

  void Serialize(emp::SerialPod & /* pod */) {
    // Nothing extra to serialize; everything should be in the SettingsManager
  }

  // === Phenotypic Traits ===

  struct Phenotype {
  };

  void RegisterTraits() {
    // AVIDA_REQUIRE_TRAIT(size_t, generation);
  }

  void RegisterSettings() {
    avida.AddSetting("lexicase.data_filename",
      [this](){ return output.GetFilename(); },
      [this](emp::String in){ output.SetFilename(in); },
      "File to output lexicase data (placed in default data directory)");
    avida.AddSetting("lexicase.output_frequency", output_frequency, "Updates between data outputs");
    avida.AddSetting("lexicase.pop_size", pop_size, "How big should populations be?", 'p');
    avida.AddSetting("lexicase.downsample_frac", downsample_frac, "Fraction of test cases to use each generation (1.0 for ALL)", 'D');
    avida.AddSetting("lexicase.informed_downsample", informed_downsample, "Should we use informed downsampling? (true/false)");
    avida.AddSetting("lexicase.epsilon", epsilon, "Max score deficit to not be eliminated (0.0 = strict lexicase)", 'e');
    avida.AddSetting("lexicase.max_generations", max_generations, "Maximum number of generations to run", 'm');
    avida.AddSetting("lexicase.scores_name", scores_name, "Name of trait to use for scores");
    avida.AddSetting("lexicase.fitness_name", fitness_name, "Name of trait to use for fitness");
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

  /// Calculate the maximum value across a specified set of organism for a particular score.
  [[nodiscard]] double CalcMaxScore(
    const auto & score_accessor,
    const emp::BitVector & org_set,
    size_t score_id) const
  {
    auto score_fun = [&](size_t org_id) { return score_accessor(avida.GetOrg(org_id))[score_id]; };
    return org_set.MaxValue(score_fun);
  }

  /// Scan through a specified set of organisms and calculate the maximum value for each score.
  [[nodiscard]] emp::vector<double> CalcMaxScores(
    const auto & score_accessor,
    const emp::BitVector & org_set) const
  {
    const size_t num_scores = score_accessor(avida.GetFirstOrg()).size();
    emp::vector<double> max_scores(num_scores);
    for (size_t score_id = 0; score_id < num_scores; ++score_id) {
      max_scores[score_id] = CalcMaxScore(score_accessor, org_set, score_id);
    }
    return max_scores;
  }

  /// Select a parent via lexicase selection from the candidates and test cases provided.
  /// @param score_accessor Direct (non-virtual) accessor for the scores trait.
  /// @param parent_bits    Bit vector of candidate org IDs (copied; modifications are local).
  /// @param test_case_ids  Indices into each organism's score vector; shuffled in place.
  [[nodiscard]] size_t SelectParent(const auto & score_accessor,
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

      // Find highest score on this test case, then eliminate all candidates below the threshold.
      const double score_threshold = score_fun(parent_bits.MaxIndex(score_fun)) - epsilon;
      parent_bits.ClearIf([&](size_t org_id){ return score_fun(org_id) < score_threshold; });
    }

    // Choose randomly from the surviving candidates.
    return parent_bits.FindNthOne(random.GetUInt32(parent_bits.CountOnes()));
  }

  /// For each task, identify the set of organisms that have within epsilon of the top score.
  [[nodiscard]] emp::vector<emp::BitVector> CalcScoreMatrix(
    const auto & score_accessor,
    const emp::BitVector & org_set
  ) const {
    auto max_scores = CalcMaxScores(score_accessor, org_set);
    emp::vector<emp::BitVector> score_matrix(max_scores.size());
    for (size_t score_id = 0; score_id < score_matrix.size(); ++score_id) {
      emp::BitVector & org_profile = score_matrix[score_id];
      org_profile.Resize(org_set.size());
      for (size_t org_id : org_set) {
        auto & org = avida.GetOrg(org_id);
        if (score_accessor(org)[score_id] + epsilon >= max_scores[score_id]) {
          org_profile.Set(org_id);
        }
      }
    }
    return score_matrix;
  }

  /// Given a set of test cases where the first num_tests have been chosen, pick the remaining index
  /// that is most informative.
  [[nodiscard]] size_t FindNextInformativeID(
    const emp::vector<size_t> & test_case_ids,
    size_t num_tests,
    const emp::vector<emp::BitVector> & score_matrix) const
  {
    emp_assert(num_tests > 0, "At least one test must be locked in for others to be informative.");
    emp_assert(num_tests < test_case_ids.size(), "No test cases available to select.");

    // Collect profiles of the already-chosen test cases (order in test_case_ids != order in score_matrix).
    emp::vector<emp::BitVector> used_profiles;
    used_profiles.reserve(num_tests);
    for (size_t i = 0; i < num_tests; ++i) used_profiles.push_back(score_matrix[test_case_ids[i]]);
    auto used_span = std::span{used_profiles};

    // Among the remaining candidates, find the position (in test_case_ids) with max min-distance.
    size_t max_dist = score_matrix[test_case_ids[num_tests]].CalcMinDistance(used_span);
    size_t max_id = num_tests;  // index into test_case_ids, not into score_matrix
    for (size_t test_pos = num_tests + 1; test_pos < test_case_ids.size(); ++test_pos) {
      size_t test_dist = score_matrix[test_case_ids[test_pos]].CalcMinDistance(used_span);
      if (test_dist > max_dist) {
        max_dist = test_dist;
        max_id = test_pos;
      }
    }
    return max_id;
  }

  [[nodiscard]] std::span<size_t> GetTestCases(
    size_t num_test_cases,
    const emp::BitVector & org_set,
    const auto & score_accessor) const
  {
    static emp::vector<size_t> test_case_ids;

    // If we need to initialize or have changed the number of test cases, update the ID set.
    if (test_case_ids.size() != num_test_cases) {
      test_case_ids.resize(num_test_cases);
      std::iota(test_case_ids.begin(), test_case_ids.end(), 0);
    }

    const size_t tests_used = std::max<size_t>(num_test_cases * downsample_frac, 1);

    // Test if we need to downsample.
    if (tests_used < num_test_cases) {
      if (informed_downsample) {
        // Informed downsample: Start random and repeatedly pick most non-overlapping cases.
        auto score_matrix = CalcScoreMatrix(score_accessor, org_set);
        size_t case_id = avida.GetRandom().GetUInt(test_case_ids.size());
        std::swap(test_case_ids[0], test_case_ids[case_id]);
        
        for (size_t num_tests = 1; num_tests < tests_used; ++num_tests) {
          case_id = FindNextInformativeID(test_case_ids, num_tests, score_matrix);
          std::swap(test_case_ids[num_tests], test_case_ids[case_id]);
        }
      } else {
        // Do a random downsample.
        emp::Shuffle(avida.GetRandom(), test_case_ids, tests_used);
      }
      return std::span{test_case_ids}.first(tests_used);
    }

    return std::span{test_case_ids};
  }

  void OnUpdate(size_t /*update*/) {
    emp::Timer<"OnUpdate"> fun_timer;
    // Look up the typed scores accessor once per update (avoids per-offspring map lookups).
    const auto & score_accessor =
      avida.template GetTypedTrait<emp::vector<double>>(scores_name).GetConstAccessFun();
    const size_t num_test_cases = score_accessor(avida.GetFirstOrg()).size();

    // Collect the parent population.
    const emp::BitVector parent_bits = avida.GetActiveBits();
    emp_assert(parent_bits.CountOnes() > 0);

    std::span<size_t> test_case_ids = GetTestCases(num_test_cases, parent_bits, score_accessor);

    // Run pop_size rounds of lexicase selection to generate the next generation.
    emp::Timer<"Do Lexicase"> lexi_timer;
    for (size_t round = 0; round < pop_size; ++round) {      
      const size_t parent_id =
        SelectParent(score_accessor, parent_bits, test_case_ids);
      avida.DivideOrg(parent_id);
    }
    lexi_timer.Stop();

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
