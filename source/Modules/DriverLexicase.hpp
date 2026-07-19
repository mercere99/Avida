#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  This module configures and runs an EC based on lexicase selection.
 *  Handles its own population management.
 */

#include <algorithm>
#include <chrono>
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
  using ModuleBase<AVIDA_T>::avida;

  emp::DataOutput output;
  size_t output_frequency = 100;

  size_t max_generations = 10000;            // How many updates should the run go for?
  size_t pop_size = 1000;                    // Number of organisms to place in next gen
  double epsilon = 0.0;                      // Distance from max to not be eliminated
  double downsample_frac = 1.0;              // Fraction of test cases to each generation
  double info_epsilon = 0.0;                 // Max dist for scores to count as "same" for informed downsampling
  emp::String mode = "base";                 // Modes: "base", "informed", "cohort"
  emp::String scores_name = "trait_values";  // Traits to use for lexicase scores
  emp::String fitness_name = "fitness";      // Trait to output for "combined" fitness
  bool profile = false;                      // Print lexicase timing and counter diagnostics?
  size_t profile_frequency = 1;              // Updates between profile outputs.
  bool cache_first_pass = true;               // Reuse full-population filtering by first test?

  using clock_t = std::chrono::steady_clock;

  struct ProfileStats {
    double get_tests_ms = 0.0;
    double score_matrix_ms = 0.0;
    double informed_sample_ms = 0.0;
    double selection_ms = 0.0;
    double delete_ms = 0.0;
    size_t parent_count = 0;
    size_t test_count = 0;
    size_t tests_used = 0;
    size_t selection_rounds = 0;
    size_t lexicase_steps = 0;
    size_t candidate_score_reads = 0;
    size_t survivor_sum = 0;
    size_t max_survivors = 0;
    size_t first_pass_cache_hits = 0;
    size_t first_pass_cache_misses = 0;
    size_t first_pass_score_reads_saved = 0;

    void Reset() { *this = {}; }
  };

  mutable ProfileStats profile_stats;

  [[nodiscard]] static double ElapsedMS(clock_t::time_point start) {
    return std::chrono::duration<double, std::milli>(clock_t::now() - start).count();
  }

  void PrintProfile(size_t update) const {
    const double ave_steps = profile_stats.selection_rounds
      ? static_cast<double>(profile_stats.lexicase_steps) / profile_stats.selection_rounds
      : 0.0;
    const double ave_survivors = profile_stats.selection_rounds
      ? static_cast<double>(profile_stats.survivor_sum) / profile_stats.selection_rounds
      : 0.0;

    std::println(
      "LexicaseProfile update={} parents={} tests_used={}/{} "
      "time_ms[get_tests={:.3f} score_matrix={:.3f} informed_sample={:.3f} "
      "selection={:.3f} delete={:.3f}] "
      "rounds={} steps={} ave_steps={:.3f} score_reads={} "
      "first_pass_cache[hits={} misses={} reads_saved={}] "
      "ave_final_survivors={:.3f} max_final_survivors={}",
      update,
      profile_stats.parent_count,
      profile_stats.tests_used,
      profile_stats.test_count,
      profile_stats.get_tests_ms,
      profile_stats.score_matrix_ms,
      profile_stats.informed_sample_ms,
      profile_stats.selection_ms,
      profile_stats.delete_ms,
      profile_stats.selection_rounds,
      profile_stats.lexicase_steps,
      ave_steps,
      profile_stats.candidate_score_reads,
      profile_stats.first_pass_cache_hits,
      profile_stats.first_pass_cache_misses,
      profile_stats.first_pass_score_reads_saved,
      ave_survivors,
      profile_stats.max_survivors
    );
  }

  void PrintStats(size_t ud) {
    std::println("Generation: {} ; PopSize: {} ; Fitness0: {}\nGenome0:{}",
      ud, avida.GetNumOrgs(), avida.GetFirstOrg().GetPhenotype().fitness,
      avida.GetFirstOrg().GetGenomeSequence()
    );
  }

