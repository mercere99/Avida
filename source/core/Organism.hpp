#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include <cstddef>  // for size_t
#include <cstdint>  // for uint64_t and uint32_t
#include <iostream>

#include "emp/base/Ptr.hpp"
#include "emp/tools/String.hpp"

#include "OrganismBase.hpp"
#include "../Hardware/HardwareManager.hpp"

template <typename HW_T, typename PHENOTYPE_T>
class Organism : public OrganismBase {
public:
  using this_t = Organism<HW_T, PHENOTYPE_T>;
  using hardware_t = HW_T;
  using genome_t = typename HW_T::genome_t;
  using manager_t = HardwareManager<HW_T>;
  using phenotype_t = PHENOTYPE_T;
private:
  genome_t genome;                  // Original genome for this organism.
  phenotype_t phenotype;            // Current phenotype for this organism.
  emp::Ptr<HW_T> hw_ptr = nullptr;  // What hardware is this organism using?

public:
  Organism(const Organism&) = delete;   // No direct copying of organism is allowed.
  Organism(Organism && in) : OrganismBase(std::move(in)) // Move constructor.
    , genome(std::move(in.genome))
    , phenotype(std::move(in.phenotype))
    , hw_ptr(in.hw_ptr)
  {
    emp_assert(hw_ptr);
    in.hw_ptr = nullptr;        // Clean up old pointers (so they don't deallocate on destruction.)
    hw_ptr->SetOrganism(*this); // Let hardware know about its new Organism.
  } 
  Organism(manager_t & hw_man, genome_t && in_genome) // Build organism from components.
    : genome(std::move(in_genome)), hw_ptr(hw_man.Allocate(*this)) { hw_ptr->Reset(genome); }
  Organism(Organism & org_to_clone) // If no offspring genome, assume clone!
    : genome(org_to_clone.genome)
    , hw_ptr(org_to_clone.GetHardware().GetManager().Allocate(*this))
  {
    hw_ptr->Reset(org_to_clone.GetGenome());
  }
  Organism(Organism & parent, genome_t && offspring_genome) // Provide parent and new genome.
    : genome(std::move(offspring_genome))
    , hw_ptr(parent.GetHardware().GetManager().Allocate(*this))
  {
    hw_ptr->Reset(genome);
  }
  ~Organism() {
    // Clean up hardware if we have it.
    if (hw_ptr) {
      auto & hw_manager = hw_ptr->GetManager();
      hw_manager.Release(hw_ptr);
      hw_ptr = nullptr;
    }
  }

  Organism& operator=(const Organism&) = delete;  // No direct copying of organism is allowed.

  /// Move assignment
  Organism & operator=(Organism && in) {
    if (this == &in) return *this;
  
    // Release current hardware in this Organism, if any.
    if (hw_ptr) { hw_ptr->GetManager().Release(hw_ptr); }
  
    // Move over guts of Organism.
    genome = std::move(in.genome);
    hw_ptr = in.hw_ptr;
    position = in.position;
    id = in.id;

    // Clean up old pointers (so they don't deallocate on destruction.)
    in.hw_ptr = nullptr;
    in.position = UNKNOWN_ID;
    in.id = UNKNOWN_ID;

    // Make sure hardware knows about its new Organism.
    emp_assert(hw_ptr);
    hw_ptr->SetOrganism(*this);

    return *this;
  }

  [[nodiscard]] size_t GetPosition() const { return position; }
  Organism & SetPosition(size_t in_position) { position = in_position; return *this; }

  [[nodiscard]] size_t GetID() const { return id; }
  Organism & SetID(size_t in_id) { id = in_id; return *this; }

  [[nodiscard]] const genome_t & GetGenome() const { return genome; }
  [[nodiscard]] emp::String GetGenomeSequence() const {
    emp_assert(hw_ptr);
    return hw_ptr->GetManager().ToSequence(genome);
  }

  [[nodiscard]] phenotype_t & GetPhenotype() { return phenotype; }
  [[nodiscard]] const phenotype_t & GetPhenotype() const { return phenotype; }

  [[nodiscard]] double GetMetabolicRate() const { return genome.size(); }

  [[nodiscard]] HW_T & GetHardware() { emp_assert(hw_ptr); return *hw_ptr; }
  [[nodiscard]] const HW_T & GetHardware() const { emp_assert(hw_ptr); return *hw_ptr; }

  Organism & Process(size_t cycles) {
    emp_assert(OK());
    hw_ptr->Process(cycles);
    return *this;
  }

  [[nodiscard]] genome_t DivideGenome(emp::Random & random) {
    emp_assert(OK());
    return hw_ptr->DivideGenome(random);
  }

  /// Called when organism is about to die.
  Organism & SignalDeath() {
    // @CAO Recycle hardware.
    return *this;
  }

  void Print(std::ostream & os = std::cout) {
    emp_assert(hw_ptr);
    os << "Genome:" << GetGenomeSequence()
      << " hw_ptr:" << hw_ptr 
      << " id:" << id;
  }

  /// Check to make sure there aren't any problems with this organism object.
  bool OK(bool check_hw_ok=true) const {
    // Make sure hardware is (1) a valid pointer, (2) not null, (3) OK(), & (4) points back here.
    if (!hw_ptr.OK()) {
      emp::notify::Warning("Invalid hardware pointer in Organism.");
      return false;
    }
    if (hw_ptr.IsNull()) {
      emp::notify::Warning("Null hardware pointer in Organism.");
      return false;
    }
    if (check_hw_ok && !hw_ptr->OK()) {
      emp::notify::Warning("Organism: Failed hardware OK() check.");
      return false;
    }
    if (&(hw_ptr->GetOrganism()) != this) {
      emp::notify::Warning("Hardware does not point back to correct organism.");
      return false;
    }

    return true;
  }
};
