#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  Track the current Pareto front in the population.
 */

#include <print>
#include <unordered_set>

#include "emp/base/vector.hpp"
#include "emp/data/DataOutput.hpp"

#include "../core/Avida.hpp"

template <typename DATA_T=emp::vector<double>>
class ParetoFront {
public:
  using data_t = DATA_T;

  enum class AddResult { NOT_ADDED, ADDED_CURRENT, ADDED_GLOBAL };

private:
  struct EntryInfo {
    DATA_T traits;      // Phenotype of this entry.
    size_t gain_gen=0;  // When did this entry first appear?
    size_t loss_gen=0;  // When was this entry last archived?
  };

  emp::vector<EntryInfo> current_only;  // In current front, but dominated historically.
  emp::vector<EntryInfo> global;        // In current front, never dominated.
  emp::vector<EntryInfo> archive;       // Not in current front, never dominated.
  emp::vector<size_t> restore_times;   // Histogram of restoration durations (in updates).

  size_t newly_archived_count = 0;  // Entries moved to archive in the most recent ArchiveAll.
  size_t total_loss_events = 0;     // Cumulative count of archived entries across all updates.
  double epsilon = 0.0;             // Minimum per-trait improvement required for dominance.

  enum class CompareResult { EXACT_MATCH, LEFT_DOMINATES, RIGHT_DOMINATES, NO_DOMINATE };

  CompareResult Compare(const DATA_T & left, const DATA_T & right) {
    emp_assert(left.size() == right.size(), "trait sizes do not match.", left.size(), right.size());
    bool left_any_greater = false, right_any_greater = false;
    for (size_t i = 0; i < left.size(); ++i) {
      if (left[i] > right[i] + epsilon) { left_any_greater = true; break; }
    }
    for (size_t i = 0; i < left.size(); ++i) {
      if (right[i] > left[i] + epsilon) { right_any_greater = true; break; }
    }
    if (!left_any_greater && !right_any_greater) return CompareResult::EXACT_MATCH;
    if ( left_any_greater && !right_any_greater) return CompareResult::LEFT_DOMINATES;
    if (!left_any_greater &&  right_any_greater) return CompareResult::RIGHT_DOMINATES;
    return CompareResult::NO_DOMINATE;
  }

  static void SwapRemove(emp::vector<EntryInfo> & vec, size_t i) {
    vec[i] = std::move(vec.back());
    vec.pop_back();
  }

public:
  void   SetEpsilon(double eps)          { epsilon = eps; }
  double GetEpsilon()              const { return epsilon; }

  size_t GetCurrentSize()        const { return global.size() + current_only.size(); }
  size_t GetGlobalSize()         const { return global.size() + archive.size(); }
  size_t GetLostSize()           const { return archive.size(); }
  size_t GetNewlyArchivedCount() const { return newly_archived_count; }
  size_t GetTotalLossEvents()    const { return total_loss_events; }

  const emp::vector<size_t> & GetRestoreTimes() const { return restore_times; }

  // Add a potential member to the front; return indicates how it was classified.
  AddResult AddEntry(const data_t & trait_set, size_t gen_id) {
    bool can_be_global = true;

    // Check against live global entries: dominated by any->reject; dominates any->remove.
    for (size_t i = 0; i < global.size(); ) {
      switch (Compare(trait_set, global[i].traits)) {
        case CompareResult::EXACT_MATCH:
        case CompareResult::RIGHT_DOMINATES:
          return AddResult::NOT_ADDED;
        case CompareResult::LEFT_DOMINATES:
          SwapRemove(global, i); continue;
        case CompareResult::NO_DOMINATE:
          ++i;
      }
    }

    // Check against live current-only entries: dominated by any->reject; dominates any->remove.
    for (size_t i = 0; i < current_only.size(); ) {
      switch (Compare(trait_set, current_only[i].traits)) {
        case CompareResult::EXACT_MATCH:
        case CompareResult::RIGHT_DOMINATES:
          return AddResult::NOT_ADDED;
        case CompareResult::LEFT_DOMINATES:
          SwapRemove(current_only, i); continue;
        case CompareResult::NO_DOMINATE:
          ++i;
      }
    }

    // Check against archived entries: dominated by any->at most CURRENT_ONLY;
    // exact match->restore to global; dominates any->remove from archive.
    for (size_t i = 0; i < archive.size(); ) {
      switch (Compare(trait_set, archive[i].traits)) {
        case CompareResult::EXACT_MATCH:
          if (archive[i].gain_gen != archive[i].loss_gen) {
            size_t t = gen_id - archive[i].loss_gen;
            if (t >= restore_times.size()) restore_times.resize(t + 1, 0);
            ++restore_times[t];
          }
          archive[i].loss_gen = 0;
          global.push_back(std::move(archive[i]));
          SwapRemove(archive, i);
          return AddResult::ADDED_GLOBAL;  // Restored; new organism represents this global entry.
        case CompareResult::RIGHT_DOMINATES: can_be_global = false; ++i; continue;
        case CompareResult::LEFT_DOMINATES:  SwapRemove(archive, i); continue;
        case CompareResult::NO_DOMINATE:     ++i;
      }
    }

    // New entry survived all comparisons; add it to the appropriate vector.
    if (can_be_global) {
      global.push_back({trait_set, gen_id, 0});
      return AddResult::ADDED_GLOBAL;
    }
    current_only.push_back({trait_set, gen_id, 0});
    return AddResult::ADDED_CURRENT;
  }

