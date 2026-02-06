#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include <expected>
#include <fstream>     // std::ifstream
#include <functional>  // std::function
#include <istream>

#include "emp/base/array.hpp"
#include "emp/io/File.hpp"
#include "emp/math/Random.hpp"
#include "emp/tools/String.hpp"

template <typename HW_T> class Organism;

// Map of genome instruction to which instruction should be run.
template <typename HW_T, size_t MAX_SET_SIZE=256, typename INST_RETURN_T=void>
class InstSet {
public:
  using hardware_t = HW_T;
  using genome_t = typename HW_T::genome_t;
  using inst_id_t = emp::min_uint_type<MAX_SET_SIZE+1>;
  using inst_fun_t = INST_RETURN_T (HW_T::*)();
  using callback_t = std::function<void(Organism<HW_T> &)>;

  static constexpr inst_id_t NULL_ID = static_cast<inst_id_t>(-1);

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
  [[nodiscard]] size_t size() const noexcept { return num_insts; }
  [[nodiscard]] size_t NumInsts() const noexcept { return num_insts; }
  [[nodiscard]] size_t NumNops() const noexcept { return num_nops; }

  [[nodiscard]] const emp::String & GetName(size_t id) const noexcept {
    emp_assert(id < num_insts);
    return info[id].name;
  }
  [[nodiscard]] char GetSymbol(size_t id) const noexcept {
    emp_assert(id < num_insts);
    return info[id].symbol;
  }
  [[nodiscard]] inst_fun_t GetFunction(size_t id) const noexcept {
    emp_assert(id < num_insts);
    return funs[id];
  }
  [[nodiscard]] bool IsCallback(size_t id) const noexcept {
    emp_assert(id < num_insts);
    return info[id].is_callback;
  }

  // Get an ID from a name.
  [[nodiscard]] inst_id_t GetID(const emp::String & name) const noexcept {
    for (size_t i = 0; i < num_insts; ++i) {
      const auto & inst = info[i];
      if (inst.name == name) return inst.id;
    }
    return NULL_ID;
  }

  // Get an ID from a symbol.
  [[nodiscard]] inst_id_t GetID(char symbol) const noexcept {
    for (size_t i = 0; i < num_insts; ++i) {
      const auto & inst = info[i];
      if (inst.symbol == symbol) return inst.id;
    }
    return NULL_ID;
  }

  [[nodiscard]] char GetNextSymbol() const noexcept {
    if (num_insts < 26) return 'a' + num_insts;
    if (num_insts < 52) return 'A' + (num_insts - 26);
    if (num_insts < 62) return '0' + (num_insts - 52);
    return '?';
  }

  [[nodiscard]] inst_id_t GetRandom(emp::Random & random) const noexcept {
    emp_assert(num_insts > 0);
    return static_cast<inst_id_t>(random.GetUInt(num_insts));
  }

  void AddInst(const emp::String & name, inst_fun_t fun, callback_t callback_fun=nullptr) noexcept {
    emp_assert(num_insts < MAX_SET_SIZE);

    const char symbol = GetNextSymbol();
    inst_id_t id = static_cast<inst_id_t>(num_insts);

    info[num_insts] = InstInfo{name, id, symbol, callback_fun != nullptr};
    funs[num_insts] = fun;
    callback_funs[num_insts] = callback_fun;
    ++num_insts;
  }

  /// Add an instruction to the set that is a nop that can be used as a modifier.
  /// Note: Nops must be at the BEGINNING of the instruction set.
  bool AddNopInst(const emp::String & name) noexcept {
    if (num_nops != num_insts) {
      emp::notify::Error("Nops must be at beginning of instruction set. (inst='", name, "')");
      return false;
    }
    AddInst(name, nullptr);
    ++num_nops;
    return true;
  }

  // Add a function that's just a callback.
  void AddCallbackInst(const emp::String & name, callback_t callback_fun) noexcept {
    AddInst(name, nullptr, callback_fun);
  }

  // Execute an instruction on a given VM instance
  void Execute(HW_T & vm, size_t id) const noexcept {
    emp_assert(id < num_insts, "Calling execute with an invalid inst id.", id, num_insts);
    emp_assert(vm.OK(), "Calling execute on an invalid virtual machine.");

    if (id < num_nops) return;  // Nops are "no operation" instructions.


    if (funs[id]) (vm.*funs[id])();
    else {
      // If this function doesn't exist, assume it is just a callback.
      emp_assert(callback_funs[id], "Instruction has no function or callback.", id);
      callback_funs[id](vm.GetOrganism());
    }
  }

  /// Rebuild an existing genome based on a string sequence.
  void BuildGenome(genome_t & genome, const emp::String & sequence) const noexcept {
    genome.Clear();
    for (char symbol : sequence) {
      const auto id = GetID(symbol);
      emp_assert(id != NULL_ID, "Unknown instruction symbol.", symbol);
      genome.Push(id);
    }
  }

  /// Build a genome based on a string sequence.
  [[nodiscard]] genome_t BuildGenome(const emp::String & sequence) const noexcept {
    genome_t genome;
    BuildGenome(genome, sequence);
    return genome;
  }

  /// Build a random genome of a given length.
  /// By default half of the instructions will be nop modifiers.
  void BuildGenome(genome_t & genome, size_t length, emp::Random & random, double nop_prob=0.5) {
    emp_assert(num_insts > 0);
    emp_assert(num_nops <= num_insts);
    emp_assert(num_nops > 0 || nop_prob == 0.0);

    const size_t non_nops = num_insts - num_nops;
    emp_assert(non_nops > 0 || nop_prob == 1.0);

    genome.Clear();
    for (size_t i = 0; i < length; ++i) {
      if (random.P(nop_prob)) {
        genome.Push(info[random.GetUInt(num_nops)].id);
      } else {
        genome.Push(info[random.GetUInt(non_nops) + num_nops].id);
      }
    }
  }

  [[nodiscard]] std::expected<genome_t, emp::String> LoadGenome(std::istream & is) const {
    genome_t genome;
    emp::File file(is);
    file.RemoveComments("//");
    file.CompressWhitespace();
    for (emp::String line : file) {
      auto inst_id = GetID(line);
      if (inst_id == NULL_ID) {
        emp::notify::Error("Unknown instruction '", line, "'.");
        return std::unexpected("Unknown instruction '" + line + "'.");
      }
      genome.Push(inst_id);
    }
    return genome;
  }

  [[nodiscard]] std::expected<genome_t, emp::String> LoadGenome(const emp::String & filename) const {
    std::ifstream is(filename);
    if (!is) return std::unexpected(emp::String("Could not open file: ") + filename);
    return LoadGenome(is);
  }


  /// Convert a genome into a simple sequence.
  [[nodiscard]] emp::String ToSequence(const genome_t & genome) const noexcept {
    emp::String out;
    out.reserve(genome.size());
    for (auto inst_id : genome) {
      out.push_back(GetSymbol(inst_id));
    }
    return out;
  }

};