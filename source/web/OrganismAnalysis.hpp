#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

// Definitions included after AvidaWebApp; the web application uses one translation unit.

emp::String AvidaWebApp::GenomeFileText(const avida_t::organism_t & organism) {
  return organism_adapter_t::GenomeText(organism);
}

std::optional<avida_t::genome_t>
AvidaWebApp::ParseGenomeText(const emp::String & genome_text) {
  std::istringstream genome_input{genome_text.str()};
  auto genome = Organisms().LoadGenome(genome_input);
  if (!genome) return std::nullopt;
  return std::move(*genome);
}

bool AvidaWebApp::IsReactionTask(size_t task_id) const {
  return std::find(
    organism_reaction_task_ids.cbegin(), organism_reaction_task_ids.cend(), task_id
  ) != organism_reaction_task_ids.cend();
}

void AvidaWebApp::InitializeOrganismAnalysis(
  const avida_t::genome_t & genome,
  const emp::String & name,
  const avida_t::organism_t * trait_source
) {
  organism_analysis_subject = std::make_unique<avida_t::organism_t>(genome);
  Avida().SetupAnalysisOrganism(*organism_analysis_subject);
  organism_analysis_name = name;
  organism_analysis_offspring.reset();
  organism_execution_step = 0;
  organism_execution_length = 0;
  organism_execution_complete = false;
  organism_last_instruction.clear();
  organism_last_description.clear();
  organism_step_notes.clear();
  organism_last_ip = 0;
  Avida().GetPlugIn<DriverBuffered>().ClearAnalysisOffspring();

  organism_task_counts.assign(Avida().GetNumTasks(), 0);
  organism_task_totals.assign(Avida().GetNumTasks(), 0);
  organism_reaction_task_ids.clear();
  organism_task_executions.clear();
  for (const auto & reaction : reaction_configs) {
    if (!Avida().HasTask(reaction.task_name)) continue;
    const size_t task_id = Avida().GetTaskID(reaction.task_name);
    if (!IsReactionTask(task_id)) organism_reaction_task_ids.push_back(task_id);
  }

  organism_analysis_traits.clear();
  const auto & displayed_organism = trait_source ? *trait_source : *organism_analysis_subject;
  for (const emp::String & trait_name : Avida().GetPrintableTraitNames()) {
    const auto & trait = Avida().GetTrait(trait_name);
    organism_analysis_traits.push_back({
      .name = trait_name,
      .value = trait.AsString(displayed_organism),
      .description = trait.GetDesc()
    });
  }

  auto preview_hardware = organism_analysis_subject->Hardware().MakeAnalysisCopy();
  auto & driver = Avida().GetPlugIn<DriverBuffered>();
  while (organism_execution_length < ORGANISM_ANALYSIS_STEP_LIMIT) {
    driver.ClearAnalysisOffspring();
    preview_hardware.ProcessStep();
    (void) preview_hardware.TakeAnalysisNotes();
    ++organism_execution_length;
    for (const size_t task_id : preview_hardware.TakeAnalysisTasks()) {
      if (task_id >= organism_task_totals.size() || !IsReactionTask(task_id)) continue;
      const bool is_first = organism_task_totals[task_id] == 0;
      ++organism_task_totals[task_id];
      organism_task_executions.push_back({
        .task_id = task_id,
        .step = organism_execution_length,
        .is_first = is_first
      });
    }
    auto offspring = driver.TakeAnalysisOffspring();
    if (!offspring) continue;
    organism_analysis_offspring = std::move(*offspring);
    organism_execution_complete = true;
    break;
  }
  SetOrganismExecutionPosition(0);
}

void AvidaWebApp::SetOrganismExecutionPosition(size_t target_step) {
  if (!organism_analysis_subject) return;
  target_step = std::min(target_step, organism_execution_length);
  organism_analysis_hardware.emplace(
    organism_analysis_subject->Hardware().MakeAnalysisCopy()
  );
  organism_last_instruction.clear();
  organism_last_description.clear();
  organism_step_notes.clear();
  organism_last_ip = 0;
  organism_task_counts.assign(Avida().GetNumTasks(), 0);

  auto & hardware = *organism_analysis_hardware;
  auto & driver = Avida().GetPlugIn<DriverBuffered>();
  for (size_t step = 0; step < target_step; ++step) {
    const auto & genome = hardware.GetGenome();
    organism_last_ip = hardware.GetHeads()[AvidaVM::HEAD_IP];
    const auto & inst_set = hardware.GetInstSet();
    const size_t inst_id = organism_last_ip < genome.size() ? genome[organism_last_ip] : 0;
    organism_last_instruction = inst_set.GetName(inst_id);
    organism_last_description = inst_set.GetDescription(inst_id);
    driver.ClearAnalysisOffspring();
    hardware.ProcessStep();
    organism_step_notes = hardware.TakeAnalysisNotes();
    for (const size_t task_id : hardware.TakeAnalysisTasks()) {
      if (task_id < organism_task_counts.size()) ++organism_task_counts[task_id];
    }
    (void) driver.TakeAnalysisOffspring();
  }
  organism_execution_step = target_step;
  UpdateOrganismTimelineControl(organism_execution_step, organism_execution_length);
}

