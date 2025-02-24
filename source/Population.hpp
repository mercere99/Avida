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
  using org_ptr_t = emp::Ptr<Organism>;
  emp::vector<org_ptr_t> orgs;

public:
  Population(size_t num_orgs=0) : orgs(num_orgs, nullptr) { }
};
