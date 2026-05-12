#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include <algorithm>
#include <cstddef>     // for size_t
#include <functional>  // for std::function
#include <tuple>
#include <utility>

#include "emp/base/array.hpp"
#include "emp/base/notify.hpp"
#include "emp/base/Ptr.hpp"
#include "emp/math/math.hpp"
#include "emp/tools/String.hpp"

#include "InstSet.hpp"
#include "VMStack.hpp"

#include "../core/Genome.hpp"
#include "../core/OrganismBase.hpp"

/// Default Avida Virtual Machine for use in Avida 5
class AvidaVM {
public:
  // Configured values.
  static constexpr size_t NUM_NOPS = 6;           // Num nop modifier instructions used
  static constexpr size_t STACK_DEPTH = 16;       // Num entries on stack before it loops.
  static constexpr size_t MEM_SIZE = 64;          // How much physical memory is available?
  static constexpr size_t MAX_INSTS = 64;         // Max number of distinct instructions.
  static constexpr size_t MAX_GENOME_SIZE = 2048; // Max genome length.

  // Configured types.
  using data_t = int;                             // Data type used by this VM
  using mem_t = emp::array<data_t, MEM_SIZE>;     // Memory is a fixed size
  using genome_t = Genome<uint8_t>;               // Genomes capped at 256 instructions
  using inst_set_t = InstSet<AvidaVM, MAX_INSTS>; // Instruction set type for AvidaVM
  using inst_id_t = typename genome_t::value_t;   // Type used for inst IDs in a genome
  using Stack = VMStack<data_t, STACK_DEPTH>;     // Stacks to use in virtual CPU
  using callback_t = void (*)(AvidaVM &);         // Special functions added to inst set

  enum class Nop {
    A = 0, B = 1, C = 2, D = 3, E = 4, F = 5, // Nops referring to specific stacks or heads
    FIRST_ARG, SECOND_ARG, NEXT_ARG           // Specialty nops to refer to other arguments
  };

  // Heads are assumed to be either on the genome or memory and cannot shift.
  enum HeadType {
    HEAD_IP = 0,      // A: Inst. Pointer (init: 0)
    HEAD_G_READ = 1,  // B: Genome Read   (init: 0)
    HEAD_G_WRITE = 2, // C: Genome Write  (init: genome size)
    HEAD_M_READ = 3,  // D: Memory Read   (init: 0)
    HEAD_M_WRITE = 4, // E: Memory Write  (init: 0)
    HEAD_FLOW = 5,    // F: Flow Control  (init: 0)
  };

  static constexpr data_t const_vals[]{ 1, 2, 4, 16, 256, -1 };

  // Calculated values.
  static constexpr size_t DATA_BITS = sizeof(data_t)*8; // Number of bits in data_t;

private:
  genome_t genome;              // Genome of this organism, being executed.
  mem_t memory{};               // Storage of values being manipulated by this organism.
  emp::Ptr<const inst_set_t> inst_set_ptr = nullptr;  // Map of instruction names to functionality.

  emp::array<size_t, NUM_NOPS> heads;
  emp::array<Stack, NUM_NOPS> stacks;
  size_t error_count = 0;
  size_t biota_id;              // ID of organism from the biota.

  // =========== Helper Functions ============

  /// Read from a position in the genome.
  [[nodiscard]] inst_id_t ReadGenome(const size_t pos) const {
    return (pos < genome.size()) ? genome[pos] : 0;
  }

  /// Read from a position in the memory.
  [[nodiscard]] data_t ReadMemory(const size_t pos) const {
    return (pos < memory.size()) ? memory[pos] : 0;
  }

  /// Write to the genome (always an insertion)
  void WriteGenome(const size_t pos, inst_id_t id) {
    if (genome.size() >= MAX_GENOME_SIZE) {
      ++error_count;
      return;
    }
    if (pos < genome.size()) genome.Insert(pos, id);
    else genome.Push(id); // At or past end, just push on back (most common)
  }

  /// Write to the memory
  void WriteMemory(const size_t pos, data_t data) {
    if (pos < memory.size()) memory[pos] = data;
    else ++error_count; // Writing outside of range.
  }

