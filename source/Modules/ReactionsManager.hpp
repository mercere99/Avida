#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  Reward organisms for completing tasks (in any environment) by modifying their traits.
 *
 *  Each configured reaction banks a bonus (additive or multiplicative) onto the organism
 *  that performs the task.  Bonuses are NOT applied to the performing organism directly;
 *  instead they are transferred to its offspring at birth.  This reproduces the classic
 *  Avida "merit" model: an organism is born with the merit its parent earned over the
 *  parent's lifetime, so a reward offsets to the next generation rather than retroactively
 *  changing a scheduling decision that drivers lock in at placement.
 */

#include <cstddef>    // for size_t
#include <iostream>   // for std::cout

#include "emp/base/notify.hpp"
#include "emp/base/Ptr.hpp"
#include "emp/base/vector.hpp"
#include "emp/meta/TypeID.hpp"
#include "emp/tools/String.hpp"

#include "../core/Avida.hpp"

template <typename AVIDA_T>
class ReactionsManager : public ModuleBase<AVIDA_T> {
private:
  using ModuleBase<AVIDA_T>::avida;

  enum class Op { ADD, MULT };

  // A distinct trait rewarded by one or more reactions.  Per-organism banked bonuses are
  // indexed by position in `targets` (see Phenotype::earned_add / earned_mult).
  // NOTE: a Trait *pointer* is cached (not an organism_t-typed accessor) so this module's
  // class body stays complete-able before Avida finishes defining organism_t.
  struct Target {
    emp::String name;                                  // Configured trait name (a double trait).
    emp::Ptr<const Trait<double, AVIDA_T>> trait;      // Resolved during BeforeStart.
  };

  // A single configured reaction: reward a target trait when a given task completes.
  struct Reaction {
    emp::String task_name;    // Task that triggers this reaction.
    emp::String trait_name;   // Target trait to reward.
    Op op = Op::ADD;          // Additive or multiplicative bonus.
    double value = 0.0;       // Magnitude of the bonus.
    size_t max_triggers = 0;  // Per-lifetime cap on rewards (0 == unlimited).

    size_t task_id = 0;       // Resolved task ID (BeforeStart).
    size_t target_id = 0;     // Index into `targets` (BeforeStart).
  };

  emp::vector<Reaction> reactions;                  // All reactions, in declaration order.
  emp::vector<Target> targets;                      // Distinct target traits across reactions.
  emp::vector<emp::vector<size_t>> task_reactions;  // task ID -> indices into `reactions`.
  emp::vector<size_t> update_counts;                // Times each reaction triggered this update.
  size_t output_freq = 0;                           // Updates between reports (0 == off).

  static Op ToOp(emp::String name) {
    name.SetLower();
    if (name == "add"  || name == "+") return Op::ADD;
    if (name == "mult" || name == "*") return Op::MULT;
    emp::notify::Error("Unknown reaction operation '", name, "'; expected 'add' or 'mult'.");
    return Op::ADD;
  }

