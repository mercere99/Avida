#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  Track the Pareto front across generations.
 *
 *  Three collections are maintained:
 *    cur_front - exact Pareto front of the current population
 *    prev_front    - exact Pareto front from the previous generation
 *    archive       - sampled historical front entries that have been lost
 *
 *  OnPlacement feeds organisms into cur_front.
 *  UpdateGen (called each OnUpdateStart) identifies losses, samples into archive,
 *  checks for recoveries, evicts stale archive entries, then rotates the fronts.
 */

#include <print>

#include "emp/base/vector.hpp"
#include "emp/data/DataOutput.hpp"
#include "emp/math/Histogram.hpp"
#include "emp/math/Random.hpp"

#include "../core/Avida.hpp"

template <typename SCORES_T=emp::vector<double>>
class ParetoFront {
private:
  struct FrontEntry {
    SCORES_T score_set;
    size_t insert_gen = 0;  // Generation when this entry was added to front
  };

  emp::vector<FrontEntry> front;
  int add_count = 0;            // Num entries added since last reset
  int remove_count = 0;         // Num entries removed since last reset
  emp::Histogram persist_times; // How long were entries on this front?

  double epsilon = 0.0;         // Amount to beat an existing solution by to remove it

  /// Remove an entry from the specified position; swap the last score_set into place.
  void Remove(size_t i, size_t cur_gen) {
    persist_times.Insert(cur_gen - front[i].insert_gen);
    front[i] = std::move(front.back());
    front.pop_back();
    ++remove_count;
  }

  /// Does the test_entry cover the target?
  bool TestCover(const SCORES_T & test_entry, const SCORES_T & target) const {
    // If test entry ever fails to cover, return false.
    for (size_t i = 0; i < target.size(); ++i) {
      if (test_entry[i] + epsilon < target[i]) { return false; }
    }
    return true;
  }

  ///
  void SortFront() {
    std::sort(front.begin(), front.end(),
      [](const FrontEntry & a, const FrontEntry & b){ return a.insert_gen < b.insert_gen; });
  }

public:
  [[nodiscard]] size_t GetSize() const { return front.size(); }
  [[nodiscard]] const emp::vector<FrontEntry> & GetEntries() const { return front; }
  [[nodiscard]] size_t GetAddCount() const { return add_count; }
  [[nodiscard]] size_t GetRemoveCount() const { return remove_count; }
  [[nodiscard]] const emp::Histogram & GetPersistTimes() const { return persist_times; }
  [[nodiscard]] double GetEpsilon() const { return epsilon; }

  void SetEpsilon(double in) { epsilon = in; }

  // Reset the stats associated with this front.
  void ResetCounts() {
    add_count = 0;
    remove_count = 0;
    persist_times.Reset();
  }

  // Reset this entire front.
  void Reset() {
    front.clear();
    ResetCounts();
  }

  // True if any member of this front dominates or at least matches `score_set`.
  [[nodiscard]] bool IsCovered(const SCORES_T & score_set) const {
    for (const auto & entry : front) {
      if (TestCover(entry.score_set, score_set)) return true;
    }
    return false;
  }

  // Count the number of entries in another front covered by this one.
  [[nodiscard]] size_t CountCovered(const ParetoFront & in) const {
    return std::ranges::count_if(in.front, [this](const auto & entry) {
      return IsCovered(entry);
    });
  }

  // Count the number of entries in another front covered by this one AND REMOVE THEM.
  size_t PruneCovered(const ParetoFront & in, size_t cur_gen) {
    size_t remove_count = 0;
    for (size_t i = 0; i < front.size();) {
      // @CAO If we are removing a bunch from a sorted front, we could do all at once.
      if (in.IsCovered(front[i].score_set)) { Remove(i, cur_gen); ++remove_count; }
      else ++i;
    };
    return remove_count;
  }

  // Remove a specified number of entries from the beginning of front.
  size_t EvictCount(size_t count) {
    emp_assert(count <= front.size());
    front.erase(front.begin(), front.begin() + static_cast<ptrdiff_t>(count));
    return count;
  }

  // Remove entries inserted before a specified generation.
  size_t EvictOlder(size_t min_gen) {
    SortFront();
    size_t evict_count = 0;
    while (evict_count < front.size() && front[evict_count].insert_gen < min_gen) ++evict_count;
    return EvictCount(evict_count);
  }