  /// Get the current position of the instruction pointer.
  [[nodiscard]] size_t & IP() { return heads[HEAD_IP]; }
  [[nodiscard]] size_t IP() const { return heads[HEAD_IP]; }

  [[nodiscard]] inst_id_t ReadIP() const { return ReadGenome(IP()); }
  void AdvanceIP() { ++IP(); }

  /// Determine an instruction argument: if next instruction is a Nop, use it; else use default_arg
  [[nodiscard]] inst_id_t GetArg(inst_id_t default_arg) {
    emp_assert(default_arg < NUM_NOPS);
    inst_id_t out_val = ReadIP();
    if (out_val >= NUM_NOPS) return default_arg; // Not a Nop
    AdvanceIP();
    return out_val;
  }

  /// Determine an instruction argument: if next instruction is a Nop, use it; else use DEFAULT_ARG
  template <Nop DEFAULT_ARG>
  [[nodiscard]] inst_id_t GetArg() {
    static_assert(static_cast<size_t>(DEFAULT_ARG) < NUM_NOPS,
                  "GetArg<DEFAULT_ARG>: DEFAULT_ARG must be a real Nop (A..F).");
    inst_id_t out_val = ReadIP();
    if (out_val >= NUM_NOPS) return static_cast<inst_id_t>(DEFAULT_ARG); // Not a Nop
    AdvanceIP();
    return out_val;
  }

  /// Determine TWO instruction arguments: shift to defaults when we are out of Nops.
  template <Nop DEFAULT_ARG1, Nop DEFAULT_ARG2>
  [[nodiscard]] auto GetArgs() {
    static_assert(DEFAULT_ARG2 != Nop::SECOND_ARG, "Second instruction Arg cannot default to itself.");

    const inst_id_t arg1 = GetArg<DEFAULT_ARG1>();
    if constexpr (DEFAULT_ARG2 == Nop::FIRST_ARG) {
      return std::tuple{arg1, GetArg(arg1)};
    } else if constexpr (DEFAULT_ARG2 == Nop::NEXT_ARG) {
      return std::tuple{arg1, GetArg((arg1+1)%NUM_NOPS)};
    } else {
      return std::tuple{arg1, GetArg<DEFAULT_ARG2>()};
    }
  }

  /// Determine THREE instruction arguments: shift to defaults when we are out of Nops.
  template <Nop DEFAULT_ARG1, Nop DEFAULT_ARG2, Nop DEFAULT_ARG3>
  [[nodiscard]] auto GetArgs() {
    const auto [arg1, arg2] = GetArgs<DEFAULT_ARG1, DEFAULT_ARG2>();

    // Otherwise determine execute a proper tuple based on the third arg:
    if constexpr (DEFAULT_ARG3 == Nop::FIRST_ARG) {
      return std::tuple{arg1, arg2, GetArg(arg1)};
    }
    else if constexpr (DEFAULT_ARG3 == Nop::SECOND_ARG) {
      return std::tuple{arg1, arg2, GetArg(arg2)};
    }
    else if constexpr (DEFAULT_ARG3 == Nop::NEXT_ARG) {
      return std::tuple{arg1, arg2, GetArg((arg2+1)%NUM_NOPS)};
    }
    else { // Arg3 has a specified default.
      return std::tuple{arg1, arg2, GetArg<DEFAULT_ARG3>()};
    }
  }

  // Does the current instruction have the specified argument?
  [[nodiscard]] bool HasArg(inst_id_t arg, size_t max_args=MAX_GENOME_SIZE) const {
    for (size_t pos = IP() + 1;
        pos < genome.size() && max_args-- && genome[pos] < NUM_NOPS;
        ++pos) {
      if (genome[pos] == arg) return true;
    }
    return false;
  }

  // Is the IP currently at a "Scope" instruction for the target scope?
  [[nodiscard]] bool AtScopeLimit(inst_id_t target_scope) const {
    static const inst_id_t inst_scope_id = GetInstSet().GetID("Scope");
    emp_assert(inst_scope_id == GetInstSet().GetID("Scope"),
              "InstSet mismatch for Scope id across AvidaVM instances.");
    return (ReadIP() == inst_scope_id) && HasArg(target_scope, 3);
  }