void AvidaWebApp::FocusTrackedOrganismHead() const {
  if (!organism_analysis_hardware
      || tracked_organism_head == TrackedOrganismHead::NONE) return;
  static constexpr std::array<size_t, 4> head_ids{
    AvidaVM::HEAD_IP, AvidaVM::HEAD_G_READ, AvidaVM::HEAD_G_WRITE, AvidaVM::HEAD_FLOW
  };
  const size_t tracked_id = static_cast<size_t>(tracked_organism_head);
  if (tracked_id >= head_ids.size()) return;
  const auto & hardware = *organism_analysis_hardware;
  const size_t position = std::min(
    hardware.GetHeads()[head_ids[tracked_id]], hardware.GetGenome().size()
  );
  ScrollTrackedGenomeHead(static_cast<int>(tracked_id), position);
}

void AvidaWebApp::RedrawOrganismModeContent() {
  const bool preserve_scroll = tracked_organism_head == TrackedOrganismHead::NONE;
  const int scroll_top = preserve_scroll ? GetGenomeExecutionScrollTop() : 0;
  organism_mode_content.Redraw();
  if (preserve_scroll) RestoreGenomeExecutionScrollTop(scroll_top);
  else FocusTrackedOrganismHead();
}

void AvidaWebApp::SetTrackedOrganismHead(size_t head_id) {
  if (head_id > static_cast<size_t>(TrackedOrganismHead::FLOW)) return;
  const auto requested = static_cast<TrackedOrganismHead>(head_id);
  tracked_organism_head = tracked_organism_head == requested
    ? TrackedOrganismHead::NONE
    : requested;
  RedrawOrganismModeContent();
}

void AvidaWebApp::SetOrganismRunMode(OrganismRunMode mode) {
  if (!organism_analysis_hardware
      || organism_execution_step >= organism_execution_length) {
    mode = OrganismRunMode::PAUSED;
  }
  organism_run_mode = mode;
  organism_play_elapsed_ms = 0.0;
  UpdateOrganismModeControls();
  if (organism_run_mode == OrganismRunMode::PAUSED) {
    if (run_mode == RunMode::PAUSED && animation.GetActive()) animation.Stop();
  } else if (!animation.GetActive()) {
    animation.Start();
  }
}

void AvidaWebApp::ToggleOrganismRunMode(OrganismRunMode mode) {
  SetOrganismRunMode(
    organism_run_mode == mode ? OrganismRunMode::PAUSED : mode
  );
}

void AvidaWebApp::JumpToOrganismExecutionPosition(const std::string & value) {
  if (!organism_analysis_subject || value.empty()) return;
  SetOrganismRunMode(OrganismRunMode::PAUSED);
  SetOrganismExecutionPosition(
    emp::String{value}.As<size_t>(organism_execution_step)
  );
  RedrawOrganismModeContent();
  UpdateOrganismModeControls();
}

bool AvidaWebApp::PrepareSelectedOrganismAnalysis() {
  if (const auto * organism = GetActiveOrganism()) {
    InitializeOrganismAnalysis(
      organism->GetGenome(),
      emp::MakeString("Organism #", organism->GetGlobalID()),
      organism
    );
    return true;
  }

  if (!run_started && active_cell_id != avida_web::EMPTY_CELL) {
    const auto placement = FindPlacedOrganism(active_cell_id);
    if (placement != placed_organisms.end()) {
      const auto genome = ParseGenomeText(placement->genome);
      if (genome) {
        InitializeOrganismAnalysis(*genome, placement->name);
        return true;
      }
    }
  }

  organism_analysis_subject.reset();
  organism_analysis_hardware.reset();
  organism_analysis_offspring.reset();
  organism_analysis_traits.clear();
  organism_task_counts.clear();
  organism_task_totals.clear();
  organism_reaction_task_ids.clear();
  organism_task_executions.clear();
  organism_execution_step = 0;
  organism_execution_length = 0;
  organism_execution_complete = false;
  return false;
}

