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
  static constexpr size_t MAX_GENOME_SIZE = 2048; // Max genome length.

  // Configured types.
  using data_t = int;                             // What data type does this VM use?
  using mem_t = emp::array<data_t, MEM_SIZE>;     // Memory is a fixed size.
  using inst_id_t = Genome::value_t;              // Type used for inst IDs in a genome.
  using inst_set_t = InstSet<AvidaVM, MAX_INSTS>; // Instruction set type for AvidaVM.
  using Stack = VMStack<data_t, STACK_DEPTH>;     // Stacks to use in virtual CPU.

  enum class Nop {
    A = 0, B = 1, C = 2, D = 3, E = 4, F = 5,
  };

  // Heads are assumed to be either on the genome or memory and cannot shift.
  enum HeadType {
    HEAD_IP = 0,      // Inst. Pointer (buffer: genome; init: start)
    HEAD_G_READ = 1,  // Genome Read   (buffer: genome; init: start)
    HEAD_G_WRITE = 2, // Genome Write  (buffer: genome; init: end)
    HEAD_M_READ = 3,  // Memory Read   (buffer: memory; init: start)
    HEAD_M_WRITE = 4, // Memory Write  (buffer: memory; init: start)
    HEAD_FLOW = 5,    // Flow Control  (buffer: genome; init: start)
  };

  static constexpr data_t const_vals[]{ 1, 2, 4, 16, 256, -1 };

  // Calculated values.
  static constexpr size_t DATA_BITS = sizeof(data_t)*8; // Number of bits in data_t;


  // === Hardware ===

  const inst_set_t & inst_set;
  Genome genome;
  Genome offspring;  // Offspring waiting to be placed.
  mem_t memory;

  emp::array<VMHead, NUM_NOPS> heads;
  emp::array<Stack, NUM_NOPS> stacks;
  size_t error_count = 0;

  // =========== Helper Functions ============

  // Read the data found at the provided head and return it.
  [[nodiscard]] data_t ReadHead(const VMHead & head) const {
    return head.on_genome ? head.Read(genome) : head.Read(memory);
  }

  // Write the provided data to the position pointed by the provided head.
  void WriteHead(VMHead & head, data_t data) {
    head.on_genome ? head.Write(data, genome) : head.Write(data, memory);
  }

  [[nodiscard]] VMHead & IP() { return heads[HEAD_IP]; }
  [[nodiscard]] const VMHead & IP() const { return heads[HEAD_IP]; }
  [[nodiscard]] data_t ReadIP() const { return ReadHead(IP()); }
  void AdvanceIP() { ++IP(); }

  [[nodiscard]] inst_id_t ToValidInst(data_t inst_val) const {
    return static_cast<inst_id_t>(emp::Mod(inst_val, inst_set.size()));
  }

  [[nodiscard]] inst_id_t ReadIPInst() const { return ToValidInst(ReadIP()); }

  // Select the argument to use, overriding a nop if possible.
  [[nodiscard]] size_t GetArg(size_t default_arg) {
    inst_id_t out_val = ReadIPInst();
    if (out_val < NUM_NOPS) {  // We found a nop.
      AdvanceIP();
      return out_val;
    }
    return default_arg; // We didn't find a nop.
  }

  [[nodiscard]] size_t GetArg(Nop default_arg) { return GetArg(static_cast<data_t>(default_arg)); }
  [[nodiscard]] Stack & GetStackArg(Nop default_arg) { return stacks[GetArg(default_arg)]; }
  [[nodiscard]] Stack & GetStackArg(size_t default_arg) { return stacks[GetArg(default_arg)]; }
  [[nodiscard]] VMHead & GetHeadArg(HeadType default_head) {
    return heads[GetArg(static_cast<size_t>(default_head))];
  }

  // Does the current instruction have the specified argument?
  [[nodiscard]] bool HasArg(inst_id_t arg) {
    for (size_t pos = IP().pos + 1;
         pos < genome.size() && genome[pos] < NUM_NOPS;
         ++pos) {
      if (genome[pos] == arg) return true;
    }
    return false;
  }

  // Is the IP currently at a "Scope" instruction for the target scope?
  [[nodiscard]] bool AtScopeLimit(inst_id_t target_scope) {
    static const inst_id_t inst_scope_id = inst_set.GetID("Scope");
    return (ReadIP() == inst_scope_id) && HasArg(target_scope);
  }

  void SkipNops() {
    while (IP().pos < genome.size() && genome[IP().pos] < NUM_NOPS) AdvanceIP();
  }

