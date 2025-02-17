#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include <algorithm>
#include <utility>

#include "emp/base/array.hpp"
#include "emp/base/notify.hpp"
#include "emp/math/math.hpp"

#include "InstSet.hpp"
#include "VMHead.hpp"
#include "VMStack.hpp"

#include "../Genome.hpp"

/// Default Avida Virtual Machine for use in Avida 5
class AvidaVM {
private:
  // Configured values.
  static constexpr size_t NUM_NOPS = 6;           // Num nop modifier instructions used
  static constexpr size_t STACK_DEPTH = 16;       // Num entries on stack before it loops.
  static constexpr size_t MEM_SIZE = 64;          // How much physical memory is available?
  static constexpr size_t MAX_INSTS = 256;        // Max number of distinct instructions.
  static constexpr size_t MAX_GENOME_SIZE = 1024; // Max genome length.

  // Configured types.
  using data_t = int;                         // What data type does this VM use?
  using mem_t = emp::array<data_t, MEM_SIZE>; // Memory is a fixed size.
  using genome_t = StateGenome<MAX_INSTS>;    // Genomes can grow as needed.
  using inst_id_t = genome_t::state_t;        // Type used for inst IDs in a genome.
  using inst_set_t = InstSet<AvidaVM, 40>;    // Instruction set type for AvidaVM.
  using Stack = VMStack<data_t, STACK_DEPTH>; // Stacks to use in virtual CPU.

  enum class Nop {
    A = 0, B = 1, C = 2, D = 3, E = 4, F = 5,
  };

  enum HeadType {
    HEAD_IP = 0,      // Instruction Pointer (init: genome start)
    HEAD_G_READ = 1,  // Genome Read (init: genome start)
    HEAD_G_WRITE = 2, // Genome Write (init: genome start)
    HEAD_M_READ = 3,  // Memory Read (init: memory start)
    HEAD_M_WRITE = 4, // Memory Write (init: memory start)
    HEAD_FLOW = 5,    // Flow Control (init: genome start)
  };

  // Calculated values.
  static constexpr size_t DATA_BITS = sizeof(data_t)*8; // Number of bits in data_t;


  // === Hardware ===

  const inst_set_t & inst_set;
  genome_t genome;
  mem_t memory;
  size_t num_insts = 0; // Number of instructions that are active.

  emp::array<VMHead, NUM_NOPS> heads;
  emp::array<Stack, NUM_NOPS> stacks;
  size_t cur_scope = 0; // Start in the global scope.
  emp::array<size_t, NUM_NOPS> scope_starts{0};  // Track where each scope started.
  size_t error_count = 0;

  emp::String StatusString() const {
    emp::String out;
    out += "Genome: ";
    for (size_t i=0; i < genome.size(); ++i) {
      if (i) out += ',';
      out += genome[i];
    }
    out += "\nMemory: ";
    for (size_t i=0; i < memory.size(); ++i) {
      if (i) out += ',';
      out += memory[i];
    }
    out += "\nHeads: ";
    for (size_t i = 0; i < heads.size(); ++i) {
      if (i) out += ',';
      out += heads[i].ToString();
    }
    out += "\nStacks: ";
    for (size_t i = 0; i < stacks.size(); ++i ) {
      if (i) out += ',';
      out.Append(static_cast<char>('A' + i), ':', stacks[i].ToString());
    }
    out.Append("\ncur_scope = ", cur_scope);
    out.Append("\nerror_count = ", error_count);
    return out;
  }

  // =========== Helper Functions ============

  // Read the data found at the provided head and return it.
  data_t ReadHead(VMHead & head) {
    return head.on_genome ? head.Read(genome) : head.Read(memory);
  }

  // Write the provided data to the position pointed by the provided head.
  void WriteHead(VMHead & head, data_t data) {
    head.on_genome ? head.Write(data, genome) : head.Write(data, memory);
  }

  VMHead & IP() { return heads[HEAD_IP]; }
  data_t ReadIP() { return ReadHead(IP()); }
  void AdvanceIP() { ++IP(); }

  inst_id_t ToValidInst(data_t inst_val) const {
    return static_cast<inst_id_t>(emp::Mod(inst_val, num_insts));
  }

