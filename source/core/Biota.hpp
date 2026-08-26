#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 * 
 *  The Biota tracks all of the extant organisms in Avida.
 */

#include <cstdint>

#include "emp/base/vector.hpp"
#include "emp/bits/BitVector.hpp"

#include "concepts.hpp"

/// Genome extracted from a parent at division time, waiting for end-of-update placement.
template <typename GENOME_T>
struct PendingOffspring {
  size_t parent_id;
  GENOME_T offspring_genome;
};

template <typename ORGANISM_T>
class Biota {
public:
  using organism_t = ORGANISM_T;
  using genome_t = organism_t::genome_t;

private:
  emp::vector<organism_t> orgs;  // All organisms (some may be inactive)
  emp::BitVector active_bits;    // Bit representation of available orgs (Slower)
  size_t num_orgs = 0;           // Number of currently-living organisms in biota
  size_t total_orgs = 0;         // Number of organisms that have ever existed
  uint64_t epoch = 0;            // Changes whenever population membership/storage changes

public:
  [[nodiscard]] size_t GetSize() const { return orgs.size(); }
  [[nodiscard]] uint32_t GetNumOrgs() const { return num_orgs; }
  [[nodiscard]] bool IsActive(size_t id) const { return active_bits.Get(id); }
  [[nodiscard]] const emp::BitVector & GetActiveBits() const { return active_bits; }
  [[nodiscard]] size_t GetTotalOrgs() const { return total_orgs; }
  [[nodiscard]] uint64_t GetEpoch() const { return epoch; }

  [[nodiscard]] auto & GetOrg(this auto & self, size_t id) {
    emp_assert(self.IsActive(id));
    return self.orgs[id];
  }
  [[nodiscard]] auto & operator[](this auto & self, size_t id) { return self.GetOrg(id); }

  [[nodiscard]] auto & GetOrgs(this auto & self) { return self.orgs; }

  [[nodiscard]] size_t FindFirstActive() const {
    emp_assert(GetNumOrgs() > 0, "No active organisms");
    return active_bits.FindOne();
  }

  [[nodiscard]] emp::vector<size_t> GetActiveIDs() const {
    emp::vector<size_t> active_ids;
    active_ids.reserve(active_bits.CountOnes());
    for (size_t id : active_bits) active_ids.push_back(id);
    return active_ids;
  }

  void Reserve(size_t max_size) {
    orgs.reserve(max_size);
    if (max_size > active_bits.GetSize()) {
      active_bits.Resize(max_size);
      ++epoch;  // Change in number of bits requires updates elsewhere.
    }
  }

  [[nodiscard]] size_t GetCapacity() const { return orgs.capacity(); }

  // Set up a new organism in the Biota, returning a reference to it.
  template <typename GENOME_T>
    requires concepts::Genome<std::remove_cvref_t<GENOME_T>>
  organism_t & ReserveOrganism(GENOME_T && new_genome) {
    size_t index = active_bits.ToggleZero();  // Find empty position in Biota and set it.
    if (index < orgs.size()) {
      orgs[index].SetGenome(std::forward<GENOME_T>(new_genome));
    }
    else if (index < active_bits.GetSize()) {
      emp_assert(index == orgs.size());
      orgs.emplace_back(std::forward<GENOME_T>(new_genome));
      orgs.back().SetBiotaID(index);
    }
    else {
      emp::notify::Error("Trying to reserve more organisms than reserved!");
    }

    organism_t & reserved_org = orgs[index];
    // Reused inactive slots may have transient hardware rebuilt without a location (notably after
    // checkpoint load).  Slot identity is authoritative, so always rebind both organism and
    // hardware IDs before any injection/offspring callbacks run.
    reserved_org.SetBiotaID(index);
    reserved_org.SetGlobalID(total_orgs++);
    ++num_orgs;
    ++epoch;

    return reserved_org;
  }

  // Remove an organism from the Biota.
  void Remove(size_t org_id) {
    emp_assert(IsActive(org_id));
    active_bits.Clear(org_id);
    --num_orgs;
    ++epoch;
  }
  
  void Clear() {
    orgs.clear();
    active_bits.Clear();
    num_orgs = 0;
    ++epoch;
  }

  /// Run each active organism through a provided function.
  void ForEachOrg(auto fun) {
    for (size_t index : active_bits) { fun(orgs[index]); }
  }

  /// Return the biota ID of the organisms with the minimum value in this function.
  template <std::invocable<const organism_t &> FUN_T>
  [[nodiscard]] size_t FindMinimumID(const FUN_T & fun) const {
    double min_found = std::numeric_limits<double>::max();
    size_t min_index = 0;
    for (size_t index : active_bits) {
      double value = fun(orgs[index]);
      if (value < min_found) {
        min_found = value;
        min_index = index;
      }
    }
    return min_index;
  }

  /// Return the biota ID of the organisms with the maximum value in this function.
  template <std::invocable<const organism_t &> FUN_T>
  [[nodiscard]] size_t FindMaximumID(const FUN_T & fun) const {
    double max_found = std::numeric_limits<double>::lowest();
    size_t max_index = 0;
    for (size_t index : active_bits) {
      double value = fun(orgs[index]);
      if (value > max_found) {
        max_found = value;
        max_index = index;
      }
    }
    return max_index;
  }

  /// Return the minimum value in this function across all organisms.
  template <std::invocable<const organism_t &> FUN_T>
  [[nodiscard]] double CalcMinimum(const FUN_T & fun) const {
    double min_found = std::numeric_limits<double>::max();
    for (size_t index : active_bits) {
      min_found = std::min<double>(min_found, fun(orgs[index]));
    }
    return min_found;
  }

  /// Return the maximum value in this function across all organisms.
  template <std::invocable<const organism_t &> FUN_T>
  [[nodiscard]] double CalcMaximum(const FUN_T & fun) const {
    double max_found = std::numeric_limits<double>::lowest();
    for (size_t index : active_bits) {
      max_found = std::max<double>(max_found, fun(orgs[index]));
    }
    return max_found;
  }

  template <std::invocable<const organism_t &> FUN_T>
  [[nodiscard]] double CalcAverage(const FUN_T & fun) const {
    double total = 0.0;
    for (size_t index : active_bits) {
      total += fun(orgs[index]);
    }
    return total / GetNumOrgs();
  }

  template <std::invocable<const organism_t &> FUN_T>
  [[nodiscard]] double CalcSum(const FUN_T & fun) const {
    double total = 0.0;
    for (size_t index : active_bits) {
      total += fun(orgs[index]);
    }
    return total;
  }

  void Serialize(emp::SerialPod & pod) {
    pod(orgs, active_bits, num_orgs, total_orgs);
    if (pod.IsLoad()) {
      for (size_t index = 0; index < orgs.size(); ++index) orgs[index].SetBiotaID(index);
      ++epoch;
    }
  }

  bool OK() {
    for (size_t index : active_bits) if (!orgs[index].OK()) return false;
    return true;
  }
};
