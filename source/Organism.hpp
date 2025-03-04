#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include "emp/base/Ptr.hpp"

#include "Hardware/HardwareBase.hpp"
#include "Hardware/HardwareManager.hpp"

class Population;

/// All organisms in Avida are set up as an Organism class.
class Organism {
private:
  using id_t = uint64_t;
  using gen_t = uint32_t;
  static constexpr id_t UNKNOWN_ID = static_cast<id_t>(-1);

  Genome genome;                           // Original genome for this organism.
  emp::Ptr<HardwareBase> hw_ptr = nullptr; // What hardware is this organism using?
  emp::Ptr<Population> pop_ptr = nullptr;  // Which population is this organism in?

  id_t id = UNKNOWN_ID;     // Unique organism ID.
  gen_t generation = 0;     // Number of ancestral steps back to injected organism.
public:
  Organism(Organism && in)  // Move constructor.
    : genome(std::move(in.genome))
    , hw_ptr(in.hw_ptr)
    , pop_ptr(in.pop_ptr)
    , id(in.id)
    , generation(in.generation)
  {
    // Clean up old pointers (so they don't deallocate on destruction.)
    in.hw_ptr = nullptr;
    in.pop_ptr = nullptr;

    // Make sure hardware knows about its new Organism.
    hw_ptr->SetOrganism(*this);
  } 
  Organism(HardwareManager & hw_man, Genome genome)
    : genome(genome), hw_ptr(hw_man.Allocate(*this)) { hw_ptr->Reset(genome); }
  Organism(Organism & org_to_clone) // If not offspring, assume clone!
    : hw_ptr(org_to_clone.GetHardware().GetManager().Allocate(*this))
    , generation(org_to_clone.generation)
  {
    hw_ptr->Reset(org_to_clone.GetGenome());
  }
  Organism(Organism & parent, Genome offspring_genome) // If not offspring, assume clone!
    : hw_ptr(parent.GetHardware().GetManager().Allocate(*this))
    , generation(parent.generation+1)
  {
    hw_ptr->Reset(offspring_genome);
  }
  ~Organism() {
    // Clean up hardware if we have it.
    if (hw_ptr) {
      auto & hw_manager = hw_ptr->GetManager();
      hw_manager.Release(hw_ptr);
      hw_ptr = nullptr;
    }
  }

  Organism & operator=(Organism && in) {
    genome = std::move(in.genome);
    hw_ptr = in.hw_ptr;
    pop_ptr = in.pop_ptr;
    id = in.id;
    generation = in.generation;

    // Clean up old pointers (so they don't deallocate on destruction.)
    in.hw_ptr = nullptr;
    in.pop_ptr = nullptr;

    // Make sure hardware knows about its new Organism.
    hw_ptr->SetOrganism(*this);

    return *this;
  }

  [[nodiscard]] id_t GetID() const { return id; }
  Organism & SetID(id_t in_id) { id = in_id; return *this; }

  [[nodiscard]] const Genome & GetGenome() const { return genome; }

  [[nodiscard]] gen_t GetGeneration() const { return generation; }
  Organism & SetGeneration(gen_t in_gen) { generation = in_gen; return *this; }

  [[nodiscard]] HardwareBase & GetHardware() { emp_assert(hw_ptr); return *hw_ptr; }
  [[nodiscard]] const HardwareBase & GetHardware() const { emp_assert(hw_ptr); return *hw_ptr; }

  [[nodiscard]] bool InPopulation() const { return pop_ptr; }
  [[nodiscard]] bool InPopulation(const Population & pop) const { return pop_ptr == &pop; }
  [[nodiscard]] Population & GetPopulation() {
    emp_assert(OK());
    emp_assert(pop_ptr);
    return *pop_ptr;
  }
  [[nodiscard]] const Population & GetPopulation() const {
    emp_assert(OK());
    emp_assert(pop_ptr);
    return *pop_ptr;
  }
  Organism & SetPopulation(Population & in_pop) {
    emp_assert(OK());
    pop_ptr = &in_pop;
    return *this;
  }

  Organism & Process(size_t cycles) {
    emp_assert(OK());
    hw_ptr->Process(cycles);
    return *this;
  }

  [[nodiscard]] Genome DivideGenome() {
    emp_assert(OK());
    return hw_ptr->DivideGenome();
  }

  /// Called when organism is about to die.
  Organism & SignalDeath() {
    // @CAO Recycle hardware.
    return *this;
  }

  void Print(std::ostream & os = std::cout) {
    os << "Genome:"; genome.Print(os);
    os << " hw_ptr:" << hw_ptr 
       << " pop_ptr:" << pop_ptr
       << " id:" << id
       << " generation:" << generation;
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
    if (check_hw_ok && !hw_ptr->OK(false)) {
      emp::notify::Warning("Organism: Failed hardware OK() check.");
      return false;
    }
    if (&(hw_ptr->GetOrganism()) != this) {
      emp::notify::Warning("Hardware does not point back to correct organism.");
      return false;
    }

    // Make sure population is valid (can be null if org is not a in a population)
    if (!pop_ptr.OK()) {
      emp::notify::Warning("Invalid population pointer in Organism.");
      return false;
    }

    return true;
  }
};