void AvidaWebApp::SelectFrozenOrganismForAnalysis(size_t freezer_id) {
  const auto iterator = std::find_if(
    freezer.organisms.begin(), freezer.organisms.end(),
    [freezer_id](const auto & item){ return item.id == freezer_id; }
  );
  if (iterator == freezer.organisms.end()) return;
  const auto genome = ParseGenomeText(iterator->genome);
  if (!genome) return;
  SetOrganismRunMode(OrganismRunMode::PAUSED);
  InitializeOrganismAnalysis(*genome, iterator->name);
  tracked_organism_head = TrackedOrganismHead::IP;
  RedrawOrganismModeContent();
  UpdateOrganismModeControls();
}

void AvidaWebApp::UpdateOrganismModeControls() {
  const bool has_subject = organism_analysis_hardware.has_value();
  const bool at_end = has_subject && organism_execution_step >= organism_execution_length;
  const bool show_offspring = at_end && organism_analysis_offspring.has_value();
  UpdateOrganismTransportControls(
    has_subject,
    organism_execution_step == 0,
    at_end,
    show_offspring,
    static_cast<int>(organism_run_mode)
  );
  UpdateOrganismTimelineControl(organism_execution_step, organism_execution_length);
  emp::String tick_spec;
  for (const auto & execution : organism_task_executions) {
    if (tick_spec.size()) tick_spec += ',';
    tick_spec.Append(
      execution.step, ':', execution.is_first ? 1 : 0, ':', execution.task_id
    );
  }
  if (BeginOrganismTimelineTaskTicks(tick_spec.c_str(), organism_execution_length)) {
    for (const auto & execution : organism_task_executions) {
      if (execution.task_id >= Avida().GetNumTasks()) continue;
      const auto & task_name = Avida().GetTaskName(execution.task_id);
      AddOrganismTimelineTaskTick(
        execution.step,
        organism_execution_length,
        execution.is_first,
        task_name.c_str()
      );
    }
  }
}

void AvidaWebApp::StepOrganismInstruction() {
  if (!organism_analysis_hardware || organism_execution_step >= organism_execution_length) return;
  SetOrganismRunMode(OrganismRunMode::PAUSED);
  SetOrganismExecutionPosition(organism_execution_step + 1);
  RedrawOrganismModeContent();
  UpdateOrganismModeControls();
}

void AvidaWebApp::AdvancePlayingOrganismInstruction() {
  if (!organism_analysis_hardware || organism_execution_step >= organism_execution_length) {
    SetOrganismRunMode(OrganismRunMode::PAUSED);
    return;
  }
  SetOrganismExecutionPosition(organism_execution_step + 1);
  RedrawOrganismModeContent();
  UpdateOrganismModeControls();
  if (organism_execution_step >= organism_execution_length) {
    SetOrganismRunMode(OrganismRunMode::PAUSED);
  }
}

void AvidaWebApp::ResetOrganismAnalysis() {
  if (!organism_analysis_subject) return;
  SetOrganismRunMode(OrganismRunMode::PAUSED);
  SetOrganismExecutionPosition(0);
  RedrawOrganismModeContent();
  UpdateOrganismModeControls();
}

void AvidaWebApp::ViewOrganismOffspring() {
  if (!organism_analysis_offspring
      || organism_execution_step != organism_execution_length) return;
  const auto genome = *organism_analysis_offspring;
  const emp::String name = emp::MakeString("Offspring of ", organism_analysis_name);
  SetOrganismRunMode(OrganismRunMode::PAUSED);
  InitializeOrganismAnalysis(genome, name);
  tracked_organism_head = TrackedOrganismHead::IP;
  organism_freezer_selector.SelectID(0);
  SetConfigurationControlValue("organism_freezer_selector", "0");
  RedrawOrganismModeContent();
  UpdateOrganismModeControls();
}

void AvidaWebApp::SetApplicationMode(ApplicationMode mode) {
  if (mode == active_application_mode) return;
  if (SimulationWorkerBusy()) {
    SetRunMode(RunMode::PAUSED);
    return;
  }
  if (active_application_mode == ApplicationMode::ORGANISM) {
    SetOrganismRunMode(OrganismRunMode::PAUSED);
  }
  if (mode == ApplicationMode::ORGANISM) {
    if (run_mode != RunMode::PAUSED) SetRunMode(RunMode::PAUSED);
    (void) PrepareSelectedOrganismAnalysis();
    tracked_organism_head = TrackedOrganismHead::IP;
  }
  active_application_mode = mode;
  RequestInterfaceRebuild();
}

