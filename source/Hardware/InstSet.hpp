#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include <emp/base/array.hpp>
#include <emp/datastructs/Vector.hpp>
#include <emp/tools/String.hpp>

// Map of genome instruction to which instruction should be run.
template <typename VM_T, size_t MAX_INSTS, typename INST_RETURN_T=void>
class InstSet {
public:
  using inst_id_t = emp::min_uint_type<MAX_INSTS+1>;
  using inst_fun_t = INST_RETURN_T (VM_T::*)();
  static constexpr size_t NULL_ID = static_cast<inst_id_t>(-1);

private:
  struct InstInfo {
    emp::String name;
    inst_id_t id;
  };

  emp::array<InstInfo, MAX_INSTS> info;
  emp::array<inst_fun_t, MAX_INSTS> funs;
  size_t num_insts = 0;

public:
  size_t size() const { return num_insts; }

  emp::String GetName(size_t id) const { return info[id].name; }
  inst_fun_t GetFunction(size_t id) const { return funs[id]; }

  inst_id_t GetID(emp::String name) const {
    for (inst_id_t id = 0; id < size(); ++id) {
      if (info[id].name == name) return id;
    }
    return NULL_ID;
  }

  void AddInst(emp::String name, inst_fun_t fun) {
    emp_assert(num_insts < MAX_INSTS);
    const inst_id_t id = info.size();
    info[num_insts] = InstInfo{name, id};
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
};