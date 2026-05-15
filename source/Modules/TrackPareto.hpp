#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  Track the current Parento front in the population.
 */

#include <print>

#include "emp/base/vector.hpp"

#include "../core/Avida.hpp"

template <typename DATA_T=emp::vector<double>>
class ParetoFront {
public:
  using data_t = DATA_T;
  enum class EntryType {
    NONE=0,        // Not part of any Pareto front
    CURRENT_ONLY,  // Part of the current Pareto front (but dominated in archive)
    ARCHIVE_ONLY,  // Part of archival Pareto front (but not in current population)
    GLOBAL         // In current population AND never dominated
  };

private:
  struct EntryInfo {
    DATA_T traits;      // Phenotype of this entry.
    size_t gain_gen=0;  // When did this Entry first appear?
    size_t loss_gen=0;  // When was this entry last lost?
    EntryType type = EntryType::NONE;
  };

  emp::vector<EntryInfo> front;

  size_t current_front_size = 0;      // Size of front represented in the population.
  size_t global_front_size = 0;       // Size of front across entire run (ignores current not in global)
  emp::vector<size_t> restore_times;  // Histogram of how long it took to restore lost front members.

  enum class CompareResult { EXACT_MATCH, LEFT_DOMINATES, RIGHT_DOMINATES, NO_DOMINATE };

  CompareResult Compare(const DATA_T & left, const DATA_T & right) {
    emp_assert(left.size() == right.size(), "trait sizes do not match.", left.size(), right.size());
    bool left_any_greater = false, right_any_greater = false;
    for (size_t i = 0; i < left.size(); ++i) {
      if (left[i] > right[i]) { left_any_greater = true; break; }
    }
    for (size_t i = 0; i < left.size(); ++i) {
      if (right[i] > left[i]) { right_any_greater = true; break; }
    }
    if (!left_any_greater && !right_any_greater) return CompareResult::EXACT_MATCH;
    if ( left_any_greater && !right_any_greater) return CompareResult::LEFT_DOMINATES;
    if (!left_any_greater &&  right_any_greater) return CompareResult::RIGHT_DOMINATES;
    return CompareResult::NO_DOMINATE;
  }

public:
  size_t GetCurrentSize() const { return current_front_size; }
  size_t GetGlobalSize()  const { return global_front_size; }
  size_t GetLostSize()    const { return front.size() - current_front_size; }

  // Add a potential member to the front.  Return indicates if added to the current front.
  bool AddEntry(const data_t & trait_set, size_t gen_id) {
    bool can_be_global = true;

    size_t i = 0;
    while (i < front.size()) {
      auto result = Compare(trait_set, front[i].traits);

      if (result == CompareResult::EXACT_MATCH) {
        if (front[i].type == EntryType::ARCHIVE_ONLY) {
          // Restore archived entry to current front.
          front[i].type = EntryType::GLOBAL;
          ++current_front_size;
          if (front[i].gain_gen != front[i].loss_gen) {
            size_t restore_time = gen_id - front[i].loss_gen;
            if (restore_time >= restore_times.size()) restore_times.resize(restore_time + 1, 0);
            ++restore_times[restore_time];
          }
          front[i].loss_gen = 0;
        }
        return false;  // Already in front (or just restored); don't add a duplicate.
      }

      if (result == CompareResult::RIGHT_DOMINATES) {  // Existing entry dominates new.
        if (front[i].type == EntryType::CURRENT_ONLY || front[i].type == EntryType::GLOBAL) {
          return false;  // Dominated by a live entry; new entry has no place in current front.
        }
        // Dominated by an archived entry: new entry can be at most CURRENT_ONLY.
        can_be_global = false;
        ++i;
        continue;
      }

      if (result == CompareResult::LEFT_DOMINATES) {  // New entry dominates existing.
        if (front[i].type == EntryType::CURRENT_ONLY) --current_front_size;
        else if (front[i].type == EntryType::GLOBAL)  { --current_front_size; --global_front_size; }
        front[i] = std::move(front.back());
        front.pop_back();
        continue;  // Don't increment i; swapped-in entry still needs to be checked.
      }

      // NO_DOMINATE: keep searching.
      ++i;
    }

    // New entry survived all comparisons; add it.
    EntryType new_type = can_be_global ? EntryType::GLOBAL : EntryType::CURRENT_ONLY;
    front.push_back({trait_set, gen_id, 0, new_type});
    ++current_front_size;
    if (can_be_global) ++global_front_size;
    return true;
  }

  // At the start of each generation, archive the previous generation's front.
  void ArchiveAll(size_t gen_id) {
    // Compact the front: drop CURRENT_ONLY entries (not globally significant),
    // and transition GLOBAL entries to ARCHIVE_ONLY.
    size_t write = 0;
    for (size_t read = 0; read < front.size(); ++read) {
      if (front[read].type == EntryType::CURRENT_ONLY) {
        --current_front_size;
        continue;  // Drop; not part of global archive.
      }
      if (front[read].type == EntryType::GLOBAL) {
        front[read].type = EntryType::ARCHIVE_ONLY;
        front[read].loss_gen = gen_id;
        --current_front_size;
      }
      if (write != read) front[write] = std::move(front[read]);
      ++write;
    }
    front.resize(write);
  }

  const emp::vector<size_t> & GetRestoreTimes() const { return restore_times; }
};


template <typename AVIDA_T>
class TrackPareto : public ModuleBase<AVIDA_T> {
private:
  AVIDA_T & avida;
  ParetoFront<emp::vector<double>> pareto_front;

  size_t current_update = 0;
  size_t output_frequency = 100;

  void PrintStats(size_t update) {
    std::println("Update: {} ; Pareto(current={} global={} lost={})",
      update,
      pareto_front.GetCurrentSize(),
      pareto_front.GetGlobalSize(),
      pareto_front.GetLostSize());
  }

public:
  TrackPareto(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>("TrackPareto", "Analysis", "Track the current (and cumulative) Pareto Front.")
    , avida(avida) {}
  ~TrackPareto() {}

  void RegisterSettings() {
    avida.AddSetting("TrackPareto.output_frequency", output_frequency,
      "Updates between Pareto front stat outputs");
  }

  // === Signal Listeners ===

  /// New organisms entering the population are tested against the Pareto front.
  template <concepts::Organism ORG_T>
  void OnPlacement(ORG_T & org) {
    pareto_front.AddEntry(org.GetPhenotype().trait_values, current_update);
  }

  /// At the start of each update, archive the previous generation's front entries.
  void OnUpdateStart(size_t update) {
    current_update = update;
    pareto_front.ArchiveAll(update);
  }

  /// Periodically output Pareto front statistics.
  void OnUpdateEnd(size_t update) {
    if (update % output_frequency == 0) PrintStats(update);
  }
};
