/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include "emp/base/Ptr.hpp"

#include "Organism.hpp"

/// A Population is a collection of organisms an a structure of how they are connected.
class Population {
private:
  emp::vector<Organism> orgs{};

public:
  Population() { }

  void Inject(HardwareManager & hw_man, const Genome & genome) {
    orgs.emplace_back(hw_man, genome);
  }

  void Inject(HardwareManager & hw_man, Genome && genome) {
    orgs.emplace_back(hw_man, std::move(genome));
  }

  void Inject(HardwareManager & hw_man, emp::String filename) {
    orgs.emplace_back(hw_man, hw_man.LoadGenome(filename));
  }
};
