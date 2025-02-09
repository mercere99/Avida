#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include <algorithm>
#include <utility>

#include "emp/base/array.hpp"
#include "emp/math/math.hpp"

#include "Genome.hpp"

/// Default Avida Virtual Machine for use in Avida 5
class AvidaVM {
private:
  // Configured values.
  static constexpr size_t NUM_NOPS = 6;
  static constexpr size_t STACK_DEPTH = 16;
  static constexpr size_t MEM_SIZE = 64;          // How much physical memory is available?
  static constexpr size_t MAX_INSTS = 256;        // Max number of distinct instructions.
  static constexpr size_t MAX_GENOME_SIZE = 1024; // Max genome length.

  // Configured types.
  using data_t = int;  // What type of data does this VM use?
  using mem_t = emp::array<data_t, MEM_SIZE>;
  using genome_t = StateGenome<MAX_INSTS>;
  using inst_fun_t = void (AvidaVM::*)(); // Type for functions to be called.

  // Calculated values.
  static constexpr size_t DATA_BITS = sizeof(data_t)*8; // Number of bits in data_t;

  struct Head {
    size_t pos;
    bool on_genome = true; // If false, head is on memory.

    Head & operator++() { ++pos; return *this; }

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
      if (stack_pos > STACK_DEPTH) stack_pos = STACK_DEPTH-1; // Loop if needed.
      return stack[stack_pos];
    }

    data_t Top() const {
      return stack[stack_pos ? (stack_pos - 1) : STACK_DEPTH-1];
    }

  };

  // === Hardware ===

  genome_t genome;
  mem_t memory;
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
    HEAD_IP = 0,      // Instruction Pointer (init: genome start)
    HEAD_G_READ = 1,  // Genome Read (init: genome start)
    HEAD_G_WRITE = 2, // Genome Write (init: genome start)
    HEAD_M_READ = 3,  // Memory Read (init: memory start)
    HEAD_M_WRITE = 4, // Memory Write (init: memory start)
    HEAD_FLOW = 5,    // Flow Control (init: genome start)
  };
  emp::array<Head, NUM_NOPS> heads;
  emp::array<Stack, NUM_NOPS> stacks;
  size_t cur_scope = 0; // Start in the global scope.
  emp::array<size_t, NUM_NOPS> scope_starts{0};  // Track where each scope started.
  size_t error_count = 0;


  // =========== Helper Functions ============

  // Read the data found at the provided head and return it.
  data_t ReadHead(Head & head) {
    return head.on_genome ? head.Read(genome) : head.Read(memory);
  }

  // Write the provided data to the position pointed by the provided head.
  void WriteHead(Head & head, data_t data) {
    head.on_genome ? head.Write(data, genome) : head.Write(data, memory);
  }

  Head & IP() { return heads[HEAD_IP]; }
  data_t ReadIP() { return ReadHead(IP()); }
  void AdvanceIP() { ++IP(); }

  data_t ToValidInst(data_t inst_val) const {
    return emp::Mod(inst_val, num_insts);
  }

  data_t ReadIPInst() { return ToValidInst(ReadIP()); }

  // Select the argument to use, overriding a nop if possible.
  size_t GetArg(size_t default_arg) {
    data_t out_val = ReadIPInst();
    if (out_val < NUM_NOPS) {  // We found a nop.
      AdvanceIP();
      return out_val;
    }
    return default_arg; // We didn't find a nop.
  }

  size_t GetArg(Nop default_arg) { return GetArg(static_cast<data_t>(default_arg)); }

  Stack & GetStackArg(Nop default_arg) {
    return stacks[GetArg(default_arg)];
  }
  Stack & GetStackArg(size_t default_arg) {
    return stacks[GetArg(default_arg)];
  }

  Head & GetHeadArg(HeadType default_head) {
    return heads[GetArg(static_cast<size_t>(default_head))];
  }

