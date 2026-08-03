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
 *  OnUpdateStart rotates the completed current front into prev_front, and OnPlacement feeds the
 *  new population into cur_front.  OnUpdateEnd then identifies losses, samples into the archive,
 *  checks for recoveries, and evicts stale archive entries before reporting the aligned fronts.
 */

#include <cmath>
#include <print>
#include <unordered_map>

#include "emp/base/vector.hpp"
#include "emp/data/DataOutput.hpp"
#include "emp/datastructs/hash_utils.hpp"
#include "emp/math/Histogram.hpp"
#include "emp/math/Random.hpp"

#include "../core/Avida.hpp"

template <typename SCORES_T=emp::Vector<double>>
class ParetoFront {
private:
  struct FrontEntry {
    SCORES_T score_set;
    size_t insert_gen = 0;  // Generation when this entry was added to front

    void Serialize(emp::SerialPod & pod) {
      pod(score_set, insert_gen);
    }
  };

  emp::vector<FrontEntry> front;
  int add_count = 0;            // Num entries added since last reset
  int remove_count = 0;         // Num entries removed since last reset
  emp::Histogram persist_times; // How long were entries on this front?

  /// Remove an entry from the specified position; swap the last score_set into place.
  void Remove(size_t i, size_t cur_gen) {
    persist_times.Insert(cur_gen - front[i].insert_gen);
    front[i] = std::move(front.back());
    front.pop_back();
    ++remove_count;
  }

  /// Does the test_entry cover the target?
  bool TestCover(const SCORES_T & test_entry, const SCORES_T & target) const {
    emp_assert(test_entry.size() == target.size());
    // If test entry ever fails to cover, return false.
    for (size_t i = 0; i < target.size(); ++i) {
      if (test_entry[i] < target[i]) { return false; }
    }
    return true;
  }

  ///
  void SortFront() {
    std::sort(front.begin(), front.end(),
      [](const FrontEntry & a, const FrontEntry & b){ return a.insert_gen < b.insert_gen; });
  }

public:
  void Serialize(emp::SerialPod & pod) {
    pod(front, add_count, remove_count, persist_times);
  }

  [[nodiscard]] size_t GetSize() const { return front.size(); }
  [[nodiscard]] const emp::vector<FrontEntry> & GetEntries() const { return front; }
  [[nodiscard]] size_t GetAddCount() const { return add_count; }
  [[nodiscard]] size_t GetRemoveCount() const { return remove_count; }
  [[nodiscard]] const emp::Histogram & GetPersistTimes() const { return persist_times; }
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
      return IsCovered(entry.score_set);
    });
  }

  // Count the number of entries in another front covered by this one AND REMOVE THEM.
  size_t PruneCovered(const ParetoFront & in, size_t cur_gen) {
    size_t remove_count = 0;
    for (size_t i = 0; i < front.size();) {
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
    for (auto & entry : in.front) AddEntry(entry.score_set, cur_gen);
  }

  // No member of this front should dominate any other member.
  bool OK() const {
    for (size_t i = 1; i < front.size(); ++i) {
      for (size_t j = 0; j < i; ++j) {
        if (TestCover(front[i].score_set, front[j].score_set) ||
            TestCover(front[j].score_set, front[i].score_set)) {
          std::println("Error: Pareto front members not independent\nScores 1: {}, Scores 2: {}",
                       front[i].score_set, front[j].score_set);
          return false;
        }
      }
    }
    return true;
  }
};

template <typename SCORES_T=emp::Vector<double>>
class FrontManager {
private: 
  ParetoFront<SCORES_T> cur_front;   // Exact Pareto front of the current population
  ParetoFront<SCORES_T> prev_front;  // Exact Pareto front from the previous generation
  SCORES_T cur_best_scores;          // Best population-wide value observed on each trait
  SCORES_T prev_best_scores;         // Per-trait best values from the previous generation

  enum class LossCause { WITHOUT_OFFSPRING, AFTER_REPRODUCTION };

  struct LossRecord {
    SCORES_T score_set;
    size_t loss_gen = 0;
    LossCause cause = LossCause::WITHOUT_OFFSPRING;

    void Serialize(emp::SerialPod & pod) { pod(score_set, loss_gen, cause); }
  };

  // Independently sampled loss events, retained until recovery or expiration.
  emp::vector<LossRecord> archive;

