#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  This module configures and runs a standard avida population.
 *  - Well-mixed population
 *  - Optional population cap
 */

#include <cstddef>   // for size_t
#include <iostream>

#include "../core/Avida.hpp"

template <typename AVIDA_T>
class PopWellMixed : public ModuleBase<AVIDA_T> {
private:
  using ModuleBase<AVIDA_T>::avida;

  uint32_t pop_cap = 10000;    // Population size limit (default: 10,000 orgs)

  // Buffered drivers reserve all offspring before placing any of them. These organisms are
  // active in Biota, but must not be replacement victims until OnPlacement has run.
  emp::vector<uint8_t> pending_placement;
  size_t pending_count = 0;

  template <concepts::Organism ORG_T>
  void MarkPending(const ORG_T & org) {
    const size_t id = org.GetBiotaID();
    if (pending_placement.size() <= id) pending_placement.resize(id + 1, 0);
    emp_assert(!pending_placement[id]);
    pending_placement[id] = 1;
    ++pending_count;
  }

  // Delete a random (occupied) organism position.
  void DeleteRandomOrg() {
    emp_assert(avida.GetNumOrgs() > 0); // Must have something to delete!
    size_t delete_id;
    do { delete_id = avida.GetRandom().GetUInt32(avida.GetBiotaSize()); }
    while (!avida.IsOccupied(delete_id)
      || (delete_id < pending_placement.size() && pending_placement[delete_id]));
    avida.DeleteOrg(delete_id);    
  }

public:
  PopWellMixed(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>(avida, "PopWellMixed", "PopManager",
        "Manage a well-mixed population") { }
  ~PopWellMixed() { }

  void Serialize(emp::SerialPod & /* pod */) {
    // Pending placement is transient and empty at checkpoint boundaries.
    // The serialized representation remains settings-only.
    emp_assert(pending_count == 0);
  }

  void RegisterSettings() {
    avida.AddSetting("pop.cap", pop_cap, "Maximum number of organisms in the population.");
    avida.GetSettings().Metadata("pop.cap").SetMinimum(1).AddTag("startup only");
  }

  size_t GetOrgReserveCount() const { return pop_cap; }

  [[nodiscard]] bool IsCheckpointSafe() const { return pending_count == 0; }

  void AfterLoad() {
    pending_placement.clear();
    pending_count = 0;
    avida.GetBiota().Reserve(static_cast<size_t>(pop_cap) + 1);
  }

  [[nodiscard]] bool LoadedStateOK() const {
    return pop_cap > 0 && pending_count == 0 && avida.GetNumOrgs() <= pop_cap;
  }

  // === Signal Listeners ===

  void ValidateConfig() {
    if (pop_cap == 0) emp::notify::Error("pop.cap must be positive.");
  }

  template <concepts::Organism ORG_T>
  void OnInjectReady(ORG_T & org) { MarkPending(org); }

  template <concepts::Organism ORG_T>
  void OnOffspringReady(ORG_T & offspring, ORG_T &) { MarkPending(offspring); }

  template <concepts::Organism ORG_T>
  void OnPlacement(ORG_T & org) {
    const size_t id = org.GetBiotaID();
    emp_assert(id < pending_placement.size() && pending_placement[id]);
    pending_placement[id] = 0;
    --pending_count;
  }


  // If we have a population cap, delete organisms rather than let population get overfull.
  template <concepts::Organism ORG_T>
  void BeforePlacement([[maybe_unused]] ORG_T & org) {
    // See if we must delete an organism to make room for the new one.
    if (avida.GetNumOrgs() > pop_cap) DeleteRandomOrg();
  }

};
