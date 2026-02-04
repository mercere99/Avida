#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include <emp/base/array.hpp>
#include <emp/datastructs/Vector.hpp>
#include <emp/io/File.hpp>
#include <emp/math/Random.hpp>
#include <emp/tools/String.hpp>

#include "../core/Genome.hpp"

template <typename HW_T> class Organism;

// Map of genome instruction to which instruction should be run.
template <typename HW_T, size_t MAX_SET_SIZE=256, typename INST_RETURN_T=void>
class InstSet {
public:
  using hardware_t = HW_T;
  using genome_t = HW_T::genome_t;
  using inst_id_t = emp::min_uint_type<MAX_SET_SIZE+1>;
  using inst_fun_t = INST_RETURN_T (HW_T::*)();
  using callback_t = std::function<void(Organism<HW_T> &)>;

  static constexpr size_t NULL_ID = static_cast<inst_id_t>(-1);

private:
  struct InstInfo {
    emp::String name;
    inst_id_t id;
    char symbol = '?';
    bool is_callback = false;
  };

  emp::array<inst_fun_t, MAX_SET_SIZE> funs;          // Member functions to call in CPU
  emp::array<callback_t, MAX_SET_SIZE> callback_funs; // For instructions with custom callbacks.
  emp::array<InstInfo, MAX_SET_SIZE> info;            // Additional info about instructions
  size_t num_insts = 0;
  size_t num_nops = 0;

public:
  size_t size() const { return num_insts; }
  size_t NumInsts() const { return num_insts; }
  size_t NumNops() const { return num_nops; }

  [[nodiscard]] emp::String GetName(size_t id) const { return info[id].name; }
  [[nodiscard]] char GetSymbol(size_t id) const { return info[id].symbol; }
  [[nodiscard]] inst_fun_t GetFunction(size_t id) const { return funs[id]; }
  [[nodiscard]] bool IsCallback(size_t id) const { return info[id].is_callback; }

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

  [[nodiscard]] char GetNextSymbol() const {
    if (num_insts < 26) return 'a' + num_insts;
    if (num_insts < 52) return 'A' + (num_insts - 26);
    if (num_insts < 62) return '0' + (num_insts - 52);
    return '?';
  }

  [[nodiscard]] inst_id_t GetRandom(emp::Random & random) const {
    return static_cast<inst_id_t>(random.GetUInt(num_insts));
  }

  void AddInst(emp::String name, inst_fun_t fun, callback_t callback_fun=nullptr) {
    emp_assert(num_insts < MAX_SET_SIZE);

    const char symbol = GetNextSymbol();
    inst_id_t id = static_cast<inst_id_t>(num_insts);
    std::cout << "AddInst: " << name << " " << num_insts << " '" << symbol << "'\n";

    info[num_insts] = InstInfo{name, id, symbol, false};
    funs[num_insts] = fun;
    if (callback_fun) callback_funs[num_insts] = callback_fun;
    ++num_insts;
  }

  /// Add an instruction to the set that is a nop that can be used as a modifier.
  /// Note: Nops must be at the BEGINNING of the instruction set.
  void AddNopInst(emp::String name) {
    if (num_nops != num_insts) {
      emp::notify::Error("Nops must be at beginning of instruction set. (inst='", name, "')");
    }
    AddInst(name, nullptr);
    ++num_nops;
  }

  // Add a function that's just a callback.
  void AddCallbackInst(emp::String name, callback_t callback_fun) {
    AddInst(name, nullptr, callback_fun);
  }

  // Execute an instruction on a given VM instance
  void Execute(HW_T & vm, size_t id) const {
    emp_assert(id < num_insts, "Calling execute with an invalid inst id.", id, num_insts);
    emp_assert(vm.OK(), "Calling execute on an invalid virtual machine.");

    if (id < num_nops) return;  // Nops are "no operation" instructions.

    // If this function doesn't exist, assume it is just a callback.
    if (!funs[id]) callback_funs[id](vm.GetOrganism());

    // Otherwise call the appropriate function.
    else (vm.*funs[id])();   // Call the instruction on the VM instance
  }

  /// Build a genome based on a string sequence.
  void BuildGenome(genome_t & genome, emp::String sequence) {
    for (char symbol : sequence) {
      genome.Push(GetID(symbol));
    }
  }

  /// Build a random genome of a given length.
  /// By default half of the instructions will be nop modifiers.
  void BuildGenome(genome_t & genome, size_t length, emp::Random & random, double nop_prob=0.5) {
    genome.Resize(0);
    const size_t non_nops = num_insts - num_nops;
    for (size_t i = 0; i < length; ++i) {
      // genome.Push(info[random.GetUInt(num_insts)].id);
      if (random.P(nop_prob)) {
        genome.Push(info[random.GetUInt(num_nops)].id);
      } else {
        genome.Push(info[random.GetUInt(non_nops) + num_nops].id);
      }
    }
  }

  [[nodiscard]] genome_t LoadGenome(std::istream & is) {
    genome_t genome;
    emp::File file(is);
    file.RemoveComments("//");
    file.CompressWhitespace();
    for (emp::String line : file) {
      auto inst_id = GetID(line);
      if (inst_id == NULL_ID) {
        emp::notify::Error("Unknown instruction '", line, "'.");
      }
      genome.Push(inst_id);
    }
    return genome;
  }

  [[nodiscard]] genome_t LoadGenome(emp::String filename) {
    std::ifstream is(filename);
    return LoadGenome(is);
  }


  /// Convert a genome into a simple sequence.
  [[nodiscard]] emp::String ToSequence(const genome_t & genome) const {
    emp::String out;
    out.reserve(genome.size());
    for (auto inst_id : genome) {
      out.push_back(GetSymbol(inst_id));
    }
    return out;
  }

};