  void SkipNops(size_t max_skipped=1000) {
    while (IP() < genome.size() &&
          max_skipped-- &&
          genome[IP()] < NUM_NOPS) AdvanceIP();
  }

public:
  AvidaVM() = default;
  AvidaVM(const AvidaVM &) = delete;
  AvidaVM(AvidaVM &&) = default;
  AvidaVM(const inst_set_t & inst_set, const genome_t & genome=genome_t{})
    : genome(genome), inst_set_ptr(&inst_set) { Reset(); }

  // === Accessors ===
  
  [[nodiscard]] size_t GetBiotaID() const { return biota_id; }
  AvidaVM & SetBiotaID(size_t in) { biota_id = in; return *this; }

  [[nodiscard]] const inst_set_t & GetInstSet() const { emp_assert(inst_set_ptr); return *inst_set_ptr; }
  AvidaVM & SetInstSet(const inst_set_t & is) { inst_set_ptr = &is; return *this; }

  // === Instructions ===
  // All instruction implementations live in AvidaVM_Insts (defined after this class).
  friend struct AvidaVM_Insts;

  void ProcessStep() {
    emp_assert(OK());
    const inst_id_t exec_id = ReadIP();
    AdvanceIP();
    GetInstSet().Execute(*this, exec_id);
  }

  void Trace(size_t cpu_cycles=200, std::ostream & os=std::cout) {
    for (size_t i = 0; i <= cpu_cycles; ++i) {
      if (i) ProcessStep();
      std::println(os, "STEP {}: {}", i, StatusString());
    }
  }

  // Initialize the state of the virtual CPU
  void Reset() {
    // Reset Heads
    heads[HEAD_IP] = 0;
    heads[HEAD_G_READ] = 0;
    heads[HEAD_G_WRITE] = genome.size();
    heads[HEAD_M_READ] = 0;
    heads[HEAD_M_WRITE] = 0;
    heads[HEAD_FLOW] = 0;

    // Reset the stacks.
    for (auto & stack : stacks) stack.Reset();

    error_count = 0;  // Reset all errors.
  }

  // Reset with a new genome.
  void Reset(const genome_t & in_genome) {
    genome = in_genome;
    Reset();
  }

  genome_t GetOffspringGenome() {
    const auto [head1_id, head2_id] = GetArgs<Nop::B, Nop::C>();

    size_t & head1 = heads[head1_id];
    size_t & head2 = heads[head2_id];

    if (head2 < head1) std::swap(head1, head2);
    if (head2 > genome.size()) head2 = genome.size();

    if (head1 >= genome.size() ||  // Start pos cannot be past end of genome
        head1 == head2) {          // Must be space between heads
      ++error_count;
      return genome_t{};
    }

    // Extract the offspring from the genome.
    genome_t offspring = genome.Extract(head1, head2 - head1);

    // Reset the heads.
    head2 = head1;  // Move head2 to the beginning of the extracted position (likely org end)
    head1 = 0;      // Move head1 to the beginning of the genome

    return offspring;
  }


  // === Static Functions for working with Avida VMs ===

  [[nodiscard]] static emp::String HardwareName() { return "AvidaVM"; }

  static void BuildInstSet(inst_set_t & inst_set);

  static bool AddCallback(inst_set_t & inst_set, emp::String name, callback_t callback_fun) {
    inst_set.AddInst(name, callback_fun);
    return true;
  }

  /////////////////////////////////////////////
  //
  //  Functions for string-based information
  //

  [[nodiscard]] emp::String NextInstName() const {
    return GetInstSet().GetName(ReadIP());
  }

  [[nodiscard]] char NextInstSymbol() const {
    return GetInstSet().GetSymbol(ReadIP());
  }

  [[nodiscard]] emp::String GetGenomeString() const { return GetInstSet().ToSequence(genome); }

