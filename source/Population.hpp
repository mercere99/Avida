/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include "emp/base/Ptr.hpp"
#include "emp/math/Random.hpp"

#include "Organism.hpp"

/// A Population is a collection of organisms an a structure of how they are connected.
class Population {
private:
  emp::vector<Organism> orgs{};
  emp::Random & random;

  size_t max_size = static_cast<size_t>(-1); // Population cap.

  Population & SwapOrgs(size_t id1, size_t id2) {
    if (id1 != id2) std::swap(orgs[id1], orgs[id2]);
    return *this;
  }

  Population & DeleteOrg() {
    emp_assert(orgs.size() > 0);

    // Pick a random organism to remove.
    size_t delete_id = random.GetUInt(orgs.size()); // Choose org to delete.
    orgs[delete_id].SignalDeath();      // Let org know its about to be deleted.
    SwapOrgs(delete_id, orgs.size()-1); // Move last org to replaced deleted.
    orgs.pop_back();                    // Remove org now at end.
    return *this;
  }

  // Organism being added to the population; may be birth or injection.
  Population & Insert(Organism && org) {
    emp_assert(org.OK());
    emp_assert(orgs.size() <= max_size);

    // See if we must delete an organism before adding a new one.
    if (orgs.size() == max_size) DeleteOrg();

    emp_assert(org.OK());
    // std::cout << "BEFORE:    "; org.Print();
    orgs.push_back(std::move(org));
    // std::cout << "\nAFTER OLD: "; org.Print();
    // std::cout << "\nAFTER NEW: "; orgs.back().Print();
    emp_assert(orgs.back().OK());
    orgs.back().SetPopulation(*this);
    return *this;
  }

public:
  Population(emp::Random & random) : random(random) { }

  size_t GetSize() const { return orgs.size(); }
  size_t size() const { return orgs.size(); }

  Population & SetMaxSize(size_t in) { max_size = in; return *this; }

  template <typename T>
  requires std::same_as<std::remove_cvref_t<T>, Genome>
  Population & Inject(HardwareManager & hw_man, T && genome) {
      return Insert(Organism{hw_man, std::forward<T>(genome)});
  }
  
  /// @brief Inject an organism using a genome loaded from a file.
  /// @param hw_man - The HardwareManager for this CPU type.
  /// @param filename - The name of the file with the genome information.
  /// @return A reference to this population object.
  Population & Inject(HardwareManager & hw_man, emp::String filename) {
    return Inject(hw_man, hw_man.LoadGenome(filename));
  }

  Population & DivideOrg(Organism & parent, Genome && offspring_genome) {
    return Insert(Organism{parent, offspring_genome});
  }

  Population & Process(int cycles) {
    emp_assert(orgs.size());
    static constexpr int CPU_CHUNK = 10;
    while (cycles > 0) {
      size_t id = random.GetUInt(orgs.size());
      orgs[id].Process(CPU_CHUNK);
      cycles -= CPU_CHUNK;
    }
    return *this;
  }
};
