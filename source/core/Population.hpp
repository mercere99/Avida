#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include <numeric>

#include "emp/base/Ptr.hpp"
#include "emp/datastructs/UnorderedIndexMap.hpp"
#include "emp/math/Random.hpp"

/// A Population is a collection of organisms and a structure of how they are connected.
template <typename ORG_T>
class Population {
public:
  using organism_t = ORG_T;
  using genome_t = organism_t::genome_t;
private:
  emp::vector<organism_t> orgs{};
  emp::Random & random;
  emp::UnorderedIndexMap speed_map; // Track how fast each CPU should be going.

  size_t max_size = emp::MAX_SIZE_T; // Population cap.

  static constexpr int ave_cycles_per_org = 30;  // Total cycles to execute per org per update.
  static constexpr int CPU_chunk_size = 10;      // Num cycles to execute each time as org is picked.

  // Basic stats
  int64_t cycles_executed = 0;  // How many CPU cycles have been run so far?

  // === HELPER FUNCTIONS ===
  template <class Self>
  Self & SwapOrgs(this Self & self, size_t id1, size_t id2) {
    if (id1 != id2) {
      std::swap(self.orgs[id1], self.orgs[id2]);
      self.speed_map.Swap(id1, id2);
    }
    return self;
  }

  // Delete a the organism at a specific position from the population.
  template <class Self>
  Self & DeleteOrg(this Self & self, size_t delete_id) {
    emp_assert(delete_id < self.orgs.size());

    self.orgs[delete_id].SignalDeath();            // Signal org prior to deletion.
    self.SwapOrgs(delete_id, self.orgs.size()-1);  // Move last org to replace deleted.
    self.orgs.pop_back();                          // Remove org now at end.
    return self;
  }

  // Delete a random organism from the population.
  template <class Self>
  Self & DeleteOrg(this Self & self) {
    emp_assert(self.orgs.size() > 0);

    // Pick a random organism to delete.
    return self.DeleteOrg( self.random.GetUInt(self.orgs.size()) );
  }

  // Organism being added to the population; may be birth or injection.
  template <class Self>
  Self & Insert(this Self & self, organism_t && org) {
    emp_assert(org.OK());
    emp_assert(self.orgs.size() <= self.max_size);

    // See if we must delete an organism before we can add a new one.
    if (self.orgs.size() == self.max_size) self.DeleteOrg();

    uint32_t index = self.orgs.size();
    self.orgs.push_back(std::move(org));
    organism_t & new_org = self.orgs.back();
    emp_assert(new_org.OK());
    new_org.SetPosition({self, index});
    self.speed_map.Set(index, new_org.GetMetabolicRate());
    return self;
  }

public:
  Population(emp::Random & random) : random(random) { }

  [[nodiscard]] size_t GetSize() const { return orgs.size(); }
  [[nodiscard]] size_t size() const { return orgs.size(); } // For std compatibility.
  [[nodiscard]] size_t GetNumOrgs() const { return orgs.size(); }

  [[nodiscard]] size_t GetMaxSize() const { return max_size; }
  template <class Self>
  Self & SetMaxSize(this Self & self, size_t in) {
    self.max_size = in;
    return self;
  }

  [[nodiscard]] int64_t GetCyclesExecuted() const { return cycles_executed; }

  template <typename FUN_T>
  double GetAveTrait(const FUN_T & fun) {
    if (orgs.empty()) return 0.0;  // An empty population has no generations.
    double total = std::accumulate(orgs.begin(), orgs.end(), 0.0,
                   [fun](double total, organism_t & org){ return total + fun(org); });
    return total / static_cast<double>(orgs.size());
  }

  double GetAveGeneration() {
    return GetAveTrait([](organism_t & org){ return org.GetGeneration(); });
  }

  // Access organism by population position.
  template <class Self>
  auto & operator[](this Self & self, size_t pos) {
    emp_assert(pos < self.orgs.size());
    return self.orgs[pos];
  }

  template <class Self, typename HW_MANAGER_T>
  Self & Inject(this Self & self, HW_MANAGER_T & hw_man, genome_t && genome) {
    return self.Insert(organism_t{hw_man, std::move(genome)});
  }
  
  /// @brief Inject an organism using a genome loaded from a file.
  /// @param hw_man - The hardware manager for this CPU type.
  /// @param filename - The name of the file with the genome information.
  /// @return A reference to this population object.
  template <class Self, typename HW_MANAGER_T>
  Self & Inject(this Self & self, HW_MANAGER_T & hw_man, emp::String filename) {
    auto exp_genome = hw_man.LoadGenome(filename);
    if (!exp_genome) {
      emp::notify::Error("Failed to inject from file '", filename, "'.");
      return self;
    }

    return self.Inject(hw_man, std::move(*exp_genome));
  }

  template <class Self>
  Self & DivideOrg(this Self & self, organism_t & parent, genome_t && offspring_genome) {
    emp_assert(parent.GetGenome().size() > 0);
    emp_assert(offspring_genome.size() > 0);
    return self.Insert(organism_t{parent, std::move(offspring_genome)});
  }

  // Process a single update for this population, returning the number of CPU cycles used.
  void ProcessUpdate() {    
    emp_assert(orgs.size(), "Running ProcessUpdate() on an empty Population.");
    const int cycles = std::ssize(orgs) * ave_cycles_per_org;
    for (int rounds = cycles / CPU_chunk_size; rounds; --rounds) {
      const size_t id = speed_map.Index(random.GetDouble(speed_map.GetWeight()));
      orgs[id].Process(CPU_chunk_size);
    }
    cycles_executed += cycles;
  }
};