  emp::Histogram restore_times;  // Histogram of recovery durations (gen - loss_gen)

  // Config parameters
  double resolution = 0.0;     // Width of fixed score bins (0 = exact scores)
  double sample_p = 0.1;        // Probability of archiving each lost entry (0 = disabled)
  size_t max_archive_size = 0;  // 0 = unlimited; oldest entries removed first when exceeded
  size_t max_archive_gen = 0;   // 0 = unlimited; entries older than this many gens are evicted

  // Stats
  size_t lost_count = 0;          // Prev-front entries not covered in cur_front this rotation
  size_t front_members_at_risk = 0;
  size_t lost_without_offspring = 0;
  size_t lost_after_reproduction = 0;
  size_t sampled_losses = 0;
  size_t best_trait_scores_at_risk = 0;
  size_t best_trait_scores_lost = 0;
  size_t newly_recovered = 0;     // Archive entries recovered this rotation
  size_t newly_expired = 0;       // Archive entries that reached the age horizon this rotation
  size_t newly_cap_evicted = 0;   // Oldest archive entries removed by the emergency size cap
  size_t total_front_member_opportunities = 0;
  size_t total_loss_events = 0;   // Cumulative lost_count across all rotations
  size_t total_lost_without_offspring = 0;
  size_t total_lost_after_reproduction = 0;
  size_t total_sampled_losses = 0;
  size_t total_best_trait_score_opportunities = 0;
  size_t total_best_trait_score_losses = 0;
  size_t total_recovery_events = 0;
  size_t total_expiration_events = 0;
  size_t total_cap_evictions = 0;

public:
  void Serialize(emp::SerialPod & pod ) {
    pod(cur_front, prev_front, cur_best_scores, prev_best_scores, archive, restore_times,
        resolution, sample_p, max_archive_size, max_archive_gen, lost_count, front_members_at_risk,
        lost_without_offspring, lost_after_reproduction, sampled_losses,
        best_trait_scores_at_risk, best_trait_scores_lost, newly_recovered, newly_expired,
        newly_cap_evicted, total_front_member_opportunities, total_loss_events,
        total_lost_without_offspring, total_lost_after_reproduction, total_sampled_losses,
        total_best_trait_score_opportunities, total_best_trait_score_losses,
        total_recovery_events, total_expiration_events, total_cap_evictions);
  }

// === Configuration ===
  void SetResolution(double r)     { resolution = r; }
  void SetSampleP(double p)        { sample_p = p; }
  void SetMaxArchiveSize(size_t s) { max_archive_size = s; }
  void SetMaxArchiveGen(size_t g)  { max_archive_gen = g; }

  double GetResolution() const { return resolution; }
  double GetSampleP() const { return sample_p; }

  // Map scores onto a fixed grid so that approximate equality is transitive.  Store bin IDs
  // rather than rounded floating-point values; exact Pareto comparisons are then performed on
  // these IDs.  The grid is anchored at zero and floor() gives the expected behavior for both
  // positive and negative scores.
  [[nodiscard]] SCORES_T BinScores(const SCORES_T & score_set) const {
    if (resolution == 0.0) return score_set;
    SCORES_T result = score_set;
    for (auto & score : result) score = std::floor(score / resolution);
    return result;
  }

  // === Statistics ===
  size_t GetCurrentSize()     const { return cur_front.GetSize(); }
  size_t GetPrevSize()        const { return prev_front.GetSize(); }
  size_t GetArchiveSize()     const { return archive.size(); }
  size_t GetLostCount()       const { return lost_count; }
  size_t GetFrontMembersAtRisk() const { return front_members_at_risk; }
  size_t GetLostWithoutOffspring() const { return lost_without_offspring; }
  size_t GetLostAfterReproduction() const { return lost_after_reproduction; }
  size_t GetSampledLosses() const { return sampled_losses; }
  size_t GetBestTraitScoresAtRisk() const { return best_trait_scores_at_risk; }
  size_t GetBestTraitScoresLost() const { return best_trait_scores_lost; }
  size_t GetNewlyRecovered()  const { return newly_recovered; }
  size_t GetNewlyExpired() const { return newly_expired; }
  size_t GetNewlyCapEvicted() const { return newly_cap_evicted; }
  size_t GetTotalFrontMemberOpportunities() const { return total_front_member_opportunities; }
  size_t GetTotalLossEvents() const { return total_loss_events; }
  size_t GetTotalLostWithoutOffspring() const { return total_lost_without_offspring; }
  size_t GetTotalLostAfterReproduction() const { return total_lost_after_reproduction; }
  size_t GetTotalSampledLosses() const { return total_sampled_losses; }
  size_t GetTotalBestTraitScoreOpportunities() const {
    return total_best_trait_score_opportunities;
  }
  size_t GetTotalBestTraitScoreLosses() const { return total_best_trait_score_losses; }
  size_t GetTotalRecoveryEvents() const { return total_recovery_events; }
  size_t GetTotalExpirationEvents() const { return total_expiration_events; }
  size_t GetTotalCapEvictions() const { return total_cap_evictions; }