  inst_id_t ReadIPInst() { return ToValidInst(ReadIP()); }

  // Select the argument to use, overriding a nop if possible.
  size_t GetArg(size_t default_arg) {
    inst_id_t out_val = ReadIPInst();
    if (out_val < NUM_NOPS) {  // We found a nop.
      AdvanceIP();
      return out_val;
    }
    return default_arg; // We didn't find a nop.
  }

  size_t GetArg(Nop default_arg) { return GetArg(static_cast<data_t>(default_arg)); }
  Stack & GetStackArg(Nop default_arg) { return stacks[GetArg(default_arg)]; }
  Stack & GetStackArg(size_t default_arg) { return stacks[GetArg(default_arg)]; }
  VMHead & GetHeadArg(HeadType default_head) {
    return heads[GetArg(static_cast<size_t>(default_head))];
  }

public:
  AvidaVM() = delete;
  AvidaVM(const AvidaVM &) = default;
  AvidaVM(AvidaVM &&) = default;
  AvidaVM(const inst_set_t & inst_set) : inst_set(inst_set) { }
  // AvidaVM & operator=(const AvidaVM &) = default;
  // AvidaVM & operator=(AvidaVM &&) = default;


  // === Instructions ===

  // Inst: No-operation.
  void Inst_Nop()  { }