public:
  DriverLexicase(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>(avida, "DriverLexicase", "Execution",
        "Run an EA with Lexicase Selection.")
    , output("Lexicase.csv") { }
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
    avida.AddSetting("lexicase.epsilon", epsilon, "Max score deficit to not be eliminated (0.0 = strict lexicase)", 'e');
    avida.AddSetting("lexicase.downsample_frac", downsample_frac, "Fraction of test cases to use each generation (1.0 for ALL)", 'D');
    avida.AddSetting("lexicase.mode", mode, "Options: 'base', 'informed', or 'cohort'", 'M');
    avida.AddSetting("lexicase.info_epsilon", info_epsilon, "Max dist for scores to count as 'same' info tests");
    avida.AddSetting("lexicase.max_generations", max_generations, "Maximum number of generations to run", 'm');
    avida.AddSetting("lexicase.scores_name", scores_name, "Name of trait to use for scores");
    avida.AddSetting("lexicase.fitness_name", fitness_name, "Name of trait to use for fitness");
    avida.AddSetting("lexicase.profile", profile, "Print lexicase timing and inner-loop counters");
    avida.AddSetting("lexicase.profile_frequency", profile_frequency, "Updates between lexicase profile outputs");
    avida.AddSetting("lexicase.cache_first_pass", cache_first_pass,
      "Cache each test's first-pass survivors within a generation");
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

  struct FirstPassCacheEntry {
    emp::BitVector survivors;
    size_t survivor_count = 0;
    size_t score_reads = 0;
    bool valid = false;
  };

  /// Select a parent via lexicase selection from the candidates and test cases provided.
  /// @param parent_bits    Bit vector containing all candidate org IDs.
  /// @param parent_count   Number of one bits in parent_bits.
  /// @param score_cache    Current generation's score-vector pointers, indexed by org ID.
  /// @param test_case_ids  Indices into each organism's score vector; shuffled in place.
  /// @param first_pass_cache Cached survivors when each test is applied to all parents first.
  [[nodiscard]] size_t SelectParent(
    const emp::BitVector & parent_bits,
    size_t parent_count,
    const emp::vector<const emp::vector<double> *> & score_cache,
    std::span<size_t> test_case_ids,
    emp::vector<FirstPassCacheEntry> & first_pass_cache)
  {
    emp_assert(parent_count > 0);
    emp_assert(test_case_ids.size() > 0);
    emp::Random & random = avida.GetRandom();

    if (parent_count == 1) {
      if (profile) {
        ++profile_stats.selection_rounds;
        ++profile_stats.survivor_sum;
        profile_stats.max_survivors = std::max<size_t>(profile_stats.max_survivors, 1);
      }
      return parent_bits.FindNthOne(random.GetUInt32(parent_count));
    }

    emp::BitVector candidate_bits;
    size_t candidate_count = 0;

    // Apply one test to the current candidate set and return the number of score reads used.
    auto apply_test = [&](size_t test_case_id) {
      const size_t start_candidate_count = candidate_count;

      // Find score range on this test case; if all candidates are within epsilon, no one drops.
      double max_score = 0.0;
      double min_score = 0.0;
      bool first = true;
      for (size_t org_id : candidate_bits) {
        const double cur_score = (*score_cache[org_id])[test_case_id];
        if (first || cur_score > max_score) max_score = cur_score;
        if (first || cur_score < min_score) {
          min_score = cur_score;
          first = false;
        }
      }

      size_t score_reads = start_candidate_count;
      if (min_score + epsilon < max_score) {
        const double score_threshold = max_score - epsilon;
        for (size_t org_id : candidate_bits) {
          if ((*score_cache[org_id])[test_case_id] < score_threshold) {
            candidate_bits.Clear(org_id);
            --candidate_count;
          }
        }
        score_reads += start_candidate_count;
      }

      if (profile) {
        ++profile_stats.lexicase_steps;
        profile_stats.candidate_score_reads += score_reads;
      }
      return score_reads;
    };

    // Choose the first test before copying candidates. Its full-population result is reusable
    // whenever this test leads another selection during the same generation.
    std::swap(test_case_ids[0], test_case_ids[random.GetUInt32(test_case_ids.size())]);
    const size_t first_test_id = test_case_ids[0];
    FirstPassCacheEntry & cache_entry = first_pass_cache[first_test_id];
    if (cache_first_pass && cache_entry.valid) {
      candidate_bits = cache_entry.survivors;
      candidate_count = cache_entry.survivor_count;
      if (profile) {
        ++profile_stats.lexicase_steps;
        ++profile_stats.first_pass_cache_hits;
        profile_stats.first_pass_score_reads_saved += cache_entry.score_reads;
      }
    } else {
      candidate_bits = parent_bits;
      candidate_count = parent_count;
      const size_t score_reads = apply_test(first_test_id);
      if (cache_first_pass) {
        cache_entry.score_reads = score_reads;
        cache_entry.survivors = candidate_bits;
        cache_entry.survivor_count = candidate_count;
        cache_entry.valid = true;
        if (profile) ++profile_stats.first_pass_cache_misses;
      }
    }

    size_t next_pos = 1;  // Fisher-Yates frontier: test_case_ids[0..next_pos) already used.
    while (candidate_count > 1 && next_pos < test_case_ids.size()) {
      // Randomly select the next test case from the remaining ones (Fisher-Yates step).
      std::swap(test_case_ids[next_pos],
                test_case_ids[random.GetUInt32(next_pos, test_case_ids.size())]);
      const size_t test_case_id = test_case_ids[next_pos++];  // Index into each org's score vector.
      apply_test(test_case_id);
    }

    // Choose randomly from the surviving candidates.
    if (profile) {
      ++profile_stats.selection_rounds;
      profile_stats.survivor_sum += candidate_count;
      profile_stats.max_survivors = std::max(profile_stats.max_survivors, candidate_count);
    }
    return parent_bits.FindNthOne(random.GetUInt32(candidate_count));
  }

  /// For each task, identify the set of organisms that have within epsilon of the top score.
  [[nodiscard]] emp::vector<emp::BitVector> CalcScoreMatrix(
    const auto & score_accessor,
    const emp::BitVector & org_set
  ) const {
    const auto start = clock_t::now();
    auto max_scores = CalcMaxScores(score_accessor, org_set);
    emp::vector<emp::BitVector> score_matrix(max_scores.size());
    for (size_t score_id = 0; score_id < score_matrix.size(); ++score_id) {
      emp::BitVector & org_profile = score_matrix[score_id];
      org_profile.Resize(org_set.size());
      for (size_t org_id : org_set) {
        auto & org = avida.GetOrg(org_id);
        if (score_accessor(org)[score_id] + info_epsilon >= max_scores[score_id]) {
          org_profile.Set(org_id);
        }
      }
    }
    if (profile) profile_stats.score_matrix_ms += ElapsedMS(start);
    return score_matrix;
  }

  /// Farthest Point Sampling: fill the first tests_used slots of test_case_ids with the most
  /// mutually-distant test cases (by BitVector Hamming distance on org profiles).
  void InformedDownsample(
    emp::vector<size_t> & test_case_ids,
    const emp::BitVector & org_set,
    const auto & score_accessor,
    size_t tests_used) const
  {
    static emp::vector<size_t> min_distances;  // Smallest distances to current set.

    const auto start = clock_t::now();
    const size_t num_test_cases = test_case_ids.size();
    auto score_matrix = CalcScoreMatrix(score_accessor, org_set);

    // Pick the first test case randomly; seed min_distances with distances from it.
    const size_t first = avida.GetRandom().GetUInt(num_test_cases);
    std::swap(test_case_ids[0], test_case_ids[first]);
    min_distances.resize(num_test_cases);
    for (size_t i = 1; i < num_test_cases; ++i) {
      min_distances[i] = score_matrix[test_case_ids[i]].CalcDistance(score_matrix[test_case_ids[0]]);
    }

    // Repeatedly pick the remaining candidate farthest from the chosen set, then update min_distances.
    for (size_t num_chosen = 1; num_chosen < tests_used; ++num_chosen) {
      // Find the candidate with the largest min-distance (linear scan over remaining).
      size_t best_pos = num_chosen;
      for (size_t i = num_chosen + 1; i < num_test_cases; ++i) {
        if (min_distances[i] > min_distances[best_pos]) best_pos = i;
      }
      std::swap(test_case_ids[num_chosen], test_case_ids[best_pos]);
      std::swap(min_distances[num_chosen], min_distances[best_pos]);

      // One CalcDistance per remaining candidate to update min_distances.
      for (size_t i = num_chosen + 1; i < num_test_cases; ++i) {
        const size_t d =
          score_matrix[test_case_ids[i]].CalcDistance(score_matrix[test_case_ids[num_chosen]]);
        if (d < min_distances[i]) min_distances[i] = d;
      }
    }
    if (profile) profile_stats.informed_sample_ms += ElapsedMS(start);
  }

  [[nodiscard]] std::span<size_t> GetTestCases(
    size_t num_test_cases,
    const emp::BitVector & org_set,
    const auto & score_accessor) const
  {
    if (downsample_frac <= 0.0) emp::notify::Error("Downsample fraction must be positive!");
    if (downsample_frac > 1.0) emp::notify::Error("Downsample fraction must 1.0 or less!");

    static emp::vector<size_t> test_case_ids;

    // If we need to initialize or have changed the number of test cases, update the ID set.
    if (test_case_ids.size() != num_test_cases) {
      test_case_ids.resize(num_test_cases);
      std::iota(test_case_ids.begin(), test_case_ids.end(), 0);
    }

    if (mode != "cohort" && downsample_frac < 1.0) {
      const size_t tests_used = std::max<size_t>(num_test_cases * downsample_frac, 1);
      if (mode == "informed") {
        InformedDownsample(test_case_ids, org_set, score_accessor, tests_used);
      } else {
        emp::Shuffle(avida.GetRandom(), test_case_ids, tests_used);
      }
      return std::span{test_case_ids}.first(tests_used);
    }

    return std::span{test_case_ids};
  }

  void DoSelection(
    const emp::BitVector & parent_bits,
    size_t parent_count,
    const emp::vector<const emp::vector<double> *> & score_cache,
    std::span<size_t> test_case_ids,
    size_t num_test_cases,
    size_t num_rounds = 0)
  {
    if (num_rounds == 0) num_rounds = pop_size;
    emp::vector<FirstPassCacheEntry> first_pass_cache(num_test_cases);

    // Run num_rounds rounds of lexicase selection to generate the next generation.
    for (size_t round = 0; round < num_rounds; ++round) {      
      const size_t parent_id =
        SelectParent(parent_bits, parent_count, score_cache, test_case_ids, first_pass_cache);
      avida.DivideOrg(parent_id);
    }
  }

  void DoCohortSelection(
    emp::BitVector parent_bits,
    const emp::vector<const emp::vector<double> *> & score_cache,
    std::span<size_t> test_case_ids)
  {
    if (downsample_frac > 0.5) {
      emp::notify::Error("Downsample fraction too high for cohort selection");
    }

    const size_t num_orgs = parent_bits.CountOnes();
    const size_t num_cohorts = std::llround(1.0 / downsample_frac);
    const size_t cohort_orgs = std::llround(num_orgs * downsample_frac);
    const size_t cohort_scores = std::llround(test_case_ids.size() * downsample_frac);

    // Shuffle the parents and test cases into cohorts.
    emp::vector<size_t> org_ids;
    org_ids.reserve(parent_bits.CountOnes());
    for (size_t parent_id : parent_bits) org_ids.push_back(parent_id);
    emp::Shuffle(avida.GetRandom(), org_ids);
    emp::Shuffle(avida.GetRandom(), test_case_ids);

    // Select from each cohort.
    for (size_t cohort_id = 0; cohort_id < num_cohorts; ++cohort_id) {
      // Identify parents for this cohort.
      parent_bits.Clear();
      const size_t org_offset = cohort_id * cohort_orgs;
      for (size_t i=0; i < cohort_orgs; ++i) parent_bits.Set(org_ids[i + org_offset]);

      // Do selection in this cohort.
      const size_t cohort_offset = cohort_scores * cohort_id;
      DoSelection(parent_bits,
        cohort_orgs,
        score_cache,
        test_case_ids.subspan(cohort_offset, cohort_scores),
        score_cache[org_ids[org_offset]]->size(),
        cohort_orgs);
    }
  }

  void OnUpdate(size_t update) {
    emp::Timer<"OnUpdate"> fun_timer;
    if (profile) profile_stats.Reset();

    // Look up the typed scores accessor once per update (avoids per-offspring map lookups).
    const auto & score_accessor =
      avida.template GetTypedTrait<emp::vector<double>>(scores_name).GetConstAccessFun();
    const size_t num_test_cases = score_accessor(avida.GetFirstOrg()).size();

    // Collect the parent population.
    const emp::BitVector parent_bits = avida.GetActiveBits();
    const size_t parent_count = parent_bits.CountOnes();
    emp_assert(parent_count > 0);

    if (profile) {
      profile_stats.parent_count = parent_count;
      profile_stats.test_count = num_test_cases;
    }

    emp::vector<const emp::vector<double> *> score_cache(avida.GetBiotaSize(), nullptr);
    for (size_t parent_id : parent_bits) {
      score_cache[parent_id] = &score_accessor(avida.GetOrg(parent_id));
    }

    const auto get_tests_start = clock_t::now();
    std::span<size_t> test_case_ids = GetTestCases(num_test_cases, parent_bits, score_accessor);
    if (profile) {
      profile_stats.get_tests_ms += ElapsedMS(get_tests_start);
      profile_stats.tests_used = test_case_ids.size();
    }

    const auto selection_start = clock_t::now();
    if (mode == "cohort") DoCohortSelection(parent_bits, score_cache, test_case_ids);
    else DoSelection(parent_bits, parent_count, score_cache, test_case_ids, num_test_cases);
    if (profile) profile_stats.selection_ms += ElapsedMS(selection_start);

    // Remove the parent generation now that offspring have been placed.
    const auto delete_start = clock_t::now();
    for (size_t id : parent_bits) avida.DeleteOrg(id);
    if (profile) {
      profile_stats.delete_ms += ElapsedMS(delete_start);
      if (profile_frequency == 0 || update % profile_frequency == 0) PrintProfile(update);
    }
  }

  void OnUpdateEnd(size_t update) {
    if (update % output_frequency == 0) {
      PrintStats(update);
      output.DoOutput();
    }
    if (update >= max_generations) avida.Exit();
  }

};
