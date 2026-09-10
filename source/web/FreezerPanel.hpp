#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

// Definitions included after AvidaWebApp; the web application uses one translation unit.

UI::Div AvidaWebApp::BuildFreezerNameEditor(
  size_t item_type,
  size_t item_id,
  const emp::String & name,
  const emp::String & id_prefix
) {
  UI::Div editor{emp::MakeString(id_prefix, item_id)};
  editor.AddAttr("class", "freezer-item-name");
  editor.SetAttr("role", "textbox");
  editor.SetAttr("aria-label", emp::MakeString("Name for ", name, "; double-click to edit"));
  editor.SetAttr("aria-readonly", "true");
  editor.SetAttr("tabindex", "0");
  editor.SetAttr("contenteditable", "false");
  editor.SetTitle("Double-click to rename");
  editor.SetAttr(
    "ondblclick",
    "event.stopPropagation();this.contentEditable='true';"
    "this.setAttribute('aria-readonly','false');this.focus();"
    "const range=document.createRange();range.selectNodeContents(this);"
    "const selection=window.getSelection();selection.removeAllRanges();selection.addRange(range);"
  );
  editor.SetAttr(
    "onkeydown",
    "if(event.key==='Enter'){event.preventDefault();this.blur();}"
    "else if(event.key==='Escape'){event.preventDefault();this.blur();}"
  );
  editor.SetAttr(
    "onblur",
    emp::MakeString(
      "if(this.isContentEditable){this.contentEditable='false';",
      "this.setAttribute('aria-readonly','true');emp.Callback(",
      rename_freezer_callback_id, ",", item_type, ",", item_id, ",this.textContent);}"
    )
  );
  editor << emp::MakeWebSafe(name);
  return editor;
}