emp::String AvidaWebApp::NotesAsHTML(emp::String notes) {
  notes = emp::MakeWebSafe(notes);
  notes.ReplaceAll("\n", "<br>");
  return notes;
}

emp::String AvidaWebApp::BuildOrganismModeHTML() const {
  if (!organism_analysis_hardware) {
    return
      "<div class='organism-mode-empty'><div class='organism-mode-empty-icon'>"
      "&#x2196;</div><h2>Select an organism first</h2>"
      "<p>Select an organism from the freezer (above) or return to Population mode "
      "and select an occupied cell.</p>"
      "</div>";
  }

  const auto & hardware = *organism_analysis_hardware;
  const auto & genome = hardware.GetGenome();
  const auto & inst_set = hardware.GetInstSet();
  const auto & heads = hardware.GetHeads();
  const size_t instruction_pointer = heads[AvidaVM::HEAD_IP];
  emp::String out;

  out.Append(
    "<div class='organism-cycle-status'><div><span class='eyebrow'>Single life cycle</span>",
    "<h1>", emp::MakeWebSafe(organism_analysis_name), "</h1></div>",
    "<div class='cycle-progress'><strong>", organism_execution_step, "</strong>",
    "<span>instructions executed</span></div></div>"
  );

  out += "<div class='organism-mode-grid'>";
  out += "<section class='organism-visual-panel genome-panel'><div class='panel-heading'>";
  out.Append(
    "<div><span class='eyebrow'>Execution map</span><h2>Genome</h2></div>",
    "<span class='panel-count'>", genome.size(), " instructions</span></div>"
  );
  out += "<div class='genome-execution-list'>";
  static constexpr std::array<size_t, 4> genome_head_ids{
    AvidaVM::HEAD_IP, AvidaVM::HEAD_G_READ, AvidaVM::HEAD_G_WRITE, AvidaVM::HEAD_FLOW
  };
  static constexpr std::array<const char *, 4> genome_head_labels{
    "IP", "READ", "WRITE", "FLOW"
  };
  for (size_t pos = 0; pos <= genome.size(); ++pos) {
    emp::String markers;
    for (size_t marker_id = 0; marker_id < genome_head_ids.size(); ++marker_id) {
      if (heads[genome_head_ids[marker_id]] != pos) continue;
      const bool is_tracked = tracked_organism_head
        == static_cast<TrackedOrganismHead>(marker_id);
      markers.Append(
        "<button type='button' class='head-marker head-", marker_id,
        is_tracked ? " is-tracked" : "", "' data-organism-head='", marker_id,
        "' aria-pressed='", is_tracked ? "true" : "false",
        "' title='", is_tracked ? "Stop following " : "Follow ",
        genome_head_labels[marker_id], " head'>", genome_head_labels[marker_id],
        " <span aria-hidden='true'>&rarr;</span></button>"
      );
    }
    if (pos == genome.size() && markers.empty()) break;
    const bool is_next = pos == instruction_pointer && pos < genome.size();
    const bool was_last = hardware.GetExeCount() && pos == organism_last_ip;
    out.Append(
      "<div class='genome-execution-row", is_next ? " is-next" : "",
      was_last ? " was-last" : "", pos == genome.size() ? " is-boundary" : "",
      "' data-genome-position='", pos, "'>",
      "<div class='genome-head-lane'>", markers, "</div>",
      "<span class='genome-position'>", pos, "</span>"
    );
    if (pos < genome.size()) {
      out.Append(
        "<code title='", emp::MakeWebSafe(inst_set.GetDescription(genome[pos])), "'>",
        emp::MakeWebSafe(inst_set.GetName(genome[pos])), "</code>"
      );
    } else {
      out += "<span class='genome-end'>end of genome</span>";
    }
    if (is_next) out += "<span class='next-badge'>NEXT</span>";
    out += "</div>";
  }
  out += "</div></section>";

  out += "<div class='organism-state-column'>";
  out += "<section class='organism-visual-panel instruction-panel'>";
  if (organism_analysis_offspring
      && organism_execution_step == organism_execution_length) {
    out.Append(
      "<div class='instruction-status success'><span>Life cycle complete</span>",
      "<strong>Offspring produced &middot; ",
      organism_analysis_offspring->size(), " instructions</strong>. Use “View offspring” ",
      "above to start it with fresh hardware.</div>"
    );
  } else if (!organism_execution_complete
             && organism_execution_step == organism_execution_length) {
    out.Append(
      "<div class='instruction-status'><span>Preview limit reached</span>",
      "<strong>Execution continues</strong> &middot; No offspring was produced in the first ",
      organism_execution_length, " instructions.</div>"
    );
  }
  out += "<div class='instruction-context-grid'><div class='instruction-context previous'>";
  if (organism_last_instruction.size()) {
    out.Append(
      "<span class='eyebrow'>Just executed at ", organism_last_ip, "</span><h2><code>",
      emp::MakeWebSafe(organism_last_instruction), "</code></h2><p>",
      emp::MakeWebSafe(organism_last_description), "</p>"
    );
    if (organism_step_notes.size()) {
      out.Append("<div class='instruction-note'>", NotesAsHTML(organism_step_notes), "</div>");
    }
  } else {
    out += "<span class='eyebrow'>Just executed</span><h2>Nothing yet</h2>";
    out += "<p>Use Step, Play, or Fast-forward to begin this life cycle.</p>";
  }
  const size_t next_id = instruction_pointer < genome.size() ? genome[instruction_pointer] : 0;
  out += "</div><div class='instruction-context next'>";
  out.Append(
    "<span class='eyebrow'>About to execute at ", instruction_pointer, "</span><h2><code>",
    emp::MakeWebSafe(inst_set.GetName(next_id)), "</code></h2><p>",
    emp::MakeWebSafe(inst_set.GetDescription(next_id)), "</p></div></div></section>"
  );

  out += "<section class='organism-visual-panel'><div class='panel-heading'>";
  out += "<div><span class='eyebrow'>Registers</span><h2>Stacks</h2></div></div>";
  out += "<div class='stack-grid'>";
  const auto & stacks = hardware.GetStacks();
  for (size_t i = 0; i < stacks.size(); ++i) {
    emp::String values = stacks[i].ToString();
    if (values.empty()) values = "0";
    out.Append(
      "<div class='stack-card'><span>Stack ", static_cast<char>('A' + i), "</span><code>",
      emp::MakeWebSafe(values), "</code></div>"
    );
  }
  out += "</div></section>";

  out += "<section class='organism-visual-panel'><div class='panel-heading'>";
  out += "<div><span class='eyebrow'>Working state</span><h2>Memory</h2></div>";
  out.Append(
    "<div class='hardware-mini-counters'><span>Copied <strong>", hardware.GetCopyCount(),
    "</strong></span><span>Errors <strong>", hardware.GetErrorCount(), "</strong></span></div></div>"
  );
  out += "<div class='memory-grid'>";
  const auto & memory = hardware.GetMemory();
  for (size_t pos = 0; pos < memory.size(); ++pos) {
    emp::String markers;
    if (heads[AvidaVM::HEAD_M_READ] == pos) {
      markers += "<span class='memory-head read'>READ &darr;</span>";
    }
    if (heads[AvidaVM::HEAD_M_WRITE] == pos) {
      markers += "<span class='memory-head write'>WRITE &darr;</span>";
    }
    out.Append(
      "<div class='memory-cell", memory[pos] ? " has-value" : "", "'>",
      "<div class='memory-head-lane'>", markers, "</div>",
      "<span>", pos, "</span><code>", memory[pos], "</code></div>"
    );
  }
  out += "</div></section>";

  out += "<section class='organism-visual-panel tasks-panel'><div class='panel-heading'>";
  out += "<div><span class='eyebrow'>Life-cycle progress</span><h2>Tasks</h2></div></div>";
  out += "<dl class='analysis-task-list'>";
  size_t displayed_task_count = 0;
  for (const size_t task_id : organism_reaction_task_ids) {
    if (task_id >= organism_task_totals.size()) continue;
    const size_t total = organism_task_totals[task_id];
    if (!total) continue;
    const size_t current = task_id < organism_task_counts.size()
      ? organism_task_counts[task_id]
      : 0;
    out.Append(
      "<div class='analysis-task-row'><dt>",
      emp::MakeWebSafe(Avida().GetTaskName(task_id)), "</dt><dd><strong>", current,
      "</strong><span>/", total, "</span></dd></div>"
    );
    ++displayed_task_count;
  }
  if (!displayed_task_count) {
    out += "<p class='analysis-task-empty'>No configured reaction tasks are performed "
           "in this life cycle.</p>";
  }
  out += "</dl></section>";

  out += "<section class='organism-visual-panel traits-panel'><div class='panel-heading'>";
  out += "<div><span class='eyebrow'>Starting phenotype</span><h2>Organism traits</h2></div></div>";
  out += "<dl class='analysis-trait-list'>";
  for (const auto & trait : organism_analysis_traits) {
    out.Append(
      "<div title='", emp::MakeWebSafe(trait.description), "'><dt>",
      emp::MakeWebSafe(trait.name), "</dt><dd>", emp::MakeWebSafe(trait.value), "</dd></div>"
    );
  }
  if (organism_analysis_traits.empty()) out += "<p>No printable traits.</p>";
  out += "</dl></section></div></div>";
  return out;
}