  // If there are too many entries, remove excess.
  size_t CapSize(size_t max_size) {
    SortFront();
    if (max_size >= front.size()) return 0;
    return EvictCount(front.size() - max_size);
  }


  // Add new score_set if not dominated or matched; return success.
  bool AddEntry(const SCORES_T & test_score_set, size_t cur_gen) {
    for (size_t i = 0; i < front.size(); ) {
      // If test score_set is covered, return false.
      if (TestCover(front[i].score_set, test_score_set)) return false;

      // If it covers an existing entry, remove that entry.
      if (TestCover(test_score_set, front[i].score_set)) Remove(i, cur_gen);
      else ++i;
    }

    // If we made it here, test_score_set should be added to the front.
    front.push_back({test_score_set, cur_gen});
    ++add_count;
    return true;
  }

  void AddFront(const ParetoFront & in, size_t cur_gen) {
    emp_assert(OK());
    ResetCounts();
    for (auto & entry : in.front) AddEntry(entry, cur_gen);
  }

  // No member of this front should dominate any other member.
  bool OK() const {
    for (size_t i = 1; i < front.size(); ++i) {
      for (size_t j = 0; j < i; ++j) {
        if (TestCover(front[i], front[j]) || TestCover(front[j], front[i])) {
          std::println("Error: Pareto front members not independent\nScores 1: {}, Scores 2: {}",
                       front[i], front[j]);
          return false;
        }
      }
    }
    return false;
  }
};

template <typename SCORES_T=emp::vector<double>>
class FrontManager {
private: 
  ParetoFront<SCORES_T> cur_front;   // Exact Pareto front of the current population
  ParetoFront<SCORES_T> prev_front;  // Exact Pareto front from the previous generation
  ParetoFront<SCORES_T> archive;     // Lost entries (possibly sampled); sort by generation lost

  emp::Histogram restore_times;  // Histogram of recovery durations (gen - loss_gen)

  // Config parameters
  double epsilon = 0.0;
  double sample_p = 0.1;        // Probability of archiving each lost entry (0 = disabled)
  size_t max_archive_size = 0;  // 0 = unlimited; oldest entries removed first when exceeded
  size_t max_archive_gen = 0;   // 0 = unlimited; entries older than this many gens are evicted

  // Stats
  size_t lost_count = 0;          // Prev-front entries not covered in cur_front this rotation
  size_t newly_recovered = 0;     // Archive entries recovered this rotation
  size_t total_loss_events = 0;   // Cumulative lost_count across all rotations

public:
  // === Configuration ===
  void SetEpsilon(double e)        { cur_front.SetEpsilon(e); prev_front.SetEpsilon(e); archive.SetEpsilon(e); }
  void SetSampleP(double p)        { sample_p = p; }
  void SetMaxArchiveSize(size_t s) { max_archive_size = s; }
  void SetMaxArchiveGen(size_t g)  { max_archive_gen = g; }

  double GetEpsilon() const { return archive.GetEpsilon(); }
  double GetSampleP() const { return sample_p; }

  // === Statistics ===
  size_t GetCurrentSize()     const { return cur_front.GetSize(); }
  size_t GetPrevSize()        const { return prev_front.GetSize(); }
  size_t GetArchiveSize()     const { return archive.GetSize(); }
  size_t GetLostCount()       const { return lost_count; }
  size_t GetNewlyRecovered()  const { return newly_recovered; }
  size_t GetTotalLossEvents() const { return total_loss_events; }

  const emp::Histogram & GetRestoreTimes() const { return restore_times; }

  // === Core Operations ===

  // Add new entry to the cur_front if not dominated or matched.
  // Returns true if the entry was added to the front.
  bool AddEntry(const SCORES_T & score_set, size_t gen_id) {
    return cur_front.AddEntry(score_set, gen_id);
  }