void AvidaWebApp::BuildFreezerPanel() {
  freezer_inspector = UI::Div{"freezer_inspector"};
  freezer_inspector.AddAttr("class", "freezer-inspector");
  freezer_inspector.SetCSS("display", "none");
  freezer_inspector << "<h2>Freezer</h2>";
  freezer_inspector <<
    "<p class='freezer-intro'>Double-click a name to edit it. Drag organisms between this freezer and the grid.</p>";
  if (freezer_message.size()) {
    const bool is_error = freezer_message.starts_with("Checkpoint rejected:")
      || freezer_message.starts_with("Checkpoint not saved:");
    freezer_inspector << emp::MakeString(
      "<p class='freezer-message", is_error ? " freezer-message-error" : "",
      "' role='", is_error ? "alert" : "status", "'>",
      emp::MakeWebSafe(freezer_message), "</p>"
    );
  }

  UI::Div organism_section{"freezer_organism_section"};
  organism_section.AddAttr("class", "freezer-section");
  UI::Div organism_header{"freezer_organism_header"};
  organism_header.AddAttr("class", "freezer-section-header");
  organism_header << "<div><h3>Organisms</h3><p>Genomes that can seed a new dish.</p></div>";
  save_organism_button = UI::Button{
    [this](){ SaveActiveOrganism(); }, "Save Organism", "save_organism_button"
  };
  save_organism_button.AddAttr("class", "freezer-save-button");
  save_organism_button.SetDisabled(!GetActiveOrganism());
  organism_header << save_organism_button;
  organism_section << organism_header;
  UI::Div organism_list{"freezer_organism_list"};
  organism_list.AddAttr("class", "freezer-list");
  for (const auto & item : freezer.organisms) {
    UI::Div row{emp::MakeString("frozen_organism_", item.id)};
    row.AddAttr("class", "freezer-item");
    row.SetAttr("data-avida-freezer-organism", item.id);
    UI::Div copy{emp::MakeString("frozen_organism_copy_", item.id)};
    copy.AddAttr("class", "freezer-item-copy");
    copy << BuildFreezerNameEditor(0, item.id, item.name, "rename_frozen_organism_");
    copy << emp::MakeString("<span>", item.instruction_count, " instructions</span>");
    row << copy;
    UI::Div actions{emp::MakeString("frozen_organism_actions_", item.id)};
    actions.AddAttr("class", "freezer-item-actions");
    UI::Button load_button{
      [this, id=item.id](){ LoadFrozenOrganism(id); },
      "Load", emp::MakeString("load_frozen_organism_", item.id)
    };
    load_button.AddAttr("class", "freezer-load-button");
    UI::Button download_button{
      [this, id=item.id](){ DownloadFrozenOrganism(id); },
      "&#x2B07;", emp::MakeString("download_frozen_organism_", item.id)
    };
    download_button.AddAttr("class", "freezer-download-button");
    download_button.SetAttr("aria-label", emp::MakeString("Download ", item.name, " organism file"));
    download_button.SetTitle(emp::MakeString("Download ", item.name, " as .org"));
    UI::Button remove_button{
      [this, id=item.id](){ RemoveFrozenItem(freezer.organisms, id); },
      "&times;", emp::MakeString("remove_frozen_organism_", item.id)
    };
    remove_button.AddAttr("class", "freezer-remove-button");
    remove_button.SetAttr("aria-label", emp::MakeString("Remove ", item.name));
    remove_button.SetTitle(emp::MakeString("Remove ", item.name));
    actions << load_button;
    actions << download_button;
    actions << remove_button;
    row << actions;
    organism_list << row;
  }
  if (freezer.organisms.empty()) {
    organism_list << "<p class='freezer-empty'>No frozen organisms.</p>";
  }
  organism_section << organism_list;
  freezer_inspector << organism_section;

  UI::Div configuration_section{"freezer_configuration_section"};
  configuration_section.AddAttr("class", "freezer-section");
  UI::Div configuration_header{"freezer_configuration_header"};
  configuration_header.AddAttr("class", "freezer-section-header");
  configuration_header << "<div><h3>Configurations</h3><p>Configured dishes before a run.</p></div>";
  save_configuration_button = UI::Button{
    [this](){ SaveConfiguration(); }, "Save Configuration", "save_configuration_button"
  };
  save_configuration_button.AddAttr("class", "freezer-save-button");
  configuration_header << save_configuration_button;
  configuration_section << configuration_header;
  UI::Div configuration_list{"freezer_configuration_list"};
  configuration_list.AddAttr("class", "freezer-list");
  for (const auto & item : freezer.configurations) {
    UI::Div row{emp::MakeString("frozen_configuration_", item.id)};
    row.AddAttr("class", "freezer-item");
    UI::Div copy{emp::MakeString("frozen_configuration_copy_", item.id)};
    copy.AddAttr("class", "freezer-item-copy");
    copy << BuildFreezerNameEditor(
      1, item.id, item.name, "rename_frozen_configuration_"
    );
    copy << emp::MakeString(
      "<span>", item.configuration.reactions.size(), " reactions, ",
      item.configuration.events.size(), " events, ",
      item.configuration.placed_organisms.size(),
      item.configuration.placed_organisms.size() == 1
        ? " placed organism</span>"
        : " placed organisms</span>"
    );
    row << copy;
    UI::Div actions{emp::MakeString("frozen_configuration_actions_", item.id)};
    actions.AddAttr("class", "freezer-item-actions");
    UI::Button load_button{
      [this, id=item.id](){ LoadFrozenConfiguration(id); },
      "Load", emp::MakeString("load_frozen_configuration_", item.id)
    };
    load_button.AddAttr("class", "freezer-load-button");
    UI::Button download_button{
      [this, id=item.id](){ DownloadFrozenConfiguration(id); },
      "&#x2B07;", emp::MakeString("download_frozen_configuration_", item.id)
    };
    download_button.AddAttr("class", "freezer-download-button");
    download_button.SetAttr(
      "aria-label", emp::MakeString("Download ", item.name, " configuration file")
    );
    download_button.SetTitle(emp::MakeString("Download ", item.name, " as .cfg"));
    UI::Button remove_button{
      [this, id=item.id](){ RemoveFrozenItem(freezer.configurations, id); },
      "&times;", emp::MakeString("remove_frozen_configuration_", item.id)
    };
    remove_button.AddAttr("class", "freezer-remove-button");
    remove_button.SetAttr("aria-label", emp::MakeString("Remove ", item.name));
    remove_button.SetTitle(emp::MakeString("Remove ", item.name));
    actions << load_button;
    actions << download_button;
    actions << remove_button;
    row << actions;
    configuration_list << row;
  }
  if (freezer.configurations.empty()) {
    configuration_list << "<p class='freezer-empty'>No frozen configurations.</p>";
  }
  configuration_section << configuration_list;
  freezer_inspector << configuration_section;

  UI::Div run_section{"freezer_run_section"};
  run_section.AddAttr("class", "freezer-section");
  UI::Div run_header{"freezer_run_header"};
  run_header.AddAttr("class", "freezer-section-header");
  run_header <<
    "<div><h3>Checkpoints</h3><p>Portable full-population files; not stored in this browser.</p></div>";
  save_run_button = UI::Button{
    [this](){ SaveRun(); }, "Save Checkpoint", "save_run_button"
  };
  save_run_button.AddAttr("class", "freezer-save-button");
  save_run_button.SetDisabled(!run_started);
  run_header << save_run_button;
  run_section << run_header;
  UI::Div import_row{"checkpoint_import_row"};
  import_row.AddAttr("class", "checkpoint-import-row");
  import_row << "<label for='checkpoint_file_input'>Load checkpoint file</label>";
  import_row <<
    "<input type='file' id='checkpoint_file_input' name='checkpoint_file_input' "
    "accept='.avida-checkpoint,application/vnd.avida.checkpoint' "
    "onchange='window.AvidaLoadCheckpointFile(this)'>";
  run_section << import_row;
  UI::Div run_list{"freezer_run_list"};
  run_list.AddAttr("class", "freezer-list");
  run_list << "<p class='freezer-empty'>Choose a checkpoint file to resume a saved run.</p>";
  run_section << run_list;
  freezer_inspector << run_section;
}
