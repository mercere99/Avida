#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  Track genotypes that are the same between parents and offspring.
 */

#include <iostream>

#include "emp/base/array.hpp"
#include "emp/bits/BitSet.hpp"
#include "emp/datastructs/RobinHoodMap.hpp"

#include "../core/Avida.hpp"

class Genotype {
private:
  uint64_t id = emp::MAX_8BYTE; // Unique ID for this genotype.
  uint64_t total_count = 0;     // Total number of this genotype that have ever lived.
  uint32_t cur_count = 0;       // Total number of this genotype currently in the population.
  int8_t rank = -1;             // Rank for most abundant genotypes; -1 means no rank.
  int8_t tag_id = -1;           // Tags for most abundant -1 = no tag; 0..N-1 tag id.

public:
  Genotype(uint64_t id=0) : id(id) { }

  void Birth() { ++total_count; ++cur_count; }
  void Death() { emp_assert(cur_count > 0); --cur_count; }

  [[nodiscard]] uint64_t GetID() const { return id; }
  [[nodiscard]] uint64_t GetTotalCount() const { return total_count; }
  [[nodiscard]] uint32_t GetCurCount() const { return cur_count; }
  [[nodiscard]] int8_t   GetRank() const { return rank; }
  [[nodiscard]] int8_t   GetTag() const { return tag_id; }
  [[nodiscard]] bool     HasTag() const { return tag_id >= 0; }

  void SetRank(int8_t in) { rank = in; }
  void SetTag(int8_t in)  { tag_id = in; }
};

template <typename AVIDA_T>
class TrackGenotypes : public ModuleBase<AVIDA_T> {
private:
  AVIDA_T & avida;
  uint64_t total_genotypes = 0;  // Total number of genotypes ever in run.

  emp::RobinHoodMap<uint64_t, Genotype> id_map;  // Genotype ID -> Genotype data.

  static constexpr size_t TAG_ASSIGN_RANK = 10;  // Gain a tag slot when abundant rank.
  static constexpr size_t NUM_TAG_SLOTS = 12;    // Number of distinct tags available.
  static constexpr size_t FINAL_RANK_INDEX = NUM_TAG_SLOTS - 1;
  static_assert(NUM_TAG_SLOTS >= TAG_ASSIGN_RANK,
    "Must have at least as many tags as assignment threshold");

  emp::array<uint64_t, NUM_TAG_SLOTS> rank_ids{};  // IDs of Most abundant genotypes
  emp::BitSet<NUM_TAG_SLOTS> tag_slots_used{};

  [[nodiscard]] uint64_t GetNextID() { return ++total_genotypes; }

  // What is the threshold size for a genotype to get a tag?
  uint32_t TagThreshold() const {
    const uint64_t last_id = rank_ids[TAG_ASSIGN_RANK-1];
    emp_assert(!last_id || id_map.contains(last_id), last_id);
    return last_id ? id_map.Get(last_id).GetCurCount() : 1;
  }

  // Reserve a tag.
  int8_t ReserveTag() {
    const size_t tag_id = tag_slots_used.ToggleZero();
    emp_assert(tag_id != tag_slots_used.npos);
    return static_cast<int8_t>(tag_id);
  }

  // Clear the rank and tag from an existing genotype.
  void ClearRank(Genotype & genotype) {
    if (genotype.GetRank() < 0) return; // Unranked genotype; nothing to clear.
    tag_slots_used.Clear(genotype.GetTag());
    rank_ids[genotype.GetRank()] = 0;
    genotype.SetTag(-1);
    genotype.SetRank(-1);
  }

  void SetRank(Genotype & genotype, int8_t rank) {
    rank_ids[rank] = genotype.GetID();
    genotype.SetRank(rank);
  }

  // Shift a genotype toward rank 0 after its count increased.
  void IncreaseRank(Genotype & genotype) {
    const uint32_t cur_count = genotype.GetCurCount();
    size_t cur_rank = static_cast<size_t>(genotype.GetRank());

    // If genotype does not have a rank, test if it needs one
    if (cur_rank == emp::MAX_SIZE_T) {
      if (cur_count <= TagThreshold()) return; // Still no rank needed.
      uint64_t final_genotype_id = rank_ids[FINAL_RANK_INDEX];
      if (final_genotype_id) ClearRank(id_map.Get(final_genotype_id));
      cur_rank = FINAL_RANK_INDEX;             // Give last rank (for now)
      genotype.SetTag(ReserveTag());
    }

    // Shift up until this genotype is in the correct rank.
    while (cur_rank > 0) {
      const uint64_t prev_id = rank_ids[cur_rank-1];
      if (prev_id) {
        emp_assert(id_map.contains(prev_id), "Invalid genotype in rank_ids");
        Genotype & prev_genotype = id_map.Get(prev_id);
        if (cur_count <= prev_genotype.GetCurCount()) break;
        SetRank(prev_genotype, cur_rank); // Shuffle down.
      }
      --cur_rank;
    }

    SetRank(genotype, cur_rank);
  }