  // Rotate fronts between generations and update the archive.
  void UpdateGen(size_t gen_id, emp::Random & random) {
    // Archive should be stripped of any entries covered by the current population.
    archive.ResetCounts();
    newly_recovered = archive.PruneCovered(cur_front, gen_id);
    restore_times += archive.GetPersistTimes();

    // Add some non-dominated entries being lost from the previous front.
    prev_front.PruneCovered(cur_front, gen_id);
    lost_count = prev_front.GetSize();
    total_loss_events += lost_count;
    for (auto & entry : prev_front.GetEntries()) {
      if (random.P(sample_p)) archive.AddEntry(entry.score_set, gen_id);
    }

    // Evict from the archive for age or archive size.
    if (max_archive_gen && gen_id > max_archive_gen) archive.EvictOlder(gen_id - max_archive_gen);
    if (max_archive_size) archive.CapSize(max_archive_size);

    // Rotate fronts.
    prev_front = std::move(cur_front);
    cur_front.Reset();
  }
};


template <typename AVIDA_T>
class TrackPareto : public ModuleBase<AVIDA_T> {
private:
  AVIDA_T & avida;
  FrontManager<emp::vector<double>> pareto_front;

  emp::DataOutput output;
  size_t output_frequency = 100;
  double epsilon = 0.01;
  double sample_p = 0.1;
  size_t max_archive_size = 10000;
  size_t max_archive_gen = 0;

  void PrintStats() {
    std::println(
      "Update: {}; Current front={}; Prev front={}; Lost={}; Archive={}; "
      "Recovered={}; Total losses={}",
      avida.GetUpdate(),
      pareto_front.GetCurrentSize(), pareto_front.GetPrevSize(),
      pareto_front.GetLostCount(),   pareto_front.GetArchiveSize(),
      pareto_front.GetNewlyRecovered(), pareto_front.GetTotalLossEvents()
    );
  }

public:
  TrackPareto(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>("TrackPareto", "Analysis", "Track the current (and cumulative) Pareto Front.")
    , avida(avida), output("Pareto.csv") {}
  ~TrackPareto() {}

  void RegisterSettings() {
    avida.AddSetting("TrackPareto.data_filename",
      [this](){ return output.GetFilename(); },
      [this](emp::String in){ output.SetFilename(in); },
      "File to output Pareto data (placed in default data directory)");
    avida.AddSetting("TrackPareto.output_frequency", output_frequency,
      "Updates between Pareto front stat outputs");
    avida.AddSetting("TrackPareto.epsilon", epsilon,
      "Minimum per-score improvement to count as dominance");
    avida.AddSetting("TrackPareto.sample_p", sample_p,
      "Probability of archiving each lost front entry (0 disables archive)");
    avida.AddSetting("TrackPareto.max_archive_size", max_archive_size,
      "Max entries in the archive (0 = unlimited; oldest removed first when exceeded)");
    avida.AddSetting("TrackPareto.max_archive_gen", max_archive_gen,
      "Generations before archive entries expire (0 = unlimited)");
  }

  // === Signal Listeners ===

  void BeforeStart() {
    pareto_front.SetEpsilon(epsilon);
    pareto_front.SetSampleP(sample_p);
    pareto_front.SetMaxArchiveSize(max_archive_size);
    pareto_front.SetMaxArchiveGen(max_archive_gen);

    if (output.GetFilename().size()) {
      output.SetFilepath(avida.GetDataDir());
      output.AddColumn("Update",             [this](){ return avida.GetUpdate(); });
      output.AddColumn("Current front size", [this](){ return pareto_front.GetCurrentSize(); });
      output.AddColumn("Prev front size",    [this](){ return pareto_front.GetPrevSize(); });
      output.AddColumn("Lost this gen",      [this](){ return pareto_front.GetLostCount(); });
      output.AddColumn("Archive size",       [this](){ return pareto_front.GetArchiveSize(); });
      output.AddColumn("Recovered this gen", [this](){ return pareto_front.GetNewlyRecovered(); });
      output.AddColumn("Total loss events",  [this](){ return pareto_front.GetTotalLossEvents(); });
    }
  }

  void OnStart() {
    output.DoOutput();
  }

  void OnUpdateStart(size_t update) {
    pareto_front.UpdateGen(update, avida.GetRandom());
  }

  template <concepts::Organism ORG_T>
  void OnPlacement(ORG_T & org) {
    pareto_front.AddEntry(org.GetPhenotype().trait_values, avida.GetUpdate());
  }

  void OnUpdateEnd(size_t update) {
    if (update % output_frequency == 0) {
      PrintStats();
      output.DoOutput();
    }
  }
};