  const emp::Histogram & GetRestoreTimes() const { return restore_times; }

  // === Core Operations ===

  // Add new entry to the cur_front if not dominated or matched.
  // Returns true if the entry was added to the front.
  bool AddEntry(const SCORES_T & score_set, size_t gen_id) {
    if (cur_best_scores.empty()) cur_best_scores = score_set;
    else {
      emp_assert(cur_best_scores.size() == score_set.size());
      for (size_t i = 0; i < score_set.size(); ++i) {
        if (score_set[i] > cur_best_scores[i]) cur_best_scores[i] = score_set[i];
      }
    }
    return cur_front.AddEntry(BinScores(score_set), gen_id);
  }

  // Preserve the completed population as the previous front and prepare to collect its offspring.
  void BeginGen() {
    prev_front = std::move(cur_front);
    cur_front.Reset();
    prev_best_scores = std::move(cur_best_scores);
    cur_best_scores.clear();
  }

  // Compare the completed current population with its parent population and update the archive.
  template <typename REPRO_COUNTS_T>
  void FinalizeGen(
    size_t gen_id,
    emp::Random & random,
    const REPRO_COUNTS_T & reproduction_counts
  ) {
    sampled_losses = 0;
    newly_recovered = 0;
    newly_expired = 0;
    newly_cap_evicted = 0;

    // A trait's best score is retained if the current population matches or improves upon the
    // previous maximum.  This is exact and intentionally independent of Pareto score binning.
    best_trait_scores_at_risk = prev_best_scores.size();
    best_trait_scores_lost = 0;
    if (!prev_best_scores.empty()) {
      emp_assert(cur_best_scores.size() == prev_best_scores.size());
      for (size_t i = 0; i < prev_best_scores.size(); ++i) {
        if (cur_best_scores[i] < prev_best_scores[i]) ++best_trait_scores_lost;
      }
    }
    total_best_trait_score_opportunities += best_trait_scores_at_risk;
    total_best_trait_score_losses += best_trait_scores_lost;
    emp_assert(total_best_trait_score_losses <= total_best_trait_score_opportunities);

    // Resolve sampled loss events independently.  Stable compaction preserves loss-generation
    // order, allowing an emergency size cap to remove the oldest records without sorting.
    size_t write_pos = 0;
    for (size_t read_pos = 0; read_pos < archive.size(); ++read_pos) {
      auto & record = archive[read_pos];
      if (cur_front.IsCovered(record.score_set)) {
        ++newly_recovered;
        ++total_recovery_events;
        restore_times.Insert(gen_id - record.loss_gen);
      } else if (max_archive_gen && gen_id - record.loss_gen > max_archive_gen) {
        ++newly_expired;
        ++total_expiration_events;
      } else {
        if (write_pos != read_pos) archive[write_pos] = std::move(record);
        ++write_pos;
      }
    }
    archive.resize(write_pos);

    // Identify lost entries and classify them using reproduction by all carriers of the same
    // score bin.  Keep prev_front intact so the reported previous-front size is the denominator
    // associated with these losses.
    front_members_at_risk = prev_front.GetSize();
    total_front_member_opportunities += front_members_at_risk;
    lost_count = 0;
    lost_without_offspring = 0;
    lost_after_reproduction = 0;

    // Reproduction is recorded by exact phenotype while offspring are being generated.  Collapse
    // those counts into the same score bins used by the front once per distinct parent phenotype,
    // rather than re-binning a parent on every reproduction event.
    std::unordered_map<SCORES_T, size_t, emp::ContainerHash<SCORES_T>> binned_reproduction_counts;
    for (const auto & [score_set, count] : reproduction_counts) {
      binned_reproduction_counts[BinScores(score_set)] += count;
    }

    for (const auto & entry : prev_front.GetEntries()) {
      if (cur_front.IsCovered(entry.score_set)) continue;

      ++lost_count;
      const auto found = binned_reproduction_counts.find(entry.score_set);
      const bool reproduced =
        found != binned_reproduction_counts.end() && found->second > 0;
      const LossCause cause = reproduced
        ? LossCause::AFTER_REPRODUCTION
        : LossCause::WITHOUT_OFFSPRING;

      if (reproduced) ++lost_after_reproduction;
      else ++lost_without_offspring;

      if (random.P(sample_p)) {
        archive.push_back({entry.score_set, gen_id, cause});
        ++sampled_losses;
      }
    }
    total_loss_events += lost_count;
    total_lost_without_offspring += lost_without_offspring;
    total_lost_after_reproduction += lost_after_reproduction;
    total_sampled_losses += sampled_losses;

    emp_assert(front_members_at_risk == prev_front.GetSize());
    emp_assert(lost_count <= front_members_at_risk);
    emp_assert(lost_without_offspring + lost_after_reproduction == lost_count);
    emp_assert(total_lost_without_offspring + total_lost_after_reproduction == total_loss_events);

    // Preserve the legacy hard cap as an emergency bound, but report all resulting censoring.
    if (max_archive_size && archive.size() > max_archive_size) {
      newly_cap_evicted = archive.size() - max_archive_size;
      archive.erase(archive.begin(), archive.begin() + static_cast<ptrdiff_t>(newly_cap_evicted));
      total_cap_evictions += newly_cap_evicted;
    }

    emp_assert(
      total_sampled_losses
        == total_recovery_events + total_expiration_events + total_cap_evictions + archive.size()
    );

  }
};