emp::String AvidaWebApp::BuildOrganismStatsHTML() {
  const auto * organism = GetActiveOrganism();
  if (!organism) {
    return "<p class='org-stats-empty'>Select an occupied population cell to inspect its organism.</p>";
  }

  emp::String out;
  out.Append(
    "<div class='org-stats-summary'><span>Active organism <strong>#",
    organism->GetGlobalID(), "</strong></span><span>",
    Population().DescribeCell(active_cell_id), "</span></div>"
  );

  out += "<section class='org-stats-section'><h3>Phenotype</h3><dl class='trait-list'>";
  const auto trait_names = Avida().GetPrintableTraitNames();
  for (const emp::String & name : trait_names) {
    const auto & trait = Avida().GetTrait(name);
    out.Append(
      "<div class='trait-row' title='", emp::MakeWebSafe(trait.GetDesc()), "'><dt>",
      emp::MakeWebSafe(name), "</dt><dd>", emp::MakeWebSafe(trait.AsString(*organism)),
      "</dd></div>"
    );
  }
  if (trait_names.empty()) out += "<div class='org-stats-empty'>No printable traits.</div>";
  out += "</dl></section>";

  const auto & genome = organism->GetGenome();
  const auto & inst_set = organism->Hardware().GetInstSet();
  out.Append(
    "<section class='org-stats-section'><h3>Genome <span>", genome.size(),
    " instructions</span></h3><ol class='genome-list'>"
  );
  for (size_t pos = 0; pos < genome.size(); ++pos) {
    const size_t inst_id = genome[pos];
    const emp::String & description = inst_set.GetDescription(inst_id);
    out.Append(
      "<li title='", emp::MakeWebSafe(description), "'><code>",
      emp::MakeWebSafe(inst_set.GetName(inst_id)), "</code></li>"
    );
  }
  out += "</ol></section>";

  const AvidaVM & hardware = organism->Hardware();
  static constexpr std::array<const char *, AvidaVM::NUM_NOPS> head_names{
    "Instruction", "Genome read", "Genome write", "Memory read", "Memory write", "Flow"
  };
  out += "<section class='org-stats-section'><h3>Hardware</h3>";
  out.Append(
    "<div class='hardware-counters'><span>Executed <strong>", hardware.GetExeCount(),
    "</strong></span><span>Copied <strong>", hardware.GetCopyCount(),
    "</strong></span><span>Errors <strong>", hardware.GetErrorCount(), "</strong></span></div>"
  );
  out += "<h4>Heads</h4><dl class='hardware-list'>";
  const auto & heads = hardware.GetHeads();
  for (size_t i = 0; i < heads.size(); ++i) {
    out.Append("<div><dt>", head_names[i], "</dt><dd>", heads[i], "</dd></div>");
  }
  out += "</dl><h4>Stacks</h4><dl class='hardware-list'>";
  const auto & stacks = hardware.GetStacks();
  for (size_t i = 0; i < stacks.size(); ++i) {
    emp::String values = stacks[i].ToString();
    if (values.empty()) values = "0";
    out.Append(
      "<div><dt>", static_cast<char>('A' + i), "</dt><dd>",
      emp::MakeWebSafe(values), "</dd></div>"
    );
  }
  out += "</dl><h4>Nonzero memory</h4><div class='hardware-memory'>";
  bool found_memory = false;
  const auto & memory = hardware.GetMemory();
  for (size_t i = 0; i < memory.size(); ++i) {
    if (memory[i] == 0) continue;
    found_memory = true;
    out.Append("<span><code>", i, "</code>: ", memory[i], "</span>");
  }
  if (!found_memory) out += "<span>All locations are zero.</span>";
  out += "</div></section>";
  return out;
}
