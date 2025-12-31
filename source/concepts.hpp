#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

 // Hardware must have the following member functions:

/*
class HardwareBase {
protected:
  HardwareManager & hw_manager;
  emp::Ptr<Organism> org_ptr = nullptr;
public:
  HardwareBase(HardwareManager & hw_manager) : hw_manager(hw_manager) { }
  virtual ~HardwareBase() { }

  [[nodiscard]] Organism & GetOrganism() { emp_assert(org_ptr); return *org_ptr; }
  [[nodiscard]] const Organism & GetOrganism() const { emp_assert(org_ptr); return *org_ptr; }
  HardwareBase & SetOrganism(Organism & org) { org_ptr = &org; return *this; }

  [[nodiscard]] HardwareManager & GetManager() { return hw_manager; }
  [[nodiscard]] const HardwareManager & GetManager() const { return hw_manager; }

  virtual void Process(size_t cycles=10) = 0;
  virtual void Reset() = 0;
  virtual void Reset(const Genome & in_genome) = 0;
  [[nodiscard]] virtual Genome DivideGenome(emp::Random & random) = 0;

  virtual bool OK([[maybe_unused]] bool check_org_ok=true) const { return true; }
};
 */


// Hardware manager must have:

// Pre-declarations
// class Organism;

// /// A class to handle a particular configuration of hardware.
// class HardwareManager {
// protected:
//   using hw_ptr_t = emp::Ptr<HardwareBase>;
//   using feedback_t = std::function<void(Organism & /*org*/)>;
//   emp::vector< hw_ptr_t > hw_ptrs;  // Pointers to available hardware.

//   virtual hw_ptr_t AllocateNew(Organism & org) = 0;
// public:
//   virtual ~HardwareManager() { Clear(); }

//   static std::string DefaultName() { return "Unnamed"; };

//   virtual bool AddCallback(emp::String name, feedback_t fun) = 0;

//   [[nodiscard]] virtual emp::String ToSequence(const Genome & genome) const = 0;
//   [[nodiscard]] virtual Genome LoadGenome(emp::String filename) = 0;

//   virtual void Mutate(emp::Random & random, Genome & genome) const = 0;

//   [[nodiscard]] hw_ptr_t Allocate(Organism & org) {
//     if (hw_ptrs.size()) {
//       hw_ptr_t out = hw_ptrs.back();
//       hw_ptrs.pop_back();
//       out->SetOrganism(org);
//       return out;
//     }
//     return AllocateNew(org);
//   }

//   void Release(hw_ptr_t ptr) {
//     hw_ptrs.push_back(ptr);
//   }

//   // Remove current inventory of hardware.
//   void Clear() {
//     for (auto ptr : hw_ptrs) ptr.Delete();
//     hw_ptrs.resize(0);
//   }
// };