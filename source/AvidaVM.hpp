#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include "emp/base/array.hpp"

#include "Genome.hpp"

/// Default Avida Virtual Machine for use in Avida 5
class AvidaVM {
private:
  using data_t = int;  // What type of data does this VM use?
  using inst_fun_t = void (AvidaVM::*)(); // Type for functions to be called.
  static constexpr size_t NUM_NOPS = 6;
  static constexpr size_t STACK_DEPTH = 16;
  static constexpr size_t MEM_SIZE = 64;    // How much physical memory is available?
  static constexpr size_t MAX_INSTS = 256;  // Maximum number of distinct instructions.

  struct Head {
    size_t pos;
    bool on_genome = true; // If false, head is on memory.

    Head & operator++() { ++pos; return this; }

    void Reset(size_t _pos=0, bool _on_genome=true) { pos = _pos; on_genome = _on_genome; }

    // Make sure head is in a valid position given the size of the buffer it is on.
    void Refresh(size_t buffer_size) {
      if (pos >= buffer_size) pos %= buffer_size;
    }

    template <typename T>
    data_t Read(const T & buffer) {
      Refresh(buffer.size());
      return buffer[pos];
    }

    template <typename T>
    void Write(data_t data, const T & buffer) {
      Refresh(buffer.size());
      buffer[pos] = data;
    }

  };

  struct Stack {
    emp::array<data_t, STACK_DEPTH> stack{0};
    size_t stack_pos = 0;

    void Reset() {
      for (auto & entry : stack) entry = 0;
      stack_pos = 0;
    }

    void Push(data_t value) {
      stack[stack_pos] = value;
      stack_pos = (stack_pos+1) % STACK_DEPTH;
    }
    data_t Pop() {
      --stack_pos;
      if (stack_pos > STACK_DEPTH) stack_pos = STACK_DEPTH-1;
      return stack[stack_pos];
    }
  };

  // === Hardware ===

  StateGenome<MAX_INSTS> genome;
  emp::array<data_t, MEM_SIZE> memory;
  emp::array<inst_fun_t, MAX_INSTS> inst_set;
  size_t num_insts = 0; // Number of instructions that are active.

  enum class Nop {
    A = 0,
    B = 1,
    C = 2,
    D = 3,
    E = 4,
    F = 5,
  };

  enum HeadType {
    HEAD_IP = 0,      // Instruction Pointer
    HEAD_G_READ = 1,  // Genome Read (init: genome start)
    HEAD_G_WRITE = 2, // Genome Write (init: genome end)
    HEAD_M_READ = 3,  // Memory Read (init: memory start)
    HEAD_M_WRITE = 4, // Memory Write (init: memory start)
    HEAD_FLOW = 5,    // Flow Control (init: genome start)
  };
  emp::array<Head, NUM_NOPS> heads;
  emp::array<Stack, NUM_NOPS> stacks;
  size_t scope = 0; // Global scope


  // =========== Helper Functions ============

  data_t ReadHead(Head & head) {
    return head.on_genome ? head.Read(genome) : head.Read(memory);
  }

  void WriteHead(Head & head, data_t data) {
    head.on_genome ? head.Write(data, genome) : head.Write(data, memory);
  }

  Head & IP() { return heads[HEAD_IP]; }
  data_t ReadIP() { return ReadHead(IP()); }
  void AdvanceIP() { ++IP(); }

  // Select the argument to use, overriding a nop if possible.
  size_t GetArg(Nop default_arg) {
    data_t out_val = ReadIP();
    if (out_val < NUM_NOPS) AdvanceIP();              // We found a nop.
    else out_val = static_cast<data_t>(default_arg);  // We didn't find a nop.
    return out_val;
  }

  Stack & GetStackArg(Nop default_arg) {
    return stacks[GetArg(default_arg)];
  }

  Head & GetHeadArg(HeadType default_head) {
    return heads[GetArg(static_cast<Nop>(default_head))];
  }


  data_t ToValidInst(data_t inst_val) const {
    inst_val %= num_insts;
    if (inst_val < 0) inst_val += num_insts;
  }

  size_t FindModifiedStack(size_t default_stack) const {

  }

public:
  // AvidaVM() = default;
  // AvidaVM(const AvidaVM &) = default;
  // AvidaVM(AvidaVM &&) = default;
  // AvidaVM & operator=(const AvidaVM &) = default;
  // AvidaVM & operator=(AvidaVM &&) = default;


  // Instructions
  void Inst_Const() {  // Push value [Nop-A] onto Stack [Nop-A]
    static constexpr data_t const_vals[]{ 1, 2, 4, 16, 256, -1, 65536, 0, -2, 0, 0, 0};
    const data_t value = const_vals[GetArg(Nop::A)];
    GetStackArg(Nop::A).Push(value);
  }

  // Initialize the state of the virtual CPU
  void Reset() {
    // Reset Heads
    heads[HEAD_IP].Reset();
    heads[HEAD_G_READ].Reset();
    heads[HEAD_G_WRITE].Reset(genome.size()-1, true);
    heads[HEAD_M_READ].Reset(0, false);
    heads[HEAD_M_WRITE].Reset(0, false);
    heads[HEAD_FLOW].Reset();

    // Reset the stacks.
    for (auto & stack : stacks) stack.Reset();

    scope = 0; // Move back to global scope.



    // Load instructions
    emp::array<inst_fun_t, MAX_INSTS> inst_set;
    size_t num_insts = 0; // Number of instructions that are active.

    
  }

};