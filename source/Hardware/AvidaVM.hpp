#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 * 
 *  Basic Avida CPUs good for genetic programming.
 * 
 *  DEVELOPER NOTES:
 *  - Experiment with memory size, stack depth, and number of stacks.
 *    Currently, stack A is the main one used for computation; drop to 3 or 4 stacks w/ modding?
 *  - Should inputs go to a stack or memory?  What should outputs come from?
 *  - Should inputs be refreshed with an instruction?
 *  - Const vals available are currently pretty arbitrary.
 *  - Merge heads, stacks, and memory?  Stacks can be located in memory.  Heads can be stack tops.
 */

#include <algorithm>
#include <cstddef>     // for size_t
#include <functional>  // for std::function
#include <limits>      // for std::numeric_limits
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
  using data_t = int32_t;                         // Data type used by this VM
  using udata_t = std::make_unsigned_t<data_t>;   // uint32_t
  using mem_t = emp::array<data_t, MEM_SIZE>;     // Memory is a fixed size
  using genome_t = Genome<uint8_t>;               // Genomes capped at 256 instructions
  using inst_set_t = InstSet<AvidaVM, MAX_INSTS>; // Instruction set type for AvidaVM
  using inst_id_t = typename genome_t::value_t;   // Type used for inst IDs in a genome
  using Stack = VMStack<data_t, STACK_DEPTH>;     // Stacks to use in virtual CPU
  using callback_t = void (*)(AvidaVM &);         // Special functions added to inst set

  static constexpr size_t ANALYSIS_BIOTA_ID = OrganismBase::ANALYSIS_BIOTA_ID;
  static constexpr size_t NO_BIOTA_ID = OrganismBase::NO_BIOTA_ID;

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

  // STACK info:
  // A: Math
  // B: Math extra
  // C: Input
  // D: Output
  // E: Storage
  // F: Storage

  static constexpr data_t const_vals[]{ 1, 2, 4, 16, 256, -1 };

  // Calculated values.
  static constexpr size_t DATA_BITS = sizeof(data_t)*8; // Number of bits in data_t;