template <typename AVIDA_T>
class TrackPareto : public ModuleBase<AVIDA_T> {
private:
  using ModuleBase<AVIDA_T>::avida;
  using score_t = emp::Vector<double>;
  using reproduction_counts_t = std::unordered_map<
    score_t,
    size_t,
    emp::ContainerHash<score_t>
  >;

  FrontManager<score_t> pareto_front;
  reproduction_counts_t reproduction_counts;

  emp::DataOutput output;
  size_t output_frequency = 100;
  double resolution = 0.01;
  double sample_p = 0.1;
  size_t max_archive_size = 10000;
  size_t max_archive_gen = 0;

  void PrintStats() {
    std::println(
      "Update: {}; Current front={}; Prev front={}; Lost={}; Archive={}; "
      "No offspring={}; After reproduction={}; Trait bests lost={}; "
      "Recovered={}; Expired={}; Total losses={}",
      avida.GetUpdate(),
      pareto_front.GetCurrentSize(), pareto_front.GetPrevSize(),
      pareto_front.GetLostCount(),   pareto_front.GetArchiveSize(),
      pareto_front.GetLostWithoutOffspring(), pareto_front.GetLostAfterReproduction(),
      pareto_front.GetBestTraitScoresLost(),
      pareto_front.GetNewlyRecovered(), pareto_front.GetNewlyExpired(),
      pareto_front.GetTotalLossEvents()
    );
  }

public:
  TrackPareto(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>(avida, "TrackPareto", "Analysis",
        "Track the current (and cumulative) Pareto Front.")
    , output("Pareto.csv") {}
  ~TrackPareto() {}

  void Serialize(emp::SerialPod & pod ) {
    pod(pareto_front, reproduction_counts);
  }

  void RegisterSettings() {
    avida.AddSetting("TrackPareto.data_filename",
      [this](){ return output.GetFilename(); },
      [this](emp::String in){ output.SetFilename(in); },
      "File to output Pareto data (placed in default data directory)");
    avida.AddSetting("TrackPareto.output_frequency", output_frequency,
      "Updates between Pareto front stat outputs");
    avida.AddSetting("TrackPareto.resolution", resolution,
      "Width of fixed score bins for Pareto comparisons (0 = exact scores)");
    avida.AddSetting("TrackPareto.sample_p", sample_p,
      "Probability of archiving each lost front entry (0 disables archive)");
    avida.AddSetting("TrackPareto.max_archive_size", max_archive_size,
      "Max entries in the archive (0 = unlimited; oldest removed first when exceeded)");
    avida.AddSetting("TrackPareto.max_archive_gen", max_archive_gen,
      "Generations before archive entries expire (0 = unlimited)");
  }

  // === Signal Listeners ===

  void ValidateConfig() {
    if (!std::isfinite(resolution) || resolution < 0.0) {
      emp::notify::Error(
        "TrackPareto.resolution must be finite and non-negative; received ", resolution, "."
      );
    }
  }

  void BeforeStart() {
    pareto_front.SetResolution(resolution);
    pareto_front.SetSampleP(sample_p);
    pareto_front.SetMaxArchiveSize(max_archive_size);
    pareto_front.SetMaxArchiveGen(max_archive_gen);

    if (output.GetFilename().size()) {
      output.SetFilepath(avida.GetDataDir());
      output.AddColumn("Update",             [this](){ return avida.GetUpdate(); });
      output.AddColumn("Current front size", [this](){ return pareto_front.GetCurrentSize(); });
      output.AddColumn("Prev front size (U-1)",    [this](){ return pareto_front.GetPrevSize(); });
      output.AddColumn("Front members at risk (U-1)",
        [this](){ return pareto_front.GetFrontMembersAtRisk(); });
      output.AddColumn("Lost this gen",      [this](){ return pareto_front.GetLostCount(); });
      output.AddColumn("Lost without offspring",
        [this](){ return pareto_front.GetLostWithoutOffspring(); });
      output.AddColumn("Lost after reproduction",
        [this](){ return pareto_front.GetLostAfterReproduction(); });
      output.AddColumn("Best trait scores at risk",
        [this](){ return pareto_front.GetBestTraitScoresAtRisk(); });
      output.AddColumn("Best trait scores lost",
        [this](){ return pareto_front.GetBestTraitScoresLost(); });
      output.AddColumn("Sampled losses",     [this](){ return pareto_front.GetSampledLosses(); });
      output.AddColumn("Archive size",       [this](){ return pareto_front.GetArchiveSize(); });
      output.AddColumn("Recovered this gen", [this](){ return pareto_front.GetNewlyRecovered(); });
      output.AddColumn("Expired this gen",   [this](){ return pareto_front.GetNewlyExpired(); });
      output.AddColumn("Evicted by cap",     [this](){ return pareto_front.GetNewlyCapEvicted(); });
      output.AddColumn("Total front member opportunities",
        [this](){ return pareto_front.GetTotalFrontMemberOpportunities(); });
      output.AddColumn("Total loss events",  [this](){ return pareto_front.GetTotalLossEvents(); });
      output.AddColumn("Total lost without offspring",
        [this](){ return pareto_front.GetTotalLostWithoutOffspring(); });
      output.AddColumn("Total lost after reproduction",
        [this](){ return pareto_front.GetTotalLostAfterReproduction(); });
      output.AddColumn("Total best trait score opportunities",
        [this](){ return pareto_front.GetTotalBestTraitScoreOpportunities(); });
      output.AddColumn("Total best trait score losses",
        [this](){ return pareto_front.GetTotalBestTraitScoreLosses(); });
      output.AddColumn("Total sampled losses",
        [this](){ return pareto_front.GetTotalSampledLosses(); });
      output.AddColumn("Total sampled recoveries",
        [this](){ return pareto_front.GetTotalRecoveryEvents(); });
      output.AddColumn("Total sampled expirations",
        [this](){ return pareto_front.GetTotalExpirationEvents(); });
      output.AddColumn("Total cap evictions",
        [this](){ return pareto_front.GetTotalCapEvictions(); });
    }
  }

  template <concepts::Organism ORG_T>
  void OnPlacement(ORG_T & org) {
    pareto_front.AddEntry(org.GetPhenotype().trait_values, avida.GetUpdate());
  }

  template <concepts::Organism ORG_T>
  void OnOffspringReady(ORG_T & /* offspring */, ORG_T & parent) {
    ++reproduction_counts[parent.GetPhenotype().trait_values];
  }

  void OnPopulationReady() {
    output.DoOutput();
  }

  void OnUpdateStart(size_t /* update */) {
    emp_assert(reproduction_counts.empty());
    pareto_front.BeginGen();
  }

  void OnUpdateEnd(size_t update) {
    pareto_front.FinalizeGen(update, avida.GetAnalyzeRandom(), reproduction_counts);
    reproduction_counts.clear();
    if (update % output_frequency == 0) {
      PrintStats();
      output.DoOutput();
    }
  }
};