  // Find the index of `trait_name` in `targets`, appending a new entry if not yet present.
  size_t GetTargetID(const emp::String & trait_name) {
    for (size_t i = 0; i < targets.size(); ++i) {
      if (targets[i].name == trait_name) return i;
    }
    targets.push_back(Target{trait_name, nullptr});
    return targets.size() - 1;
  }

public:
  ReactionsManager(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>(avida, "ReactionsManager", "Reactions",
        "Reward tasks by modifying organism traits (classic-Avida merit model).") {}
  ~ReactionsManager() {}

  void Serialize(emp::SerialPod & pod) {
    // `reactions`/`targets`/`task_reactions` are rebuilt from config keywords plus BeforeStart;
    // only the transient per-update tally is genuine runtime state to save.
    pod(update_counts);
  }

  // === Phenotypic Traits ===

  // Per-organism bonus earned this lifetime, banked for the organism's OFFSPRING.
  // earned_add / earned_mult are indexed by target-trait ID (see `targets`).
  struct Phenotype {
    emp::vector<double> earned_add;      // Additive bonus per target trait (identity 0).
    emp::vector<double> earned_mult;     // Multiplicative bonus per target trait (identity 1).
    emp::vector<size_t> reaction_counts; // Times each reaction has triggered this lifetime.
  };

  void RegisterTraits() {
    AVIDA_REGISTER_TRAIT(earned_add,  "Additive task bonus banked for offspring (per target trait).");
    AVIDA_REGISTER_TRAIT(earned_mult, "Multiplicative task bonus banked for offspring (per target trait).");
    AVIDA_REGISTER_TRAIT(reaction_counts, "Times each reaction has triggered this lifetime.");
  }

  void RegisterSettings() {
    avida.AddKeyword("Reaction",
      [this](emp::vector<emp::String> args){ AddReaction(args); },
      "Define a reaction: Reaction <task> <trait> <add|mult> <value> [max_triggers]",
      '\0', /*max_args=*/5);
    avida.AddSetting("reactions.output_freq", output_freq,
      "Updates between reaction-count reports (0 = off).");
  }

  // Parse one `Reaction` config line into an (as-yet unresolved) reaction record.
  // Takes args by value (as the keyword handler supplies them) since ConvertTo is non-const.
  void AddReaction(emp::vector<emp::String> args) {
    if (args.size() < 4 || args.size() > 5) {
      emp::notify::Error("Reaction needs 4-5 args: <task> <trait> <add|mult> <value> [max_triggers];"
                         " received ", args.size(), ".");
      return;
    }
    Reaction r;
    r.task_name    = args[0];
    r.trait_name   = args[1];
    r.op           = ToOp(args[2]);
    r.value        = args[3].ConvertTo<double>();
    r.max_triggers = (args.size() == 5) ? args[4].ConvertTo<size_t>() : 0;
    reactions.push_back(std::move(r));
  }

  // === Signal Listeners ===

  // Validate the configuration and resolve task IDs + trait accessors, now that every
  // module has registered its tasks and traits.
  void BeforeStart() {
    task_reactions.resize(avida.GetNumTasks());
    update_counts.resize(reactions.size(), 0);

    for (size_t i = 0; i < reactions.size(); ++i) {
      Reaction & r = reactions[i];

      if (!avida.HasTask(r.task_name)) {
        emp::notify::Error("Reaction references unknown task '", r.task_name,
          "'; is an environment module that defines it included in the module list?");
        continue;
      }
      r.task_id = avida.GetTaskID(r.task_name);

      if (!avida.HasTrait(r.trait_name)) {
        emp::notify::Error("Reaction targets unknown trait '", r.trait_name, "'.");
        continue;
      }
      if (avida.GetTrait(r.trait_name).GetTypeID() != emp::GetTypeID<double>()) {
        emp::notify::Error("Reaction trait '", r.trait_name, "' must be type double, but is ",
          avida.GetTrait(r.trait_name).GetTypeID(), ".");
        continue;
      }

      r.target_id = GetTargetID(r.trait_name);
      task_reactions[r.task_id].push_back(i);
    }

    // Cache a pointer to each distinct target trait for fast access at reproduction.
    for (Target & t : targets) {
      t.trait = &avida.template GetTypedTrait<double>(t.name);
    }
  }

  void OnUpdateStart([[maybe_unused]] size_t update) {
    for (size_t & c : update_counts) c = 0;
  }

  // Bank each triggered reaction's bonus on the organism; it reaches its offspring at birth.
  template <concepts::Organism ORG_T>
  void OnTaskComplete(ORG_T & org, size_t task_id) {
    emp_assert(task_id < task_reactions.size(), task_id, task_reactions.size());
    auto & counts = org.GetPhenotype().reaction_counts;
    auto & add    = org.GetPhenotype().earned_add;
    auto & mult   = org.GetPhenotype().earned_mult;

    for (size_t r_idx : task_reactions[task_id]) {
      const Reaction & r = reactions[r_idx];
      if (r.max_triggers && counts[r_idx] >= r.max_triggers) continue; // Hit the lifetime cap.
      switch (r.op) {
      case Op::ADD:  add[r.target_id]  += r.value; break;
      case Op::MULT: mult[r.target_id] *= r.value; break;
      }
      ++counts[r_idx];
      ++update_counts[r_idx];
    }
  }

  // Offspring are born with the merit their PARENT earned (one-generation offset).
  // A fresh offspring phenotype holds each target trait at its default, so applying the
  // additive then multiplicative bonus yields (default + earned_add) * earned_mult.
  template <concepts::Organism ORG_T>
  void OnOffspringInit(ORG_T & offspring, ORG_T & parent) {
    const auto & p_add  = parent.GetPhenotype().earned_add;
    const auto & p_mult = parent.GetPhenotype().earned_mult;
    for (size_t t = 0; t < targets.size(); ++t) {
      double & trait = targets[t].trait->GetAccessFun()(offspring);
      trait += p_add[t];
      trait *= p_mult[t];
    }
  }

  // Give every organism a clean slate of banked bonuses and reaction counts at activation.
  template <concepts::Organism ORG_T>
  void BeforePlacement(ORG_T & org) {
    org.GetPhenotype().earned_add.assign(targets.size(), 0.0);
    org.GetPhenotype().earned_mult.assign(targets.size(), 1.0);
    org.GetPhenotype().reaction_counts.assign(reactions.size(), 0);
  }

  void OnUpdateEnd(size_t update) {
    if (output_freq == 0 || update % output_freq != 0) return;
    std::cout << "UD:" << update << "  Reactions:";
    for (size_t i = 0; i < reactions.size(); ++i) {
      std::cout << ' ' << reactions[i].task_name << '=' << update_counts[i];
    }
    std::cout << std::endl;
  }
};
