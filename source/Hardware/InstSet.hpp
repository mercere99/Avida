#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include <emp/base/array.hpp>
#include <emp/datastructs/Vector.hpp>
#include <emp/math/Random.hpp>
#include <emp/tools/String.hpp>

#include "../Genome.hpp"

// Map of genome instruction to which instruction should be run.
template <typename VM_T, size_t MAX_SET_SIZE, typename INST_RETURN_T=void>
class InstSet {
public:
  using inst_id_t = emp::min_uint_type<MAX_SET_SIZE+1>;
  using inst_fun_t = INST_RETURN_T (VM_T::*)();
  using genome_t = StateGenome<MAX_SET_SIZE>;
  static constexpr size_t NULL_ID = static_cast<inst_id_t>(-1);

private:
  struct InstInfo {
    emp::String name;
    inst_id_t id;
    char symbol = '?';
  };

  emp::array<InstInfo, MAX_SET_SIZE> info;
  emp::array<inst_fun_t, MAX_SET_SIZE> funs;
  size_t num_insts = 0;

public:
  size_t size() const { return num_insts; }

  [[nodiscard]] emp::String GetName(size_t id) const { return info[id].name; }
  [[nodiscard]] char GetSymbol(size_t id) const { return info[id].symbol; }
  [[nodiscard]] inst_fun_t GetFunction(size_t id) const { return funs[id]; }

  // Get an ID from a name.
  [[nodiscard]] inst_id_t GetID(emp::String name) const {
    for (const auto & inst : info) {
      if (inst.name == name) return inst.id;
    }
    return NULL_ID;
  }

  // Get an ID from a symbol.
  [[nodiscard]] inst_id_t GetID(char symbol) const {
    for (const auto & inst : info) {
      if (inst.symbol == symbol) return inst.id;
    }
    return NULL_ID;
  }

  void AddInst(emp::String name, inst_fun_t fun) {
    emp_assert(num_insts < MAX_SET_SIZE);

    char symbol = '?';
    if (num_insts < 26) symbol = 'a' + num_insts;
    else if (num_insts < 52) symbol = 'A' + (num_insts - 26);
    else if (num_insts < 62) symbol = '0' + (num_insts - 52);

    std::cout << "AddInst: " << name << " " << num_insts << " '" << symbol << "'\n";

    info[num_insts] = InstInfo{name, static_cast<inst_id_t>(num_insts), symbol};
    funs[num_insts] = fun;
    ++num_insts;
  }

  void AddNopInst(emp::String name) {
    AddInst(name, nullptr);
  }

  // Execute an instruction on a given VM instance
  void Execute(VM_T & vm, size_t id) {
    emp_assert(id < funs.size(), id);
    (vm.*funs[id])(); // Call the instruction on the VM instance
  }

  /// Build a genome based on a string sequence.
  [[nodiscard]] genome_t BuildGenome(emp::String sequence) {
    genome_t genome;
    for (char symbol : sequence) {
      genome.Push(GetID(symbol));
    }
    return genome;
  }

  /// Build a genome based on a repeated instruction.
  [[nodiscard]] genome_t BuildGenome(size_t length, size_t inst_id=0) {
    return genome_t(length, info[inst_id].id);
  }

  /// Build a random genome of a given length.
  [[nodiscard]] genome_t BuildGenome(size_t length, emp::Random & random) {
    genome_t genome;
    for (size_t i = 0; i < length; ++i) {
      genome.Push(info[random.GetUInt(num_insts)].id);
    }
    return genome;
  }

  /// Convert a genome into a simple sequence.
  [[nodiscard]] std::string ToSequence(const genome_t & genome) const {
    std::string out;
    out.reserve(genome.size());
    for (auto inst_id : genome) {
      out.push_back(GetSymbol(inst_id));
    }
    return out;
  }

};