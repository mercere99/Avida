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

template <typename HW_T, typename PHENOTYPE_T>
class Organism : public OrganismBase {
public:
  using this_t = Organism<HW_T, PHENOTYPE_T>;
  using hardware_t = HW_T;
  using inst_set_t = hardware_t::inst_set_t;
  using genome_t = typename HW_T::genome_t;
  using phenotype_t = PHENOTYPE_T;
private:
  genome_t genome;        // Original genome for this organism.
  phenotype_t phenotype;  // Current phenotype for this organism.
  hardware_t hardware;    // Hardware run by this organism.

  bool is_mutant = false; // Is this organism different from its parent?

public:
  Organism(const Organism&) = delete;   // No direct copying of organism is allowed.
  Organism(Organism && in) = default;
  Organism(const inst_set_t & inst_set, genome_t && in_genome) // Build organism from components.
    : genome(std::move(in_genome)), hardware(inst_set, genome) { }
  ~Organism() = default;

  Organism & operator=(const Organism&) = delete;  // No direct copying of organism is allowed.

  /// Move assignment
  Organism & operator=(Organism && in) = default;

  Organism & SetBiotaID(size_t id) {
    biota_id = id;
    hardware.SetBiotaID(id);
    return *this;
  }

  [[nodiscard]] bool IsMutated() const { return is_mutant; }
  void SetMutated(bool in=true) { is_mutant=in; }

  void ResetHardware() { hardware.Reset(genome); }
  void SetGenome(genome_t && in_genome) {
    genome = std::move(in_genome);
    is_mutant = false;
  }

  [[nodiscard]] genome_t & GetGenome() { return genome; }
  [[nodiscard]] const genome_t & GetGenome() const { return genome; }
  [[nodiscard]] emp::String GetGenomeSequence() const {
    return hardware.GetInstSet().ToSequence(genome);
  }

  [[nodiscard]] phenotype_t & GetPhenotype() { return phenotype; }
  [[nodiscard]] const phenotype_t & GetPhenotype() const { return phenotype; }

  [[nodiscard]] double GetMetabolicRate() const { return genome.size(); }

  [[nodiscard]] HW_T & GetHardware() { return hardware; }
  [[nodiscard]] const HW_T & GetHardware() const { return hardware; }

  Organism & Process(size_t cycles) {
    emp_assert(OK());
    hardware.Process(cycles);
    return *this;
  }

  [[nodiscard]] genome_t DivideGenome() {
    emp_assert(OK());
    genome_t offspring = hardware.DivideGenome();
    return offspring;
  }

  /// Called when organism is about to die.
  Organism & SignalDeath() {
    // @CAO Recycle hardware.
    return *this;
  }

  void Print(std::ostream & os = std::cout) {
    os << "Genome:" << GetGenomeSequence()
      << " biota_id:" << biota_id
      << " global_id:" << global_id;
  }

  /// Check to make sure there aren't any problems with this organism object.
  bool OK() const {
    if (hardware.GetBiotaID() != biota_id) {
      emp::notify::Error("Hardware biota_id = ", hardware.GetBiotaID(),
                         " but organism biota_id = ", biota_id, ".");
      return false;
    }

    if (!hardware.OK()) {
      emp::notify::Error("Organism: Failed hardware OK() check.");
      return false;
    }

    return true;
  }
};