  [[nodiscard]] emp::String StatusString() const {
    emp::String out = GetGenomeString();
    if (IP() < out.size()) {
      out.insert(IP(), ">");
    }
    out.insert(0, "Genome: ");
    out += "\nMemory: ";
    for (size_t i=0; i < memory.size(); ++i) {
      if (i) out += ',';
      out.Append(static_cast<int>(memory[i]));
    }
    out.Append("\nHeads: IP:", IP(),
      " GenRead:", heads[1],
      " GenWrite:", heads[2],
      " MemRead:", heads[3],
      " MemWrite:", heads[4],
      " Flow:", heads[5]);
    out += "\nStacks: ";
    for (size_t i = 0; i < stacks.size(); ++i ) {
      if (i) out += "; ";
      out.Append(static_cast<char>('A' + i), ':', stacks[i].ToString());
    }
    out.Append("\nerror_count = ", error_count);
    out.Append("\nNEXT >>>>>>>>>>>> ", NextInstName(), " [", NextInstSymbol(), "]");
    return out;
  }


  bool OK() const {
    return true;
  }
};

// Instruction implementations for AvidaVM.
// Free functions taking AvidaVM & allow storage as plain function pointers in InstSet.
struct AvidaVM_Insts {
  using vm_t = AvidaVM;

  // Inst: No-operation.
  static void Nop(vm_t &) { }

  // Inst: Push value [Nop-A] onto Stack [Nop-A]
  static void Const(vm_t & vm) {
    const auto [val_id, stack_id] = vm.GetArgs<vm_t::Nop::A, vm_t::Nop::A>();
    vm.stacks[stack_id].Push(vm_t::const_vals[val_id]);
  }

  // Inst: X = Value[Nop-A] ; Y = Pop[Nop-A] ; Push[Arg2]: X + Y.
  static void Offset(vm_t & vm) {
    const auto [val_id, Y_id, output_id] = vm.GetArgs<vm_t::Nop::A, vm_t::Nop::A, vm_t::Nop::SECOND_ARG>();
    vm.stacks[output_id].Push(vm_t::const_vals[val_id] + vm.stacks[Y_id].Pop());
  }

  // Inst: X = Pop[Nop-A] ; Push[Arg1] : !X
  static void Not(vm_t & vm) {
    const auto [X_id, output_id] = vm.GetArgs<vm_t::Nop::A, vm_t::Nop::FIRST_ARG>();
    vm.stacks[output_id].Push(!vm.stacks[X_id].Pop());
  }

  // Inst: X = Pop[Nop-A] ; Y = Pop[Arg1] ; Push[Arg1] : X<<Y
  static void Shift(vm_t & vm) {
    const auto [X_id, Y_id, output_id] = vm.GetArgs<vm_t::Nop::A, vm_t::Nop::FIRST_ARG, vm_t::Nop::FIRST_ARG>();
    const size_t X = vm.stacks[X_id].Pop();
    const size_t Y = vm.stacks[Y_id].Pop();
    vm.stacks[output_id].Push(X << (Y % vm_t::DATA_BITS));
  }

  // Inst: X = Pop[Nop-A] ; Y = Pop[Arg1] ; Push[Arg1] : X + Y
  static void Add(vm_t & vm) {
    const auto [X_id, Y_id, output_id] = vm.GetArgs<vm_t::Nop::A, vm_t::Nop::FIRST_ARG, vm_t::Nop::FIRST_ARG>();
    vm.stacks[output_id].Push(vm.stacks[X_id].Pop() + vm.stacks[Y_id].Pop());
  }

  // Inst: X = Pop[Nop-A] ; Y = Pop[Arg1] ; Push[Arg1] : X - Y
  static void Sub(vm_t & vm) {
    const auto [X_id, Y_id, output_id] = vm.GetArgs<vm_t::Nop::A, vm_t::Nop::FIRST_ARG, vm_t::Nop::FIRST_ARG>();
    vm.stacks[output_id].Push(vm.stacks[X_id].Pop() - vm.stacks[Y_id].Pop());
  }

  // Inst: X = Pop[Nop-A] ; Y = Pop[Arg1] ; Push[Arg1] : X * Y
  static void Mult(vm_t & vm) {
    const auto [X_id, Y_id, output_id] = vm.GetArgs<vm_t::Nop::A, vm_t::Nop::FIRST_ARG, vm_t::Nop::FIRST_ARG>();
    vm.stacks[output_id].Push(vm.stacks[X_id].Pop() * vm.stacks[Y_id].Pop());
  }