  // Inst: Push value [Nop-A] onto Stack [Nop-A]
  void Inst_Const() {
    static constexpr data_t const_vals[]{ 1, 2, 4, 16, 256, -1 }; // , 65536, 0, -2, 0, 0, 0};
    const data_t value = const_vals[GetArg(Nop::A)];
    GetStackArg(Nop::A).Push(value);
  }

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
      inst_id_t inst_scope_id = inst_set.GetID("Scope");
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
    VMHead & from_head = GetHeadArg(HEAD_G_READ);
    VMHead & to_head = GetHeadArg(HEAD_G_WRITE);
    WriteHead(to_head, ReadHead(from_head));
  }

  // Inst: Read value at Head [Nop-D]:X ; Push X onto stack [Nop-A] ; advance Head.
  void Inst_Load() {
    VMHead & from_head = GetHeadArg(HEAD_M_READ);
    GetStackArg(Nop::A).Push( ReadHead(from_head) );
  }

  // Inst: Pop[Nop-A] and write the value into Head [NopE] ; advance Head.
  void Inst_Store() {
    data_t value = GetStackArg(Nop::A).Pop();
    VMHead & to_head = GetHeadArg(HEAD_M_WRITE);
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
    VMHead & head = GetHeadArg(HEAD_FLOW);
    GetStackArg(Nop::A).Push(head.pos);
  }

  // Inst: Pop stack [Nop-A] and move head[Nop-F] to that position.
  void Inst_SetHead() {
    const data_t new_pos = GetStackArg(Nop::A).Pop();
    GetHeadArg(HEAD_FLOW).pos = new_pos;
  }

  // Inst: Jump Head[Nop-A] to Head[Nop-F]
  void Inst_JumpHead() {
    VMHead & jump_head = GetHeadArg(HEAD_IP);
    jump_head = GetHeadArg(HEAD_FLOW);
  }
  
  // Inst: Shift Head[Nop-F] by the value Pop[Nop-A]
  void Inst_OffsetHead() {
    VMHead & head = GetHeadArg(HEAD_FLOW);
    head.pos += GetStackArg(Nop::A).Pop();
  }

  void ProcessInst() {
    ProcessInst(IP().pos);
    IP().pos++;
  }

  void ProcessInst(size_t id) {
    switch (id) {
    case 0:                      // Nop-A
    case 1:                      // Nop-B
    case 2:                      // Nop-C
    case 3:                      // Nop-D
    case 4:                      // Nop-E
    case 5: Inst_Nop(); break;   // Nop-F

    case 6: Inst_Const(); break;
    case 7: Inst_Not(); break;
    case 8: Inst_Shift(); break;
    case 9: Inst_Add(); break;
    case 10: Inst_Sub(); break;
    case 11: Inst_Mult(); break;
    case 12: Inst_Div(); break;
    case 13: Inst_Mod(); break;
    case 14: Inst_Exp(); break;
    case 15: Inst_Sort(); break;
    case 16: Inst_TestLess(); break;
    case 17: Inst_TestEqu(); break;
    case 18: Inst_Nand(); break;
    case 19: Inst_Xor(); break;
    case 20: Inst_If(); break;
    case 21: Inst_Scope(); break;
    case 22: Inst_Continue(); break;
    case 23: Inst_Break(); break;
    case 24: Inst_StackPop(); break;
    case 25: Inst_StackDup(); break;
    case 26: Inst_StackSwap(); break;
    case 27: Inst_StackMove(); break;
    case 28: Inst_Copy(); break;
    case 29: Inst_Load(); break;
    case 30: Inst_Store(); break;
    case 31: Inst_Allocate(); break;
    case 32: Inst_DivideCell(); break;
    case 33: Inst_HeadPos(); break;
    case 34: Inst_SetHead(); break;
    case 35: Inst_JumpHead(); break;
    case 36: Inst_OffsetHead(); break;
    default:
      emp::notify::Error("Instruction ", id, " out of range.");
    }
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

    cur_scope = 0;    // Move back to global scope.
    error_count = 0;  // Reset all errors.
  }


  // === Static Functions for working with Avida VMs ===

  static inst_set_t BuildInstSet() {    
    inst_set_t inst_set;
    inst_set.AddNopInst("Nop-A");
    inst_set.AddNopInst("Nop-B");
    inst_set.AddNopInst("Nop-C");
    inst_set.AddNopInst("Nop-D");
    inst_set.AddNopInst("Nop-E");
    inst_set.AddNopInst("Nop-F");

    inst_set.AddInst("Const",      &AvidaVM::Inst_Const);
    inst_set.AddInst("Not",        &AvidaVM::Inst_Not);
    inst_set.AddInst("Shift",      &AvidaVM::Inst_Shift);
    inst_set.AddInst("Add",        &AvidaVM::Inst_Add);
    inst_set.AddInst("Sub",        &AvidaVM::Inst_Sub);
    inst_set.AddInst("Mult",       &AvidaVM::Inst_Mult);
    inst_set.AddInst("Div",        &AvidaVM::Inst_Div);
    inst_set.AddInst("Mod",        &AvidaVM::Inst_Mod);
    inst_set.AddInst("Exp",        &AvidaVM::Inst_Exp);
    inst_set.AddInst("Sort",       &AvidaVM::Inst_Sort);
    inst_set.AddInst("TestLess",   &AvidaVM::Inst_TestLess);
    inst_set.AddInst("TestEqu",    &AvidaVM::Inst_TestEqu);
    inst_set.AddInst("Nand",       &AvidaVM::Inst_Nand);
    inst_set.AddInst("Xor",        &AvidaVM::Inst_Xor);
    inst_set.AddInst("If",         &AvidaVM::Inst_If);
    inst_set.AddInst("Scope",      &AvidaVM::Inst_Scope);
    inst_set.AddInst("Continue",   &AvidaVM::Inst_Continue);
    inst_set.AddInst("Break",      &AvidaVM::Inst_Break);
    inst_set.AddInst("StackPop",   &AvidaVM::Inst_StackPop);
    inst_set.AddInst("StackDup",   &AvidaVM::Inst_StackDup);
    inst_set.AddInst("StackSwap",  &AvidaVM::Inst_StackSwap);
    inst_set.AddInst("StackMove",  &AvidaVM::Inst_StackMove);
    inst_set.AddInst("Copy",       &AvidaVM::Inst_Copy);
    inst_set.AddInst("Load",       &AvidaVM::Inst_Load);
    inst_set.AddInst("Store",      &AvidaVM::Inst_Store);
    inst_set.AddInst("Allocate",   &AvidaVM::Inst_Allocate);
    inst_set.AddInst("DivideCell", &AvidaVM::Inst_DivideCell);
    inst_set.AddInst("HeadPos",    &AvidaVM::Inst_HeadPos);
    inst_set.AddInst("SetHead",    &AvidaVM::Inst_SetHead);
    inst_set.AddInst("JumpHead",   &AvidaVM::Inst_JumpHead);
    inst_set.AddInst("OffsetHead", &AvidaVM::Inst_OffsetHead);

    return inst_set;
  }
};