  // Archive the current generation's front entries to prepare for the next generation.
  void ArchiveAll(size_t gen_id) {
    newly_archived_count = global.size();
    total_loss_events += newly_archived_count;
    current_only.clear();  // Drop; not globally significant.
    for (auto & entry : global) {
      entry.loss_gen = gen_id;
      archive.push_back(std::move(entry));
    }
    global.clear();
  }
};


template <typename AVIDA_T>
class TrackPareto : public ModuleBase<AVIDA_T> {
private:
  AVIDA_T & avida;
  ParetoFront<emp::vector<double>> pareto_front;

  emp::DataOutput output;
  size_t output_frequency = 100;
  double epsilon = 0.01;

  // Organism IDs on the global front from the previous generation.
  std::unordered_set<size_t> prev_global_front_ids;
  // Organism IDs added to the global front during the current generation.
  std::unordered_set<size_t> current_global_front_ids;
  // Prev-generation front member IDs that reproduced during the current generation.
  std::unordered_set<size_t> reproduced_front_ids;

  // Cumulative loss-reason counters (approximated at organism level).
  // Note: multiple organisms may share a phenotype; counts reflect individual organisms,
  // not unique phenotypes.
  size_t selection_losses = 0;  // Front-member orgs not selected to reproduce.
  size_t mutation_losses = 0;   // Front-member orgs that reproduced (offspring may have mutated).
  size_t loss_updates = 0;      // Updates where at least one global front member was archived.

  void PrintStats() {
    const size_t global_size  = pareto_front.GetGlobalSize();
    const size_t current_size = pareto_front.GetCurrentSize();
    const size_t lost_size    = pareto_front.GetLostSize();
    const double coverage = global_size > 0
      ? static_cast<double>(current_size) / static_cast<double>(global_size) : 1.0;

    std::println(
      "Update: {}; Current front={}; All-time front={}; on front and lost={}; coverage={:.3f} "
      "; Cumulative losses={} (selection={} mutation={})",
      avida.GetUpdate(), current_size, global_size, lost_size, coverage,
      pareto_front.GetTotalLossEvents(), selection_losses, mutation_losses);
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
      "Minimum per-trait improvement required to count as dominance");
  }

  // === Signal Listeners ===

  void BeforeStart() {
    pareto_front.SetEpsilon(epsilon);

    // If we have a filename, set up the columns.
    if (output.GetFilename().size()) {
      output.SetFilepath(avida.GetDataDir());
      output.AddColumn("Update", [this](){ return avida.GetUpdate(); });
      output.AddColumn("Phenotypes count on current front",
        [this](){ return pareto_front.GetCurrentSize(); });
      output.AddColumn("Phenotypes count on all-time front",
        [this](){ return pareto_front.GetGlobalSize(); });
      output.AddColumn("Phenotypes lost and not yet recovered",
        [this](){ return pareto_front.GetLostSize(); });
      output.AddColumn("Known front coverage",
        [this](){
          const double current_size = static_cast<double>(pareto_front.GetCurrentSize());
          const double global_size = static_cast<double>(pareto_front.GetGlobalSize());
          return current_size / static_cast<double>(global_size)+0.0001;
        });
      output.AddColumn("Phenotypes lost ever",
        [this](){ return pareto_front.GetTotalLossEvents(); });
      output.AddColumn("Selection losses", selection_losses);
      output.AddColumn("Mutation losses", mutation_losses);      
    }
  }

  void OnStart() {
    output.DoOutput();
  }

  /// Setup: classify losses from the previous generation, then archive the current front.
  void OnUpdateStart(size_t update) {
    // Classify loss reasons for organisms that were on the global front last generation.
    for (size_t id : prev_global_front_ids) {
      if (reproduced_front_ids.count(id)) ++mutation_losses;
      else                                ++selection_losses;
    }

    // Rotate tracking sets: current->prev, clear current and reproduced.
    prev_global_front_ids = std::move(current_global_front_ids);
    current_global_front_ids.clear();
    reproduced_front_ids.clear();

    // Archive the current global front and record whether any losses occurred.
    pareto_front.ArchiveAll(update);
    if (pareto_front.GetNewlyArchivedCount() > 0) ++loss_updates;
  }

  /// New organisms entering the population are tested against the Pareto front.
  template <concepts::Organism ORG_T>
  void OnPlacement(ORG_T & org) {
    auto result = pareto_front.AddEntry(org.GetPhenotype().trait_values, avida.GetUpdate());
    if (result == ParetoFront<emp::vector<double>>::AddResult::ADDED_GLOBAL) {
      current_global_front_ids.insert(org.GetBiotaID());
    }
  }

  /// Track which global front members reproduced so we can classify loss reasons.
  template <concepts::Organism ORG_T>
  void OnOffspringInit(ORG_T & /*offspring*/, ORG_T & parent) {
    if (prev_global_front_ids.count(parent.GetBiotaID())) {
      reproduced_front_ids.insert(parent.GetBiotaID());
    }
  }

  /// Output Pareto front statistics.
  void OnUpdateEnd(size_t update) {
    if (update % output_frequency == 0) {
      PrintStats();
      output.DoOutput();
    }
  }
};