  // Inst: X = Pop[Nop-A] ; Y = Pop[Arg1] ; Push[Arg1] : X / Y  (error on Y==0)
  static void Div(vm_t & vm) {
    const auto [X_id, Y_id, output_id] = vm.GetArgs<vm_t::Nop::A, vm_t::Nop::FIRST_ARG, vm_t::Nop::FIRST_ARG>();
    const vm_t::data_t X = vm.stacks[X_id].Pop();
    const vm_t::data_t Y = vm.stacks[Y_id].Pop();
    if (Y == 0) ++vm.error_count;
    else vm.stacks[output_id].Push(X / Y);
  }

  // Inst: X = Pop[Nop-A] ; Y = Pop[Arg1] ; Push[Arg1] : X % Y  (error on Y==0)
  static void Mod(vm_t & vm) {
    const auto [X_id, Y_id, output_id] = vm.GetArgs<vm_t::Nop::A, vm_t::Nop::FIRST_ARG, vm_t::Nop::FIRST_ARG>();
    const vm_t::data_t X = vm.stacks[X_id].Pop();
    const vm_t::data_t Y = vm.stacks[Y_id].Pop();
    if (Y == 0) ++vm.error_count;
    else vm.stacks[output_id].Push(X % Y);
  }

  // Inst: X = Pop[Nop-A] ; Y = Pop[Arg1] ; Push[Arg1] : X ** Y
  static void Exp(vm_t & vm) {
    const auto [X_id, Y_id, output_id] = vm.GetArgs<vm_t::Nop::A, vm_t::Nop::FIRST_ARG, vm_t::Nop::FIRST_ARG>();
    vm.stacks[output_id].Push(emp::Pow(vm.stacks[X_id].Pop(), vm.stacks[Y_id].Pop()));
  }

  // Inst: X = Pop[Nop-A] ; Y = Pop[Arg1] ; push back, swapping if X < Y.
  static void Sort(vm_t & vm) {
    const auto [X_id, Y_id] = vm.GetArgs<vm_t::Nop::A, vm_t::Nop::FIRST_ARG>();
    vm_t::data_t X = vm.stacks[X_id].Pop();
    vm_t::data_t Y = vm.stacks[Y_id].Pop();
    if (X < Y) std::swap(X, Y);
    vm.stacks[Y_id].Push(Y);
    vm.stacks[X_id].Push(X);
  }

  // Inst: X = Pop[Nop-A] ; Y = Pop[Arg1] ; Push[Arg1] : X < Y
  static void TestLess(vm_t & vm) {
    const auto [X_id, Y_id, output_id] = vm.GetArgs<vm_t::Nop::A, vm_t::Nop::FIRST_ARG, vm_t::Nop::FIRST_ARG>();
    vm.stacks[output_id].Push(vm.stacks[X_id].Pop() < vm.stacks[Y_id].Pop());
  }

  // Inst: X = Pop[Nop-A] ; Y = Pop[Arg1] ; Push[Arg1] : X == Y
  static void TestEqu(vm_t & vm) {
    const auto [X_id, Y_id, output_id] = vm.GetArgs<vm_t::Nop::A, vm_t::Nop::FIRST_ARG, vm_t::Nop::FIRST_ARG>();
    vm.stacks[output_id].Push(vm.stacks[X_id].Pop() == vm.stacks[Y_id].Pop());
  }

  // Inst: X = Pop[Nop-A] ; Y = Pop[Arg1] ; Push[Arg1] : ~(X&Y)  (bitwise)
  static void Nand(vm_t & vm) {
    const auto [X_id, Y_id, output_id] = vm.GetArgs<vm_t::Nop::A, vm_t::Nop::FIRST_ARG, vm_t::Nop::FIRST_ARG>();
    vm.stacks[output_id].Push(~(vm.stacks[X_id].Pop() & vm.stacks[Y_id].Pop()));
  }

