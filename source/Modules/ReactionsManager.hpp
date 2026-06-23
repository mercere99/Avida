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
 *
 *  This module also tracks, per organism, how many times it (and its parent) performed each
 *  task.  The data file reports, per task, how many current organisms had a parent perform it
 *  at least once -- a proxy for how many organisms are likely capable of that task.
 */

#include <cstddef>    // for size_t

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

  // A single configured reaction: reward a target trait when a given task completes.
  struct Reaction {
    emp::String task_name;    // Task that triggers this reaction.
    emp::String trait_name;   // Target trait to modify.
    Op op = Op::ADD;          // Additive or multiplicative bonus.
    double value = 0.0;       // Magnitude of the change.
    size_t max_triggers = 0;  // Per-lifetime cap on reaction count (0 == unlimited).

    // Internally calculated:
    size_t task_id = 0;                                // Resolved task ID
    // Target trait, resolved in BeforeStart.  A Trait *pointer* (template-id) is cached rather
    // than the accessor by value, so this struct names no organism_t-dependent type at class
    // scope -- which would fail while Avida's phenotype (and thus organism_t) is still forming.
    emp::Ptr<const Trait<double, AVIDA_T>> trait_ptr;
  };

  emp::vector<Reaction> reactions;                  // All reactions, in declaration order.
  emp::vector<emp::vector<size_t>> task_reactions;  // task ID -> indices into `reactions`.

  static Op ToOp(emp::String name) {
    name.SetLower();
    if (name == "add"  || name == "+") return Op::ADD;
    if (name == "mult" || name == "*") return Op::MULT;
    emp::notify::Error("Unknown reaction operation '", name, "'; expected 'add' or 'mult'.");
    return Op::ADD;
  }

  // Count active organisms whose parent performed `task_id` at least once (a capability proxy).
  size_t CountParentPerformers(size_t task_id) {
    size_t count = 0;
    avida.GetBiota().ForEachOrg([&](auto & org) {
      const auto & pc = org.GetPhenotype().parent_task_counts;
      if (task_id < pc.size() && pc[task_id]) ++count;
    });
    return count;
  }

public:
  ReactionsManager(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>(avida, "ReactionsManager", "Reactions",
        "Reward tasks by modifying organism traits (classic-Avida merit model).") {}
  ~ReactionsManager() {}

  void Serialize(emp::SerialPod & /* pod */) {
    // `reactions`/`targets`/`task_reactions` are rebuilt from config keywords plus BeforeStart.
    // Per-organism task counts are registered traits (serialized with each organism), and the
    // output tally is recomputed before each write -- so there is nothing extra to save here.
  }

  // === Phenotypic Traits ===

  // Per-organism state.  earned_add / earned_mult bank the bonus this organism earns for its
  // OFFSPRING (indexed by target-trait ID).  task_counts records how many times THIS organism
  // performed each task this lifetime; parent_task_counts is its parent's final tally, copied
  // at birth (both indexed by task ID).
  struct Phenotype {
    emp::vector<size_t> task_counts;        // Times THIS organism performed each task.
    emp::vector<size_t> parent_task_counts; // Times this organism's PARENT performed each task.
  };

  void RegisterTraits() {
    AVIDA_REGISTER_TRAIT(task_counts, "Times organism performed each task.");
    AVIDA_REGISTER_TRAIT(parent_task_counts, "Times organism's parent performed each task.");
  }

  void RegisterSettings() {
    avida.AddKeyword("Reaction",
      [this](emp::vector<emp::String> args){ AddReaction(args); },
      "Define a reaction: Reaction <task> <trait> <add|mult> <value> [max_triggers]");
  }

  // Parse one `Reaction` config line into an (as-yet unresolved) reaction record.
  void AddReaction(const emp::vector<emp::String> & args) {
    if (args.size() < 4 || args.size() > 5) {
      emp::notify::Error("Reaction needs 4-5 args: <task> <trait> <add|mult> <value> [max_triggers];"
                         " received ", args.size(), ".");
      return;
    }
    Reaction react;
    react.task_name    = args[0];
    react.trait_name   = args[1];
    react.op           = ToOp(args[2]);
    react.value        = args[3].ConvertTo<double>();
    react.max_triggers = (args.size() >= 5) ? args[4].ConvertTo<size_t>() : 0;
    reactions.push_back(std::move(react));
  }

  // === Signal Listeners ===

  // Validate the configuration and resolve task IDs + trait accessors, now that every
  // module has registered its tasks and traits.
  void BeforeStart() {
    task_reactions.resize(avida.GetNumTasks());

    // Process each reaction...
    for (size_t react_id = 0; react_id < reactions.size(); ++react_id) {
      Reaction & react = reactions[react_id];

      // Make sure that we can hook in the task for this reaction.
      if (!avida.HasTask(react.task_name)) {
        emp::notify::Error("Reaction references unknown task '", react.task_name,
          "'; is an environment module that defines it included in the module list?");
      }
      react.task_id = avida.GetTaskID(react.task_name);

      // Make sure that we have a proper double trait to modify from this reaction.
      if (!avida.HasTrait(react.trait_name)) {
        emp::notify::Error("Reaction targets unknown trait '", react.trait_name, "'.");
      }
      if (avida.GetTrait(react.trait_name).GetTypeID() != emp::GetTypeID<double>()) {
        emp::notify::Error("Reaction trait '", react.trait_name, "' must be type double, but is ",
          avida.GetTrait(react.trait_name).GetTypeID(), ".");
      }

      react.trait_ptr = &avida.template GetTypedTrait<double>(react.trait_name);

      task_reactions[react.task_id].push_back(react_id);

      avida.AddOutput("reactions.csv", react.task_name,
        [this, task_id=react.task_id](){ return CountParentPerformers(task_id); });
    }
  }

  // Count this task for the organism and bank any rewards (which reach its offspring at birth).
  template <concepts::Organism ORG_T>
  void OnTaskComplete(ORG_T & org, size_t task_id) {
    emp_assert(task_id < task_reactions.size(), task_id, task_reactions.size());

    auto & task_counts = org.GetPhenotype().task_counts;
    const size_t n = ++task_counts[task_id];  // Nth time THIS organism performed this task.

    // Trigger all reactions associated with this task.
    for (size_t r_idx : task_reactions[task_id]) {
      const Reaction & react = reactions[r_idx];
      if (react.max_triggers && n > react.max_triggers) continue;  // Past reaction's lifetime cap.
      switch (react.op) {
      case Op::ADD:  react.trait_ptr->GetAccessFun()(org) += react.value; break;
      case Op::MULT: react.trait_ptr->GetAccessFun()(org) *= react.value; break;
      }
    }
  }

  template <concepts::Organism ORG_T>
  void OnOffspringReady(ORG_T & offspring, ORG_T & parent) {
    offspring.GetPhenotype().parent_task_counts = parent.GetPhenotype().task_counts;
  }

  // Injected organisms have no parent, so they start with an empty parental task history.
  template <concepts::Organism ORG_T>
  void OnInjectReady(ORG_T & org) {
    org.GetPhenotype().parent_task_counts.assign(avida.GetNumTasks(), 0);
  }

  // Give every organism a clean slate of banked bonuses and its own task counts at activation.
  // parent_task_counts is set at birth (OnOffspringInit) or injection (OnInjectReady), so it
  // is intentionally left intact here.
  template <concepts::Organism ORG_T>
  void BeforePlacement(ORG_T & org) {
    org.GetPhenotype().task_counts.assign(avida.GetNumTasks(), 0);
  }
};
