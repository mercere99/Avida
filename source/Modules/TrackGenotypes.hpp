/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  Track genotypes that are the same between parents and offspring.
 */

#include <iostream>

#include "emp/datastructs/RobinHoodMap.hpp"

#include "../core/Avida.hpp"

class Genotype {
  uint64_t total_count = 0; // Total number of this genotype that have ever lived.
  uint32_t cur_count = 0;   // Total number of this genotype currently in the population.

public:
  void Birth() { ++total_count; ++cur_count; }
  void Death() { emp_assert(cur_count > 0); --cur_count; }

  [[nodiscard]] uint64_t GetTotalCount() const { return total_count; }
  [[nodiscard]] uint64_t GetCurCount() const { return cur_count; }
};

template <typename AVIDA_T>
class TrackGenotypes : public ModuleBase<AVIDA_T> {
private:
  AVIDA_T & avida;
  uint64_t total_genotypes = 0;  // The total number of genotypes that have ever existed.
  emp::RobinHoodMap<uint64_t, Genotype> id_map; // Map of IDs to current genotypes.

  [[nodiscard]] uint64_t GetCurID() const { return total_genotypes; }
  [[nodiscard]] uint64_t GetNextID() { return ++total_genotypes; }

public:
  TrackGenotypes(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>("TrackGenotypes", "Analysis", "Track identical genomes.")
    , avida(avida) {}
  ~TrackGenotypes() {}

  // === Phenotypic Traits ===

  struct Phenotype {
    uint64_t genotype_id = 0; // Unique ID for the genotype of this organism.
  };

  void RegisterTraits() {
    AVIDA_REGISTER_TRAIT(genotype_id, "Unique ID for group of identical genomes");
  }

  // === Signal Listeners ===

  // When an offspring is born with a mutation, give it a new genotype;
  // otherwise give it the same genotype as its parent.
  template <concepts::Organism ORG_T>
  void OnOffspringReady(ORG_T & offspring, ORG_T & parent) {
    const uint64_t id = (offspring.IsMutated()) ? GetNextID() : parent.GetPhenotype().genotype_id;
    offspring.GetPhenotype().genotype_id = id;
    id_map[id].Birth();
  }

  template <concepts::Organism ORG_T>
  void OnInjectReady(ORG_T & inject_org) {
    inject_org.GetPhenotype().genotype_id = GetNextID();
    id_map[GetCurID()].Birth();
  }

  template <concepts::Organism ORG_T>
  void BeforeDeath([[maybe_unused]] ORG_T & org) {
    const uint64_t id = org.GetPhenotype().genotype_id;
    Genotype & genotype = id_map[id];
    genotype.Death();
    if (genotype.GetCurCount() == 0) id_map.erase(id);
  }

  void OnUpdateEnd(size_t update) {
    if (update % 100 == 0) {
      std::println("{}: Cur Genotypes: {}; Total Genotypes: {}",
        update, id_map.size(), total_genotypes);
    }
  }

};
