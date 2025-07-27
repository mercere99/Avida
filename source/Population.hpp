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

#include "Organism.hpp"

/// A Population is a collection of organisms an a structure of how they are connected.
class Population {
private:
  emp::vector<Organism> orgs{};
  emp::Random & random;
  emp::UnorderedIndexMap speed_map; // Track how fast each CPU should be going.

  size_t max_size = emp::MAX_SIZE_T; // Population cap.

  int ave_cycles_per_org = 30;  // Total cycles to execute per org per update.
  int CPU_chunk_size = 10;      // Num cycles to execute each time as org is picked.

  // === HELPER FUNCTIONS ===
  template <class Self>
  Self & SwapOrgs(this Self & self, size_t id1, size_t id2) {
    if (id1 != id2) {
      std::swap(self.orgs[id1], self.orgs[id2]);
      self.speed_map.Swap(id1, id2);
    }
    return self;
  }

  template <class Self>
  Self & DeleteOrg(this Self & self) {
    const size_t num_orgs = self.orgs.size()
    emp_assert(num_orgs > 0);

    // Pick a random organism to remove.
    size_t delete_id = self.random.GetUInt(num_orgs); // Choose org to delete.
    self.orgs[delete_id].SignalDeath();               // Signal org prior to deletion.
    self.SwapOrgs(delete_id, num_orgs-1);             // Move last org to replaced deleted.
    self.orgs.pop_back();                             // Remove org now at end.
    return self;
  }

  // Organism being added to the population; may be birth or injection.
  template <class Self>
  Self & Insert(this Self & self, Organism && org) {
    emp_assert(self.org.OK());
    emp_assert(self.orgs.size() <= max_size);

    // See if we must delete an organism before we can add a new one.
    if (self.orgs.size() == self.max_size) self.DeleteOrg();

    uint32_t index = self.orgs.size();
    self.orgs.push_back(std::move(org));
    Organism & new_org = self.orgs.back();
    emp_assert(new_org.OK());
    new_org.SetPopulation(self, index);
    self.speed_map.Set(index, new_org.GetMetabolicRate());
    return self;
  }

public:
  Population(emp::Random & random) : random(random) { }

  size_t GetSize() const { return orgs.size(); }
  size_t size() const { return orgs.size(); }

  template <class Self>
  Self & SetMaxSize(this Self & self, size_t in) { self.max_size = in; return self; }

  double GetAveGeneration() {
    if (orgs.empty()) return 0.0;  // An empty population has no generations.
    size_t total = std::accumulate(orgs.begin(), orgs.end(), 0.0,
                   [](size_t total, Organism & org){ return total + org.GetGeneration(); });
    return total / static_cast<double>(orgs.size());
  }

  // Access organism by population position.
  template <class Self>
  auto & operator[](this Self & self, size_t pos) {
    emp_assert(pos < self.orgs.size());
    return self.orgs[pos];
  }

  template <class Self, typename T>
  requires std::same_as<std::remove_cvref_t<T>, Genome>
  Self & Inject(this Self & self, HardwareManager & hw_man, T && genome) {
    return self.Insert(Organism{hw_man, std::forward<T>(genome)});
  }
  
  /// @brief Inject an organism using a genome loaded from a file.
  /// @param hw_man - The HardwareManager for this CPU type.
  /// @param filename - The name of the file with the genome information.
  /// @return A reference to this population object.
  template <class Self>
  Self & Inject(this Self & self, HardwareManager & hw_man, emp::String filename) {
    return self.Inject(hw_man, hw_man.LoadGenome(filename));
  }

  template <class Self>
  Self & DivideOrg(this Self & self, Organism & parent, Genome && offspring_genome) {
    emp_assert(parent.GetGenome().size() > 0);
    emp_assert(offspring_genome.size() > 0);
    return self.Insert(Organism{parent, offspring_genome});
  }

  // Process a single update for this population, returning the number of CPU cycles used.
  int ProcessUpdate() {    
    emp_assert(orgs.size(), "Running ProcessUpdate() on an empty Population.");
    const int cycles = orgs.size() * ave_cycles_per_org;
    int rounds = cycles / CPU_chunk_size;
    while (--rounds >= 0) {
      const size_t id = speed_map.Index(random.GetDouble(speed_map.GetWeight()));
      orgs[id].Process(CPU_chunk_size);
    }
    return cycles;
  }
};