  // Inst: X = Pop[Nop-A] ; Y = Pop[Arg1] ; Push[Arg1] : X ^ Y   (bitwise)
  static void Xor(vm_t & vm) {
    const auto [X_id, Y_id, output_id] = vm.GetArgs<vm_t::Nop::A, vm_t::Nop::FIRST_ARG, vm_t::Nop::FIRST_ARG>();
    vm.stacks[output_id].Push(vm.stacks[X_id].Pop() ^ vm.stacks[Y_id].Pop());
  }

  // Inst: X = Pop[Nop-A] ; if X == 0, skip next instruction
  static void If(vm_t & vm) {
    if (!vm.stacks[vm.GetArg<vm_t::Nop::A>()].Pop()) vm.AdvanceIP();
  }

  // Inst: X = Pop[Nop-A] ; if X != 0, skip next instruction
  static void IfNot(vm_t & vm) {
    if (vm.stacks[vm.GetArg<vm_t::Nop::A>()].Pop()) vm.AdvanceIP();
  }

  // Inst: A marker for scope beginnings and ends; nops indicate scope break.
  static void Scope(vm_t & vm) {
    vm.SkipNops(3);
  }

  // Inst: Restart Scope [Nop-A]
  static void Continue(vm_t & vm) {
    const auto target_scope = vm.GetArg<vm_t::Nop::A>();
    vm.IP() -= 2;
    while (vm.IP() < vm.genome.size()) {
      if (vm.AtScopeLimit(target_scope)) {
        vm.AdvanceIP();
        vm.SkipNops();
        return;
      }
      --vm.IP();
    }
    vm.IP() = 0;
  }

  // Inst: Advance to end of Scope [Nop-A]
  static void Break(vm_t & vm) {
    const auto target_scope = vm.GetArg<vm_t::Nop::A>();
    while (vm.IP() < vm.genome.size()) {
      if (vm.AtScopeLimit(target_scope)) { vm.SkipNops(); return; }
      vm.AdvanceIP();
    }
  }

  // Inst: Discard top entry from Stack [Nop-A]
  static void StackPop(vm_t & vm) {
    vm.stacks[vm.GetArg<vm_t::Nop::A>()].Pop();
  }

  // Inst: Read top of Stack [Nop-A] (no popping) and push a copy on [Arg1]
  static void StackDup(vm_t & vm) {
    const auto [read_id, push_id] = vm.GetArgs<vm_t::Nop::A, vm_t::Nop::FIRST_ARG>();
    vm.stacks[push_id].Push(vm.stacks[read_id].Top());
  }

  // Inst: X = Pop[Nop-A] ; Y = Pop[Arg1]; push back in reverse order.
  static void StackSwap(vm_t & vm) {
    const auto [s1, s2] = vm.GetArgs<vm_t::Nop::A, vm_t::Nop::FIRST_ARG>();
    const vm_t::data_t X = vm.stacks[s1].Pop();
    const vm_t::data_t Y = vm.stacks[s2].Pop();
    vm.stacks[s2].Push(X);
    vm.stacks[s1].Push(Y);
  }

  // Inst: X = Pop[Nop-A] and Push[Arg1+1] X
  static void StackMove(vm_t & vm) {
    const auto [s1, s2] = vm.GetArgs<vm_t::Nop::A, vm_t::Nop::NEXT_ARG>();
    if (s1 != s2) vm.stacks[s2].Push(vm.stacks[s1].Pop());
  }

  // Inst: Copy the value from Head [Nop-B] to Head [Nop-C], advancing both
  static void CopyInst(vm_t & vm) {
    const auto [read_id, write_id] = vm.GetArgs<vm_t::Nop::B, vm_t::Nop::C>();
    vm.WriteGenome(vm.heads[write_id], vm.ReadGenome(vm.heads[read_id]));
    ++vm.heads[read_id];
    ++vm.heads[write_id];
  }

  // Inst: Read value at Head [Nop-D] ; Push onto Stack [Nop-A] ; advance Head.
  static void Load(vm_t & vm) {
    const auto [head_id, stack_id] = vm.GetArgs<vm_t::Nop::D, vm_t::Nop::A>();
    vm.stacks[stack_id].Push(vm.ReadMemory(vm.heads[head_id]));
    ++vm.heads[head_id];
  }