public:
  // AvidaVM() = default;
  // AvidaVM(const AvidaVM &) = default;
  // AvidaVM(AvidaVM &&) = default;
  // AvidaVM & operator=(const AvidaVM &) = default;
  // AvidaVM & operator=(AvidaVM &&) = default;


  // === Instructions ===

  // Inst: Push value [Nop-A] onto Stack [Nop-A]
  void Inst_Const() {
    static constexpr data_t const_vals[]{ 1, 2, 4, 16, 256, -1, 65536, 0, -2, 0, 0, 0};
    const data_t value = const_vals[GetArg(Nop::A)];
    GetStackArg(Nop::A).Push(value);
  }

  // Inst: No-operation.
  void Inst_Nop()  { }

  // Inst: Pop[Nop-A]:X ; Push[Arg1] !X
  void Inst_Not() {
    size_t X_id = GetArg(Nop::A);
    const data_t X = stacks[X_id].Pop();
    GetStackArg(X_id).Push(!X);
  }

  // Inst: Pop[Nop-A]:X ; Pop[Arg1]:Y ; Push[Arg1] X<<Y
  void Inst_Shift() {
    size_t X_id = GetArg(Nop::A);
    const data_t X = stacks[X_id].Pop();
    const data_t Y = GetStackArg(X_id).Pop();
    GetStackArg(X_id).Push(X << emp::Mod(Y,DATA_BITS));
  }

  // Inst: Pop[Nop-A]:X ; Pop[Arg1]:Y ; Push[Arg1] X + Y
  void Inst_Add() {
    size_t X_id = GetArg(Nop::A);
    const data_t X = stacks[X_id].Pop();
    const data_t Y = GetStackArg(X_id).Pop();
    GetStackArg(X_id).Push(X + Y);
  }

  // Inst: Pop[Nop-A]:X ; Pop[Arg1]:Y ; Push[Arg1] X - Y
  void Inst_Sub() {
    size_t X_id = GetArg(Nop::A);
    const data_t X = stacks[X_id].Pop();
    const data_t Y = GetStackArg(X_id).Pop();
    GetStackArg(X_id).Push(X - Y);
  }

  // Inst: Pop[Nop-A]:X ; Pop[Arg1]:Y ; Push[Arg1] X * Y
  void Inst_Mult() {
    size_t X_id = GetArg(Nop::A);
    const data_t X = stacks[X_id].Pop();
    const data_t Y = GetStackArg(X_id).Pop();
    GetStackArg(X_id).Push(X * Y);
  }

  // Inst: Pop[Nop-A]:X ; Pop[Arg1]:Y ; Push[Arg1] X / Y
  void Inst_Div() {
    size_t X_id = GetArg(Nop::A);
    const data_t X = stacks[X_id].Pop();
    const data_t Y = GetStackArg(X_id).Pop();
    if (Y == 0) ++error_count;
    else GetStackArg(X_id).Push(X / Y);
  }

  // Inst: Pop[Nop-A]:X ; Pop[Arg1]:Y ; Push[Arg1] X % Y
  void Inst_Mod() {
    size_t X_id = GetArg(Nop::A);
    const data_t X = stacks[X_id].Pop();
    const data_t Y = GetStackArg(X_id).Pop();
    if (Y == 0) ++error_count;
    else GetStackArg(X_id).Push(X % Y);
  }

  // Inst: Pop[Nop-A]:X ; Pop[Arg1]:Y ; Push[Arg1] X ** Y
  void Inst_Exp() {
    size_t X_id = GetArg(Nop::A);
    const data_t X = stacks[X_id].Pop();
    const data_t Y = GetStackArg(X_id).Pop();
    GetStackArg(X_id).Push(emp::Pow(X, Y));
  }

  // Inst: Pop[Nop-A]:X ; Pop[Arg1]:Y ; Push[Arg1] X,Y if X>Y else Y,X
  void Inst_Sort() {
    size_t X_id = GetArg(Nop::A);
    size_t Y_id = GetArg(X_id);
    data_t X = stacks[X_id].Pop();
    data_t Y = stacks[Y_id].Pop();
    if (X < Y) std::swap(X,Y);
    GetStackArg(X_id).Push(X);
    GetStackArg(Y_id).Push(Y);
  }

  // Inst: Pop[Nop-A]:X ; Pop[Arg1]:Y ; Push[Arg1] X < Y
  void Inst_TestLess() {
    size_t X_id = GetArg(Nop::A);
    const data_t X = stacks[X_id].Pop();
    const data_t Y = GetStackArg(X_id).Pop();
    GetStackArg(X_id).Push(X < Y);
  }

  // Inst: Pop[Nop-A]:X ; Pop[Arg1]:Y ; Push[Arg1] X == Y
  void Inst_TestEqu() {
    size_t X_id = GetArg(Nop::A);
    const data_t X = stacks[X_id].Pop();
    const data_t Y = GetStackArg(X_id).Pop();
    GetStackArg(X_id).Push(X == Y);
  }

  // Inst: Pop[Nop-A]:X ; Pop[Arg1]:Y ; Push[Arg1] ~(X&Y)  (bitwise)
  void Inst_Nand() {
    size_t X_id = GetArg(Nop::A);
    const data_t X = stacks[X_id].Pop();
    const data_t Y = GetStackArg(X_id).Pop();
    GetStackArg(X_id).Push(~(X & Y));
  }

  // Inst: Pop[Nop-A]:X ; Pop[Arg1]:Y ; Push[Arg1] X ^ Y   (bitwise)
  void Inst_Xor() {
    size_t X_id = GetArg(Nop::A);
    const data_t X = stacks[X_id].Pop();
    const data_t Y = GetStackArg(X_id).Pop();
    GetStackArg(X_id).Push(X ^ Y);
  }

  // Inst: Pop[Nop-A]:X ; if X == 0, skip next instruction
  void Inst_If() {
    const data_t X = GetStackArg(Nop::A).Pop();
    if (!X) AdvanceIP();
  }

  // Inst: Change current Scope to [Nop-A]. If current Scope <= previous, end prev Scope.
  void Inst_Scope() {
    size_t new_scope = GetArg(Nop::A);
    // If we are starting new scopes, track starting lines here.
    for (size_t scope = cur_scope; scope <= new_scope; ++scope) {
      scope_starts[scope] = IP().pos;
    }
    cur_scope = new_scope;
  }

  // Inst: Restart Scope [Current]
  void Inst_Continue() {
    size_t target_scope = GetArg(cur_scope);
    if (target_scope > cur_scope) ++error_count;  // Trying to continue a scope we are not in.
    else {
      IP().pos = scope_starts[target_scope];
      cur_scope = target_scope;
    }
  }

  // Inst: Advance to end of Scope [Current]
  void Inst_Break() {
    size_t target_scope = GetArg(cur_scope);
    if (target_scope > cur_scope) ++error_count;  // Trying to break from a scope we are not in.
    else {
      while (IP().pos > 0) {  // Scan for end of this scope.
        if (ReadIPInst() == inst_scope_id) {
          AdvanceIP();
          if (GetArg(Nop::A) <= target_scope) break;
        }
        AdvanceIP();
      }
    }
  }

  // Inst: Discard top entry from [Nop-A] X
  void Inst_StackPop() {
    GetStackArg(Nop::A).Pop();
  }

  // Inst: Read top of Stack [Nop-A] (no popping) and push a copy on [Arg1]
  void Inst_StackDup() {
    const size_t stack1_id = GetArg(Nop::A);
    data_t value = stacks[stack1_id].Top();
    GetStackArg(stack1_id).Push(value);
  }

  // Inst: Pop[Nop-A]:X ; Pop[Arg1]:Y; push X on Stack [Arg2]; push Y on Stack [Arg1]
  void Inst_StackSwap() {
    const size_t stack1_id = GetArg(Nop::A);
    const size_t stack2_id = GetArg(stack1_id);
    const data_t X = stacks[stack1_id].Pop();
    const data_t Y = stacks[stack2_id].Pop();
    GetStackArg(stack2_id).Push(X);
    GetStackArg(stack1_id).Push(Y);
  }

  // Inst: Pop[Nop-A]:X and Push[Arg1+1] X
  void Inst_StackMove() {
    const size_t stack1_id = GetArg(Nop::A);
    const size_t stack2_id = GetArg((stack1_id+1)%NUM_NOPS);
    if (stack1_id != stack2_id) {
      stacks[stack2_id].Push( stacks[stack1_id].Pop() );
    }
  }

  // Inst: Copy the value from Head [Nop-B] to Head [Nop-C], advancing both
  void Inst_Copy() {
    Head & from_head = GetHeadArg(HEAD_G_READ);
    Head & to_head = GetHeadArg(HEAD_G_WRITE);
    WriteHead(to_head, ReadHead(from_head));
  }

  // Inst: Read value at Head [Nop-D]:X ; Push X onto stack [Nop-A] ; advance Head.
  void Inst_Load() {
    Head & from_head = GetHeadArg(HEAD_M_READ);
    GetStackArg(Nop::A).Push( ReadHead(from_head) );
  }

  // Inst: Pop[Nop-A] and write the value into Head [NopE] ; advance Head.
  void Inst_Store() {
    data_t value = GetStackArg(Nop::A).Pop();
    Head & to_head = GetHeadArg(HEAD_M_WRITE);
    WriteHead(to_head, value);
  }

  // Inst: Allocates extra space and places the write head at the beginning of the space.
  void Inst_Allocate() {
    const size_t start_size = genome.size();
    genome.resize( std::min(start_size*2, MAX_GENOME_SIZE) );
    heads[HEAD_G_WRITE].Reset(start_size, true);
  }

  // Inst: Split off space between read head and write head.
  void Inst_DivideCell() {
  }

  // Inst: Push the position of Head[Nop-F] onto stack [Nop-A]
  void Inst_HeadPos() {
    Head & head = GetHeadArg(HEAD_FLOW);
    GetStackArg(Nop::A).Push(head.pos);
  }

  // Inst: Pop stack [Nop-A] and move head[Nop-F] to that position.
  void Inst_SetHead() {
    const data_t new_pos = GetStackArg(Nop::A).Pop();
    GetHeadArg(HEAD_FLOW).pos = new_pos;
  }

  // Inst: Jump Head[Nop-A] to Head[Nop-F]
  void Inst_JumpHead() {
    Head & jump_head = GetHeadArg(HEAD_IP);
    jump_head = GetHeadArg(HEAD_FLOW);
  }
  
  // Inst: Shift Head[Nop-F] by the value Pop[Nop-A]
  void Inst_OffsetHead() {
    Head & head = GetHeadArg(HEAD_FLOW);
    head.pos += GetStackArg(Nop::A).Pop();
  }


  // Initialize the state of the virtual CPU
  void Reset() {
    // Reset Heads
    heads[HEAD_IP].Reset();
    heads[HEAD_G_READ].Reset();
    heads[HEAD_G_WRITE].Reset();
    heads[HEAD_M_READ].Reset(0, false);
    heads[HEAD_M_WRITE].Reset(0, false);
    heads[HEAD_FLOW].Reset();

    // Reset the stacks.
    for (auto & stack : stacks) stack.Reset();

    cur_scope = 0;       // Move back to global scope.
    error_count = 0; // Reset all errors.


    // Load instructions
    emp::array<inst_fun_t, MAX_INSTS> inst_set;
    size_t num_insts = 0; // Number of instructions that are active.

    
  }

};