  // Shift a genotype toward the tail after its count decreased.  Returns final rank.
  void DecreaseRank(Genotype & genotype) {
    // If this genotype doesn't have a rank, no need to adjust it.
    if (genotype.GetRank() < 0) return;

    // Shift down until this genotype is in the correct rank.
    const uint32_t cur_count = genotype.GetCurCount();
    size_t cur_rank = static_cast<size_t>(genotype.GetRank());
    while (cur_rank < FINAL_RANK_INDEX) {
      const uint64_t next_id = rank_ids[cur_rank+1];
      if (next_id == 0) break; // No genotypes below this one.
      Genotype & next_genotype = id_map.Get(next_id);
      if (cur_count >= next_genotype.GetCurCount()) break;
      SetRank(next_genotype, cur_rank); // Shuffle down.
      ++cur_rank;
    }

    SetRank(genotype, cur_rank);
    if (cur_count == 0) ClearRank(genotype);
  }

public:
  TrackGenotypes(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>("TrackGenotypes", "Analysis", "Track identical genomes.")
    , avida(avida) {}
  ~TrackGenotypes() {}

  // === Phenotypic Traits ===

  struct Phenotype {
    uint64_t genotype_id = 0;  // Unique ID for the genotype of this organism.
  };

  void RegisterTraits() {
    AVIDA_REGISTER_TRAIT(genotype_id, "Unique ID for group of identical genomes");
  }

  // === Accessors ===

  [[nodiscard]] size_t GetNumGenotypes() const { return id_map.size(); }

  [[nodiscard]] const Genotype & GetGenotype(uint64_t id) const { return id_map.Get(id); }

  // Returns the ID of the kth most abundant genotype (rank 0 = most abundant).
  [[nodiscard]] const Genotype & GetRankedGenotype(size_t rank) const {
    emp_assert(rank < rank_ids.size() && rank_ids[rank] != 0);
    return GetGenotype(rank_ids[rank]);
  }

  uint32_t GetMaxAbundance() {
    if (rank_ids[0] == 0) return 0;
    return GetRankedGenotype(0).GetCurCount();
  }

  template <concepts::Organism ORG_T>
  void AssignGenotype(ORG_T & org, uint64_t genotype_id) {
    org.GetPhenotype().genotype_id = genotype_id;
    if (!id_map.contains(genotype_id)) {
      id_map.Insert(genotype_id, Genotype{genotype_id});
    }
    Genotype & genotype = id_map.Get(genotype_id);  // Get the Genotype object
    genotype.Birth();                               // Indicate increased genotype count
    IncreaseRank(genotype);                         // Adjust for new rank.
  }

  // === Signal Listeners ===

  // When an offspring is born with a mutation, give it a new genotype;
  // otherwise give it the same genotype as its parent.
  template <concepts::Organism ORG_T>
  void OnOffspringReady(ORG_T & offspring, ORG_T & parent) {
    const bool mutated = offspring.IsMutated();
    const uint64_t id = mutated ? GetNextID() : parent.GetPhenotype().genotype_id;
    AssignGenotype(offspring, id);
  }

  template <concepts::Organism ORG_T>
  void OnInjectReady(ORG_T & inject_org) {
    AssignGenotype(inject_org, GetNextID());
  }

  template <concepts::Organism ORG_T>
  void BeforeDeath(ORG_T & org) {
    const uint64_t id = org.GetPhenotype().genotype_id;
    Genotype & genotype = id_map.Get(id);
    genotype.Death();
    DecreaseRank(genotype);                             // Adjust rank if needed.
    if (genotype.GetCurCount() == 0) id_map.erase(id); // Remove extinct genotypes.
  }

  void OnUpdateEnd(size_t update) {
    if (update % 100 == 0) {
      std::println("{}: Cur Genotypes: {}; Total Genotypes: {}; Max Abundance: {}",
        update, id_map.size(), total_genotypes, GetMaxAbundance());
    }
  }

};