  // Inst: Pop[Nop-A] and write into Head [Nop-E] ; advance Head.
  static void Store(vm_t & vm) {
    const auto [stack_id, head_id] = vm.GetArgs<vm_t::Nop::A, vm_t::Nop::E>();
    vm.WriteMemory(vm.heads[head_id], vm.stacks[stack_id].Pop());
    ++vm.heads[head_id];
  }

  // Inst: Push the position of Head[Nop-F] onto Stack [Nop-A]
  static void HeadPos(vm_t & vm) {
    const auto [head_id, stack_id] = vm.GetArgs<vm_t::Nop::F, vm_t::Nop::A>();
    vm.stacks[stack_id].Push(vm.heads[head_id]);
  }

  // Inst: Pop Stack [Nop-A] and move Head[Nop-F] to that position.
  static void SetHead(vm_t & vm) {
    const auto [stack_id, head_id] = vm.GetArgs<vm_t::Nop::A, vm_t::Nop::F>();
    vm.heads[head_id] = static_cast<size_t>(vm.stacks[stack_id].Pop());
  }

  // Inst: Jump Head[Nop-A] to Head[Nop-F]
  static void JumpHead(vm_t & vm) {
    const auto [jump_id, target_id] = vm.GetArgs<vm_t::Nop::A, vm_t::Nop::F>();
    vm.heads[jump_id] = vm.heads[target_id];
  }

  // Inst: Shift Head[Nop-F] by the value Pop[Nop-A]
  static void OffsetHead(vm_t & vm) {
    const auto [head_id, stack_id] = vm.GetArgs<vm_t::Nop::F, vm_t::Nop::A>();
    vm.heads[head_id] += vm.stacks[stack_id].Pop();
  }
};

void AvidaVM::BuildInstSet(inst_set_t & inst_set) {
  using Inst = AvidaVM_Insts;
  inst_set.AddNopInst("Nop-A", Inst::Nop);
  inst_set.AddNopInst("Nop-B", Inst::Nop);
  inst_set.AddNopInst("Nop-C", Inst::Nop);
  inst_set.AddNopInst("Nop-D", Inst::Nop);
  inst_set.AddNopInst("Nop-E", Inst::Nop);
  inst_set.AddNopInst("Nop-F", Inst::Nop);

  inst_set.AddInst("Const",      Inst::Const);
  inst_set.AddInst("Offset",     Inst::Offset);
  inst_set.AddInst("Not",        Inst::Not);
  inst_set.AddInst("Shift",      Inst::Shift);
  inst_set.AddInst("Add",        Inst::Add);
  inst_set.AddInst("Sub",        Inst::Sub);
  inst_set.AddInst("Mult",       Inst::Mult);
  inst_set.AddInst("Div",        Inst::Div);
  inst_set.AddInst("Mod",        Inst::Mod);
  inst_set.AddInst("Exp",        Inst::Exp);
  inst_set.AddInst("Sort",       Inst::Sort);
  inst_set.AddInst("TestLess",   Inst::TestLess);
  inst_set.AddInst("TestEqu",    Inst::TestEqu);
  inst_set.AddInst("Nand",       Inst::Nand);
  inst_set.AddInst("Xor",        Inst::Xor);
  inst_set.AddInst("If",         Inst::If);
  inst_set.AddInst("IfNot",      Inst::IfNot);
  inst_set.AddInst("Scope",      Inst::Scope);
  inst_set.AddInst("Continue",   Inst::Continue);
  inst_set.AddInst("Break",      Inst::Break);
  inst_set.AddInst("StackPop",   Inst::StackPop);
  inst_set.AddInst("StackDup",   Inst::StackDup);
  inst_set.AddInst("StackSwap",  Inst::StackSwap);
  inst_set.AddInst("StackMove",  Inst::StackMove);
  inst_set.AddInst("CopyInst",   Inst::CopyInst);
  inst_set.AddInst("Load",       Inst::Load);
  inst_set.AddInst("Store",      Inst::Store);
  inst_set.AddInst("HeadPos",    Inst::HeadPos);
  inst_set.AddInst("SetHead",    Inst::SetHead);
  inst_set.AddInst("JumpHead",   Inst::JumpHead);
  inst_set.AddInst("OffsetHead", Inst::OffsetHead);
}