public:
  AvidaVM() = delete;
  AvidaVM(const AvidaVM &) = default;
  AvidaVM(AvidaVM &&) = default;
  AvidaVM(const inst_set_t & inst_set, const Genome & genome)
    : inst_set(inst_set), genome(genome) { Reset(); }
  // AvidaVM & operator=(const AvidaVM &) = default;
  // AvidaVM & operator=(AvidaVM &&) = default;


  // === Instructions ===

  // Inst: No-operation.
  void Inst_Nop()  { }

  // Inst: Push value [Nop-A] onto Stack [Nop-A]
  void Inst_Const() {
    const data_t value = const_vals[GetArg(Nop::A)];
    GetStackArg(Nop::A).Push(value);
  }

  // Inst: Y = Value[Nop-A] ; Pop[Nop-A]:Y ; Push[Arg2] X + Y.
  void Inst_Offset() {
    const data_t X = const_vals[GetArg(Nop::A)];
    size_t Y_id = GetArg(Nop::A);
    const data_t Y = stacks[Y_id].Pop();
    GetStackArg(Y_id).Push(X + Y);
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

  // Inst: Pop[Nop-A]:X ; if X != 0, skip next instruction
  void Inst_IfNot() {
    const data_t X = GetStackArg(Nop::A).Pop();
    if (X) AdvanceIP();
  }

  // Inst: A marker for scope beginnings and ends; each nop to follow indicates a scope break.
  void Inst_Scope() {  }

  // Inst: Restart Scope [Nop-A]
  void Inst_Continue() {
    IP().pos -= 2; // Scan backward for a "Scope" from this position.
    const inst_id_t target_scope = GetArg(Nop::A);

    // Scan backwards to find beginning of target scope.
    while (IP().pos < genome.size()) {
      // Test if this is a Scope instruction and if it starts the target scope.
      if (AtScopeLimit(target_scope)) {
        AdvanceIP();
        SkipNops(); // IP() should jump past Nops on the Scope instruction.
        return;
      }
      --IP().pos;
    }
  }

  // Inst: Advance to end of Scope [Nop-A]
  void Inst_Break() {
    const size_t target_scope = GetArg(Nop::A);

    // Scan find end of target scope.
    while (IP().pos < genome.size()) {
      // Test if this is a Scope instruction and if it starts the target scope.
      if (AtScopeLimit(target_scope)) {
        SkipNops(); // IP() should jump past Nops on the Scope instruction.
        return;
      }
      AdvanceIP();
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
    ++from_head;
    ++to_head;
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

  // Inst: Split off space between Head[Nop-B] and Head[Nop-C], resetting both.
  void Inst_DivideCell() {
    VMHead & head1 = GetHeadArg(HEAD_G_READ);
    VMHead & head2 = GetHeadArg(HEAD_G_WRITE);

    auto start_pos = head1.pos;
    auto stop_pos = head2.pos;
    if (stop_pos < start_pos) std::swap(start_pos, stop_pos);
    if (stop_pos > genome.size()) stop_pos = genome.size();

    if (!head1.on_genome || !head2.on_genome || // Both heads must be on the genome
        start_pos >= genome.size() ||           // Start pos cannot be past end of genome
        start_pos == stop_pos) {                // Must be space between heads
      ++error_count;
      return;      
    }

    // Extract the offspring from the genome.
    offspring = genome.Extract(start_pos, stop_pos - start_pos);

    // Reset the heads.
    head1.pos = 0;
    head2.pos = genome.size();
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
    const auto inst_id = ReadIPInst();
    AdvanceIP();
    inst_set.Execute(*this, inst_id);
    // ProcessInst(inst_id);
  }

  void ProcessInst(size_t id) {
    switch (id) {
    case 0:          // Nop-A
    case 1:          // Nop-B
    case 2:          // Nop-C
    case 3:          // Nop-D
    case 4:          // Nop-E
    case 5: break;   // Nop-F

    case 6: Inst_Const(); break;
    case 7: Inst_Offset(); break;
    case 8: Inst_Not(); break;
    case 9: Inst_Shift(); break;
    case 10: Inst_Add(); break;
    case 11: Inst_Sub(); break;
    case 12: Inst_Mult(); break;
    case 13: Inst_Div(); break;
    case 14: Inst_Mod(); break;
    case 15: Inst_Exp(); break;
    case 16: Inst_Sort(); break;
    case 17: Inst_TestLess(); break;
    case 18: Inst_TestEqu(); break;
    case 19: Inst_Nand(); break;
    case 20: Inst_Xor(); break;
    case 21: Inst_If(); break;
    case 22: Inst_IfNot(); break;
    case 23: Inst_Scope(); break;
    case 24: Inst_Continue(); break;
    case 25: Inst_Break(); break;
    case 26: Inst_StackPop(); break;
    case 27: Inst_StackDup(); break;
    case 28: Inst_StackSwap(); break;
    case 29: Inst_StackMove(); break;
    case 30: Inst_Copy(); break;
    case 31: Inst_Load(); break;
    case 32: Inst_Store(); break;
    case 33: Inst_DivideCell(); break;
    case 34: Inst_HeadPos(); break;
    case 35: Inst_SetHead(); break;
    case 36: Inst_JumpHead(); break;
    case 37: Inst_OffsetHead(); break;
    default:
      emp::notify::Error("Instruction ", id, " out of range.");
    }
  }

   // Initialize the state of the virtual CPU
  void Reset() {
    // Reset offspring
    offspring.Resize(0);

    // Reset Heads
    heads[HEAD_IP].Reset();
    heads[HEAD_G_READ].Reset();
    heads[HEAD_G_WRITE].Reset(genome.size());
    heads[HEAD_M_READ].Reset(0, false);
    heads[HEAD_M_WRITE].Reset(0, false);
    heads[HEAD_FLOW].Reset();

    // Reset the stacks.
    for (auto & stack : stacks) stack.Reset();

    error_count = 0;  // Reset all errors.
  }

  // Reset with a new genome.
  void Reset(const Genome & in_genome) {
    genome = in_genome;
    Reset();
  }



  // === Static Functions for working with Avida VMs ===

  [[nodiscard]] static inst_set_t BuildInstSet() {    
    inst_set_t inst_set;
    inst_set.AddNopInst("Nop-A");
    inst_set.AddNopInst("Nop-B");
    inst_set.AddNopInst("Nop-C");
    inst_set.AddNopInst("Nop-D");
    inst_set.AddNopInst("Nop-E");
    inst_set.AddNopInst("Nop-F");

    inst_set.AddInst("Const",      &AvidaVM::Inst_Const);
    inst_set.AddInst("Offset",     &AvidaVM::Inst_Offset);
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
    inst_set.AddInst("IfNot",      &AvidaVM::Inst_IfNot);
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
    inst_set.AddInst("DivideCell", &AvidaVM::Inst_DivideCell);
    inst_set.AddInst("HeadPos",    &AvidaVM::Inst_HeadPos);
    inst_set.AddInst("SetHead",    &AvidaVM::Inst_SetHead);
    inst_set.AddInst("JumpHead",   &AvidaVM::Inst_JumpHead);
    inst_set.AddInst("OffsetHead", &AvidaVM::Inst_OffsetHead);

    return inst_set;
  }

  /////////////////////////////////////////////
  //
  //  Functions for string-based information
  //

  [[nodiscard]] emp::String NextInstName() const {
    return inst_set.GetName(ReadIPInst());
  }

  [[nodiscard]] char NextInstSymbol() const {
    return inst_set.GetSymbol(ReadIPInst());
  }

  [[nodiscard]] emp::String StatusString() const {
    emp::String out = inst_set.ToSequence(genome);
    if (IP().pos < out.size()) {
      out.insert(IP().pos, ">");
    }
    out.insert(0, "Genome: ");
    out += "\nMemory: ";
    for (size_t i=0; i < memory.size(); ++i) {
      if (i) out += ',';
      out.Append(static_cast<int>(memory[i]));
    }
    out += "\nHeads: ";
    for (size_t i = 0; i < heads.size(); ++i) {
      if (i) out += ',';
      out += heads[i].ToString();
    }
    out += "\nStacks: ";
    for (size_t i = 0; i < stacks.size(); ++i ) {
      if (i) out += "; ";
      out.Append(static_cast<char>('A' + i), ':', stacks[i].ToString());
    }
    out.Append("\nerror_count = ", error_count);
    out.Append("\nNEXT >>>>>>>>>>>> ", NextInstName(), " [", NextInstSymbol(), "]");
    return out;
  }
};