private:
  genome_t genome;              // Genome of this organism, being executed.
  mem_t memory{};               // Storage of values being manipulated by this organism.
  emp::Ptr<const inst_set_t> inst_set_ptr = nullptr;  // Map of instruction names to functionality.

  // Alternate instruction set used for tracing/analyzing organisms in isolation.  It shares the
  // live set's instruction IDs (so genomes decode identically) but rebinds population-mutating
  // callbacks (e.g. DivideCell) to neutral variants, so analysis never alters the live population.
  emp::Ptr<const inst_set_t> analysis_inst_set_ptr = nullptr;

  emp::array<size_t, NUM_NOPS> heads{};
  emp::array<Stack, NUM_NOPS> stacks{};
  size_t exe_count = 0;              // How many instructions have been executed?
  size_t copy_count = 0;             // How many instructions copied into the genome this gestation?
  size_t error_count = 0;            // How many instructions tried something illegal?
  size_t biota_id = NO_BIOTA_ID;     // Live Biota slot, analysis marker, or no location.

  // Scratch buffer for analysis/trace annotations (e.g. "Task performed: NAND").
  // Stays empty during live evolution and flushed each step by Trace().
  emp::String analysis_notes{};

  struct AnalysisCopyTag { };

  AvidaVM(const AvidaVM & source, AnalysisCopyTag)
    : genome(source.genome)
    , memory(source.memory)
    , inst_set_ptr(source.analysis_inst_set_ptr)
    , analysis_inst_set_ptr(source.analysis_inst_set_ptr)
    , heads(source.heads)
    , stacks(source.stacks)
    , exe_count(source.exe_count)
    , copy_count(source.copy_count)
    , error_count(source.error_count)
    , biota_id(ANALYSIS_BIOTA_ID)
  {
    emp_always_assert(
      analysis_inst_set_ptr,
      "Cannot make an AvidaVM analysis copy without an analysis instruction set."
    );
  }

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
    ++copy_count;         // Count instructions actually copied this gestation (gates divide).
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

  // Push to a fixed stack.
  template <Nop STACK_ID>
  void StackPush(data_t value) {
    stacks[static_cast<size_t>(STACK_ID)].Push(value);
  }

  // Pop from a fixed stack.
  template <Nop STACK_ID>
  data_t StackPop() {
    return stacks[static_cast<size_t>(STACK_ID)].Pop();
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

  void Serialize(emp::SerialPod & pod) {
    pod(genome, memory, heads, stacks, exe_count, copy_count, error_count, biota_id);
  }

  // === Accessors ===

  [[nodiscard]] size_t GetExeCount() const { return exe_count; }
  [[nodiscard]] size_t GetCopyCount() const { return copy_count; }
  [[nodiscard]] size_t GetErrorCount() const { return error_count; }
  [[nodiscard]] const mem_t & GetMemory() const { return memory; }
  [[nodiscard]] const auto & GetHeads() const { return heads; }
  [[nodiscard]] const auto & GetStacks() const { return stacks; }
  
  [[nodiscard]] size_t GetBiotaID() const { return biota_id; }
  [[nodiscard]] bool IsAnalysis() const { return biota_id == ANALYSIS_BIOTA_ID; }
  [[nodiscard]] bool HasLiveBiotaID() const { return biota_id < ANALYSIS_BIOTA_ID; }
  AvidaVM & SetBiotaID(size_t in) {
    emp_always_assert(in < ANALYSIS_BIOTA_ID, "Cannot assign a reserved Biota ID to a live VM.");
    biota_id = in;
    return *this;
  }

  /// Copy this VM's complete state for isolated execution under its analysis instruction set.
  [[nodiscard]] AvidaVM MakeAnalysisCopy() const {
    return AvidaVM(*this, AnalysisCopyTag{});
  }

  [[nodiscard]] const inst_set_t & GetInstSet() const { emp_assert(inst_set_ptr); return *inst_set_ptr; }
  AvidaVM & SetInstSet(const inst_set_t & is) {
    emp_always_assert(!IsAnalysis(), "Cannot assign a live instruction set to an analysis VM.");
    inst_set_ptr = &is;
    return *this;
  }

  // Supply the alternate instruction set used by Trace()/analysis (see analysis_inst_set_ptr).
  AvidaVM & SetAnalysisInstSet(const inst_set_t & is) {
    analysis_inst_set_ptr = &is;
    if (IsAnalysis()) inst_set_ptr = &is;
    return *this;
  }



  // === Instructions ===

  // All instruction implementations live in AvidaVM_Insts (defined after this class).
  friend struct AvidaVM_Insts;

  void ProcessStep() {
    emp_assert(OK());
    const inst_id_t exec_id = ReadIP();
    AdvanceIP();
    GetInstSet().Execute(*this, exec_id);
    ++exe_count;
  }

  // Append an annotation to be emitted with the current Trace() step (For analysis-mode only!)
  void AddNote(auto &&... args) {
    analysis_notes.Append(args...);
    analysis_notes += '\n';
  }

  void Trace(size_t cpu_cycles=200, std::ostream & os=std::cout) {
    emp_always_assert(IsAnalysis(), "AvidaVM::Trace requires an analysis copy.");
    for (size_t i = 0; i <= cpu_cycles; ++i) {
      if (i) ProcessStep();
      std::println(os, "STEP {}: {}", i, StatusString());
      // Emit anything the step's signal handlers recorded (e.g. completed tasks), then reset.
      if (analysis_notes.size()) { std::print(os, "{}", analysis_notes); analysis_notes.clear(); }
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

    exe_count = 0;    // Reset execution count.
    copy_count = 0;   // Reset count of instructions copied this gestation.
    error_count = 0;  // Reset all errors.
  }

  // Partially reset hardware after birth.
  void ResetBirth() {
    exe_count = 0;    // Reset execution count.
    copy_count = 0;   // Reset count of instructions copied this gestation.
    error_count = 0;  // Reset all errors.
  }

  // Reset with a new genome.
  void Reset(const genome_t & in_genome) {
    genome = in_genome;
    Reset();
  }

  // Add a single input value.
  template <typename T>
  void AddInput(T input) {
    if constexpr (std::convertible_to<T, data_t>) {
      StackPush<Nop::C>(input);
    } else if constexpr (emp::IsContainer<T>) {
      for (const auto & x : input) AddInput(x);
    } else {
      static_assert(false, "Do not know how to add input.");
    }
  }

  // Setup all inputs at once.
  void SetInput(auto... inputs) {
    (AddInput(inputs) , ...);
  }

  // A faux instruction that manages output handling.
  data_t GetOutput() {
    return stacks[GetArg<Nop::D>()].Pop();
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

    // Require that the offspring was actually copied this gestation -- an organism cannot hand out
    // more instructions than it has copied.  This blocks "free" divides that just re-extract an
    // existing region of the genome without copying (which would make gestation cost ~1).
    const size_t offspring_size = head2 - head1;
    if (copy_count < offspring_size) {
      ++error_count;
      return genome_t{};  // Keep copy_count: the organism can copy more and retry.
    }

    // Extract the offspring from the genome.
    genome_t offspring = genome.Extract(head1, offspring_size);
    copy_count = 0;  // A new gestation begins for this cell.

    // Reset the heads.
    head2 = head1;  // Move head2 to the beginning of the extracted position (likely org end)
    head1 = 0;      // Move head1 to the beginning of the genome

    return offspring;
  }


  // === Static Functions for working with Avida VMs ===

  [[nodiscard]] static emp::String HardwareName() { return "AvidaVM"; }

  static void BuildInstSet(inst_set_t & inst_set);

  static bool AddCallback(inst_set_t & inst_set,
                          emp::String name,
                          callback_t callback_fun,
                          emp::String description = "") {
    if (description.empty()) {
      description = emp::MakeString("Invoke the registered ", name, " callback.");
    }
    inst_set.AddInst(name, callback_fun, description);
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
      " GenRead:", heads[HEAD_G_READ],
      " GenWrite:", heads[HEAD_G_WRITE],
      " MemRead:", heads[HEAD_M_READ],
      " MemWrite:", heads[HEAD_M_WRITE],
      " Flow:", heads[HEAD_FLOW]);
    out += "\nStacks: ";
    for (size_t i = 0; i < stacks.size(); ++i ) {
      if (i) out += "; ";
      out.Append(static_cast<char>('A' + i), ':', stacks[i].ToString());
    }
    out.Append("\nexe_count = ", exe_count);
    out.Append("\nerror_count = ", error_count);
    out.Append("\nNEXT >>>>>>>>>>>> ", NextInstName(), " [", NextInstSymbol(), "]");
    return out;
  }


  bool OK() const {
    // @CAO Add tests here...
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
    const auto X = static_cast<vm_t::udata_t>(vm.stacks[X_id].Pop());
    const auto Y = static_cast<vm_t::udata_t>(vm.stacks[Y_id].Pop());
    vm.stacks[output_id].Push(X << (Y % vm_t::DATA_BITS));
  }

  // Inst: X = Pop[Nop-A] ; Y = Pop[Arg1] ; Push[Arg1] : X + Y
  static void Add(vm_t & vm) {
    const auto [X_id, Y_id, output_id] = vm.GetArgs<vm_t::Nop::A, vm_t::Nop::FIRST_ARG, vm_t::Nop::FIRST_ARG>();
    const auto result = emp::WrapAdd(vm.stacks[X_id].Pop(), vm.stacks[Y_id].Pop());
    vm.stacks[output_id].Push(result);
  }

  // Inst: X = Pop[Nop-A] ; Y = Pop[Arg1] ; Push[Arg1] : X - Y
  static void Sub(vm_t & vm) {
    const auto [X_id, Y_id, output_id] = vm.GetArgs<vm_t::Nop::A, vm_t::Nop::FIRST_ARG, vm_t::Nop::FIRST_ARG>();
    const auto result = emp::WrapSub(vm.stacks[X_id].Pop(), vm.stacks[Y_id].Pop());
    vm.stacks[output_id].Push(result);
  }

  // Inst: X = Pop[Nop-A] ; Y = Pop[Arg1] ; Push[Arg1] : X * Y
  static void Mult(vm_t & vm) {
    const auto [X_id, Y_id, output_id] = vm.GetArgs<vm_t::Nop::A, vm_t::Nop::FIRST_ARG, vm_t::Nop::FIRST_ARG>();
    const auto result = emp::WrapMult(vm.stacks[X_id].Pop(), vm.stacks[Y_id].Pop());
    vm.stacks[output_id].Push(result);
  }

  // Inst: X = Pop[Nop-A] ; Y = Pop[Arg1] ; Push[Arg1] : X / Y  (error on Y==0)
  static void Div(vm_t & vm) {
    const auto [X_id, Y_id, output_id] = vm.GetArgs<vm_t::Nop::A, vm_t::Nop::FIRST_ARG, vm_t::Nop::FIRST_ARG>();
    const vm_t::data_t X = vm.stacks[X_id].Pop();
    const vm_t::data_t Y = vm.stacks[Y_id].Pop();
    if (Y == 0 || (X == std::numeric_limits<vm_t::data_t>::min() && Y == -1)) ++vm.error_count;
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
    const auto result = emp::Pow(vm.stacks[X_id].Pop(), vm.stacks[Y_id].Pop());
    vm.stacks[output_id].Push(result);
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
    vm.heads[head_id] = static_cast<vm_t::data_t>(vm.stacks[stack_id].Pop());
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
  inst_set.AddNopInst("Nop-A", Inst::Nop, "No operation; selects component A as an argument.");
  inst_set.AddNopInst("Nop-B", Inst::Nop, "No operation; selects component B as an argument.");
  inst_set.AddNopInst("Nop-C", Inst::Nop, "No operation; selects component C as an argument.");
  inst_set.AddNopInst("Nop-D", Inst::Nop, "No operation; selects component D as an argument.");
  inst_set.AddNopInst("Nop-E", Inst::Nop, "No operation; selects component E as an argument.");
  inst_set.AddNopInst("Nop-F", Inst::Nop, "No operation; selects component F as an argument.");

  inst_set.AddInst("Const", Inst::Const, "Push a selected constant onto a selected stack.");
  inst_set.AddInst("Offset", Inst::Offset, "Add a selected constant to a popped stack value.");
  inst_set.AddInst("Not", Inst::Not, "Apply logical NOT to a popped stack value.");
  inst_set.AddInst("Shift", Inst::Shift, "Left-shift one popped value by another.");
  inst_set.AddInst("Add", Inst::Add, "Add two popped stack values.");
  inst_set.AddInst("Sub", Inst::Sub, "Subtract one popped stack value from another.");
  inst_set.AddInst("Mult", Inst::Mult, "Multiply two popped stack values.");
  inst_set.AddInst("Div", Inst::Div, "Divide two popped stack values; division by zero is an error.");
  inst_set.AddInst("Mod", Inst::Mod, "Calculate the remainder of two popped values.");
  inst_set.AddInst("Exp", Inst::Exp, "Raise one popped stack value to the power of another.");
  inst_set.AddInst("Sort", Inst::Sort, "Sort the top values of two selected stacks.");
  inst_set.AddInst("TestLess", Inst::TestLess, "Test whether one popped value is less than another.");
  inst_set.AddInst("TestEqu", Inst::TestEqu, "Test whether two popped stack values are equal.");
  inst_set.AddInst("Nand", Inst::Nand, "Apply bitwise NAND to two popped stack values.");
  inst_set.AddInst("Xor", Inst::Xor, "Apply bitwise XOR to two popped stack values.");
  inst_set.AddInst("If", Inst::If, "Skip the next instruction when the selected value is zero.");
  inst_set.AddInst("IfNot", Inst::IfNot, "Skip the next instruction when the selected value is nonzero.");
  inst_set.AddInst("Scope", Inst::Scope, "Mark a scope boundary identified by following Nops.");
  inst_set.AddInst("Continue", Inst::Continue, "Jump backward to the selected scope boundary.");
  inst_set.AddInst("Break", Inst::Break, "Jump forward to the selected scope boundary.");
  inst_set.AddInst("StackPop", Inst::StackPop, "Discard the top value of a selected stack.");
  inst_set.AddInst("StackDup", Inst::StackDup, "Copy the top value of one stack onto another.");
  inst_set.AddInst("StackSwap", Inst::StackSwap, "Exchange the top values of two selected stacks.");
  inst_set.AddInst("StackMove", Inst::StackMove, "Move the top value from one stack to another.");
  inst_set.AddInst("CopyInst", Inst::CopyInst, "Copy an instruction between selected genome heads.");
  inst_set.AddInst("Load", Inst::Load, "Load a memory value onto a selected stack.");
  inst_set.AddInst("Store", Inst::Store, "Store a popped stack value in memory.");
  inst_set.AddInst("HeadPos", Inst::HeadPos, "Push a selected head's position onto a stack.");
  inst_set.AddInst("SetHead", Inst::SetHead, "Set a selected head from a popped stack value.");
  inst_set.AddInst("JumpHead", Inst::JumpHead, "Move one selected head to another head's position.");
  inst_set.AddInst("OffsetHead", Inst::OffsetHead, "Move a selected head by a popped offset.");
}
