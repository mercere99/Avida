#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

// EM_JS definitions belong to the single web entry translation unit.
#include <cstddef>
#include <cstdint>
#include <emscripten.h>

EM_JS(void, RenderPopulationPixels,
      (const uint32_t * pixels, int width, int height), {
  // Only a deferred frame needs a copy: never retain a pointer into a C++ vector.
  if (window.__avidaPopulationRenderFrame) {
    cancelAnimationFrame(window.__avidaPopulationRenderFrame);
    window.__avidaPopulationRenderFrame = 0;
  }
  let deferredPixels;
  const render = () => {
    const canvas = document.getElementById('population_canvas');
    if (!canvas) {
      if (!deferredPixels) {
        deferredPixels = HEAPU8.slice(pixels, pixels + width * height * 4);
      }
      window.__avidaPopulationRenderFrame = requestAnimationFrame(render);
      return;
    }

    window.__avidaPopulationRenderFrame = 0;
    canvas.dataset.gridWidth = width;
    canvas.dataset.gridHeight = height;
    const displayWidth = Math.max(1, canvas.clientWidth);
    const displayHeight = Math.max(1, canvas.clientHeight);
    const pixelRatio = window.devicePixelRatio || 1;
    const pixelWidth = Math.max(width, Math.round(displayWidth * pixelRatio));
    const pixelHeight = Math.max(height, Math.round(displayHeight * pixelRatio));
    if (canvas.width !== pixelWidth) canvas.width = pixelWidth;
    if (canvas.height !== pixelHeight) canvas.height = pixelHeight;

    const byteLength = width * height * 4;
    const wasmPixels = deferredPixels
      || new Uint8ClampedArray(HEAPU8.buffer, pixels, byteLength);
    const context = canvas.getContext('2d');
    context.imageSmoothingEnabled = false;
    for (let row = 0; row < height; ++row) {
      const top = Math.round(row * pixelHeight / height);
      const bottom = Math.round((row + 1) * pixelHeight / height);
      for (let column = 0; column < width; ++column) {
        const left = Math.round(column * pixelWidth / width);
        const right = Math.round((column + 1) * pixelWidth / width);
        const offset = (row * width + column) * 4;
        context.fillStyle = `rgb(${wasmPixels[offset]}, ${wasmPixels[offset + 1]}, ${wasmPixels[offset + 2]})`;
        context.fillRect(left, top, right - left, bottom - top);
      }
    }

    // Draw the colors and lines into the same bitmap so browser subpixel rounding
    // cannot put a line in a neighboring cell.
    const lineWidth = Math.max(1, Math.round(pixelRatio));
    const lineOffset = Math.floor(lineWidth / 2);
    context.fillStyle = 'rgba(4, 17, 25, 0.28)';
    for (let column = 1; column < width; ++column) {
      const left = Math.round(column * pixelWidth / width) - lineOffset;
      context.fillRect(left, 0, lineWidth, pixelHeight);
    }
    for (let row = 1; row < height; ++row) {
      const top = Math.round(row * pixelHeight / height) - lineOffset;
      context.fillRect(0, top, pixelWidth, lineWidth);
    }
  };
  render();
});

EM_JS(bool, ConfirmPopulationRestart, (), {
  return window.confirm('Restart this population? All evolution so far will be lost.');
});

EM_JS(void, SetConfigurationControlValue,
      (const char * control_id, const char * value), {
  const control = document.getElementById(UTF8ToString(control_id));
  if (control) control.value = UTF8ToString(value);
});

EM_JS(void, UpdateOrganismTimelineControl, (size_t step, size_t max_step), {
  const slider = document.getElementById('organism_position_slider');
  if (slider) {
    slider.max = String(max_step);
    slider.value = String(step);
    slider.setAttribute('aria-valuenow', String(step));
  }
  const readout = document.getElementById('organism_position_readout');
  if (readout) readout.textContent = `${step} / ${max_step}`;
});

EM_JS(bool, BeginOrganismTimelineTaskTicks,
      (const char * tick_spec_ptr, size_t max_step), {
  const layer = document.getElementById('organism_task_ticks');
  if (!layer) return false;
  const tickSpec = UTF8ToString(tick_spec_ptr);
  const cacheKey = `${max_step}|${tickSpec}`;
  if (layer.dataset.tickSpec === cacheKey) return false;
  layer.dataset.tickSpec = cacheKey;
  layer.replaceChildren();
  return Boolean(max_step && tickSpec);
});

EM_JS(void, AddOrganismTimelineTaskTick,
      (size_t step, size_t max_step, bool is_first, const char * task_name_ptr), {
  const layer = document.getElementById('organism_task_ticks');
  if (!layer || !max_step) return;
  const taskName = UTF8ToString(task_name_ptr);
  const tick = document.createElement('span');
  tick.className = `organism-task-tick${is_first ? " is-first" : ""}`;
  tick.style.left = `${Math.min(100, Math.max(0, step * 100 / max_step))}%`;
  tick.dataset.task = taskName;
  tick.title = taskName;
  layer.appendChild(tick);
});

EM_JS(void, UpdateOrganismTransportControls,
      (bool has_subject, bool at_start, bool at_end, bool show_offspring, int run_mode), {
  const paused = run_mode === 0;
  const play = run_mode === 1;
  const fast_forward = run_mode === 2;
  const setDisabled = (id, disabled) => {
    const control = document.getElementById(id);
    if (control) control.disabled = disabled;
  };
  const setActive = (id, active) => {
    const control = document.getElementById(id);
    if (!control) return;
    control.classList.toggle('is-active', active);
    control.setAttribute('aria-pressed', active ? 'true' : 'false');
  };

  setDisabled('organism_reset_button', !has_subject || at_start);
  setDisabled('organism_step_button', !has_subject || at_end || !paused);
  const slider = document.getElementById('organism_position_slider');
  if (slider) slider.disabled = !has_subject;
  setDisabled('organism_play_button', !has_subject || at_end);
  setDisabled('organism_pause_button', !has_subject || at_end || paused);
  setDisabled('organism_fast_forward_button', !has_subject || at_end);
  setDisabled('organism_offspring_button', !show_offspring);
  setActive('organism_play_button', play);
  setActive('organism_pause_button', paused);
  setActive('organism_fast_forward_button', fast_forward);
});

EM_JS(void, ScrollTrackedGenomeHead, (int head_id, size_t position), {
  if (head_id < 0) return;
  requestAnimationFrame(() => {
    const list = document.querySelector('.genome-execution-list');
    const row = document.querySelector(
      `.genome-execution-row[data-genome-position="${position}"]`
    );
    if (!list || !row) return;
    const list_bounds = list.getBoundingClientRect();
    const row_bounds = row.getBoundingClientRect();
    const target = list.scrollTop + row_bounds.top - list_bounds.top
      - (list.clientHeight - row.offsetHeight) / 2;
    list.scrollTop = Math.max(0, target);
  });
});

EM_JS(int, GetGenomeExecutionScrollTop, (), {
  const list = document.querySelector('.genome-execution-list');
  return list ? Math.round(list.scrollTop) : 0;
});

EM_JS(void, RestoreGenomeExecutionScrollTop, (int scroll_top), {
  requestAnimationFrame(() => {
    const list = document.querySelector('.genome-execution-list');
    if (list) list.scrollTop = scroll_top;
  });
});

EM_JS(void, InstallOrganismKeyboardBridge, (size_t step_callback), {
  if (window.__avidaOrganismKeyboardBridge) return;
  window.__avidaOrganismKeyboardBridge = true;
  document.addEventListener('keydown', event => {
    if (event.code !== 'Space' || event.repeat || event.altKey || event.ctrlKey || event.metaKey) {
      return;
    }
    const target = event.target;
    if (target && (target.isContentEditable || target.tagName === 'TEXTAREA'
        || target.tagName === 'SELECT'
        || (target.tagName === 'INPUT' && target.type !== 'range')
        || target.tagName === 'BUTTON')) return;
    const mode = document.getElementById('organism_mode');
    const step = document.getElementById('organism_step_button');
    if (!mode || mode.getAttribute('aria-pressed') !== 'true' || !step || step.disabled) return;
    event.preventDefault();
    emp.Callback(step_callback);
  });
});

EM_JS(void, InstallPopulationKeyboardBridge,
      (size_t step_callback, size_t fast_forward_callback), {
  if (window.__avidaPopulationKeyboardBridge) return;
  window.__avidaPopulationKeyboardBridge = true;
  document.addEventListener('keydown', event => {
    if (event.repeat || event.altKey || event.ctrlKey || event.metaKey) return;
    const target = event.target;
    if (target && (target.isContentEditable || target.tagName === 'TEXTAREA'
        || target.tagName === 'SELECT' || target.tagName === 'INPUT'
        || target.tagName === 'BUTTON')) return;
    const mode = document.getElementById('population_mode');
    if (!mode || mode.getAttribute('aria-pressed') !== 'true') return;

    if (event.code === 'Space') {
      const step = document.getElementById('step_button');
      if (!step || step.disabled && document.getElementById('pause_button')?.disabled) return;
      event.preventDefault();
      emp.Callback(step_callback);
    } else if (event.key === '>') {
      const fastForward = document.getElementById('fast_forward_button');
      if (!fastForward || fastForward.disabled) return;
      event.preventDefault();
      emp.Callback(fast_forward_callback);
    }
  });
});

EM_JS(void, InstallOrganismModeInteractionBridge,
      (size_t scrub_callback, size_t head_callback), {
  if (window.__avidaOrganismModeInteractionBridge) return;
  window.__avidaOrganismModeInteractionBridge = true;
  document.addEventListener('input', event => {
    if (event.target?.id !== 'organism_position_slider') return;
    emp.Callback(scrub_callback, event.target.value);
  });
  document.addEventListener('click', event => {
    const marker = event.target.closest?.('[data-organism-head]');
    if (!marker) return;
    emp.Callback(head_callback, Number(marker.dataset.organismHead));
  });
});

EM_JS(void, ResizePopulationDisplay, (int width, int height), {
  const canvas = document.getElementById('population_canvas');
  if (canvas) {
    // Cell coordinates are independent of the HiDPI bitmap dimensions set by the renderer.
    canvas.dataset.gridWidth = width;
    canvas.dataset.gridHeight = height;
  }

  const surface = document.getElementById('population_grid_surface');
  if (surface) {
    surface.style.setProperty('--grid-cell-width', `${100 / width}%`);
    surface.style.setProperty('--grid-cell-height', `${100 / height}%`);
  }
});

EM_JS(void, ClearPopulationDisplay, (), {
  if (window.__avidaPopulationRenderFrame) {
    cancelAnimationFrame(window.__avidaPopulationRenderFrame);
    window.__avidaPopulationRenderFrame = 0;
  }
  const canvas = document.getElementById('population_canvas');
  if (!canvas) return;
  canvas.width = 1;
  canvas.height = 1;
  canvas.getContext('2d').clearRect(0, 0, 1, 1);
});

EM_JS(int, GetPopulationCellAtClient,
      (int client_x, int client_y, int width, int height), {
  const canvas = document.getElementById('population_canvas');
  if (!canvas || width <= 0 || height <= 0) return -1;

  const bounds = canvas.getBoundingClientRect();
  if (client_x < bounds.left || client_x >= bounds.right
      || client_y < bounds.top || client_y >= bounds.bottom) return -1;

  const x = Math.floor((client_x - bounds.left) * width / bounds.width);
  const y = Math.floor((client_y - bounds.top) * height / bounds.height);
  return x + y * width;
});

EM_JS(void, PositionActiveCellHighlight,
      (int cell_id, int width, int height), {
  const highlight = document.getElementById('active_cell_highlight');
  if (!highlight) return;
  if (cell_id < 0 || width <= 0 || height <= 0) {
    highlight.style.display = 'none';
    return;
  }

  const x = cell_id % width;
  const y = Math.floor(cell_id / width);
  highlight.style.display = 'block';
  highlight.style.left = `${100 * x / width}%`;
  highlight.style.top = `${100 * y / height}%`;
  highlight.style.width = `${100 / width}%`;
  highlight.style.height = `${100 / height}%`;
});

EM_JS(void, ShowGridCellMenu, (int client_x, int client_y), {
  const menu = document.getElementById('grid_cell_menu');
  const surface = document.getElementById('population_grid_surface');
  if (!menu || !surface) return;

  const bounds = surface.getBoundingClientRect();
  menu.style.visibility = 'hidden';
  menu.style.display = 'grid';
  const left = Math.max(4, Math.min(client_x - bounds.left, bounds.width - menu.offsetWidth - 4));
  const top = Math.max(4, Math.min(client_y - bounds.top, bounds.height - menu.offsetHeight - 4));
  menu.style.left = `${left}px`;
  menu.style.top = `${top}px`;
  menu.style.visibility = 'visible';
  const firstAction = menu.querySelector('button');
  if (firstAction) firstAction.focus();

  if (!window.__avidaGridCellMenuDismissal) {
    window.__avidaGridCellMenuDismissal = true;
    document.addEventListener('pointerdown', event => {
      const activeMenu = document.getElementById('grid_cell_menu');
      if (activeMenu && activeMenu.style.display !== 'none'
          && !activeMenu.contains(event.target)) activeMenu.style.display = 'none';
    });
    document.addEventListener('keydown', event => {
      if (event.key !== 'Escape') return;
      const activeMenu = document.getElementById('grid_cell_menu');
      if (activeMenu) activeMenu.style.display = 'none';
    });
  }
});

EM_JS(void, HideGridCellMenu, (), {
  const menu = document.getElementById('grid_cell_menu');
  if (menu) menu.style.display = 'none';
});

EM_JS(char *, LoadFreezerLocalStorage, (const char * key), {
  try {
    const value = window.localStorage.getItem(UTF8ToString(key));
    if (value === null) return 0;
    const size = lengthBytesUTF8(value) + 1;
    const buffer = _malloc(size);
    stringToUTF8(value, buffer, size);
    return buffer;
  } catch (error) {
    console.warn('Avida freezer could not read local storage.', error);
    return 0;
  }
});

EM_JS(bool, SaveFreezerLocalStorage, (const char * key, const char * value), {
  try {
    window.localStorage.setItem(UTF8ToString(key), UTF8ToString(value));
    return true;
  } catch (error) {
    console.warn('Avida freezer could not write local storage.', error);
    return false;
  }
});

EM_JS(void, DownloadBrowserFile,
      (const char * filename, const char * mime_type, const char * contents, size_t content_size), {
  const bytes = HEAPU8.slice(contents, contents + content_size);
  const blob = new Blob([bytes], {type: UTF8ToString(mime_type)});
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url;
  link.download = UTF8ToString(filename);
  link.style.display = 'none';
  document.body.appendChild(link);
  link.click();
  link.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
});

EM_JS(void, InstallCheckpointFileBridge, (size_t callback_id), {
  window.AvidaLoadCheckpointFile = input => {
    const files = input && input.files;
    if (!files || !files.length) return;
    const reader = new FileReader();
    reader.onload = load_event => {
      const bytes = new Uint8Array(load_event.target.result);
      const buffer = _malloc(bytes.byteLength || 1);
      try {
        HEAPU8.set(bytes, buffer);
        emp.Callback(callback_id, buffer, bytes.byteLength);
      } finally {
        _free(buffer);
        if (input) input.value = "";
      }
    };
    reader.onerror = () => {
      console.warn('Avida could not read the selected checkpoint file.', reader.error);
      if (input) input.value = "";
    };
    reader.readAsArrayBuffer(files[0]);
  };
});

EM_JS(void, InstallOrganismDragBridge, (size_t drop_callback, size_t freeze_callback), {
  if (window.__avidaOrganismDragBridge) return;
  window.__avidaOrganismDragBridge = true;
  let drag = null;

  const finishDrag = () => {
    const current = drag;
    drag = null;
    document.body.classList.remove('avida-organism-dragging');
    document.querySelectorAll('.is-organism-drop-target').forEach(
      element => element.classList.remove('is-organism-drop-target')
    );
    return current;
  };

  const cellAt = (canvas, clientX, clientY) => {
    const bounds = canvas.getBoundingClientRect();
    const width = Number(canvas.dataset.gridWidth);
    const height = Number(canvas.dataset.gridHeight);
    if (!width || !height || clientX < bounds.left || clientX >= bounds.right
        || clientY < bounds.top || clientY >= bounds.bottom) return -1;
    const x = Math.floor((clientX - bounds.left) * width / bounds.width);
    const y = Math.floor((clientY - bounds.top) * height / bounds.height);
    return x + y * width;
  };

  document.addEventListener('pointerdown', event => {
    if (event.button !== 0) return;
    const freezerItem = event.target.closest('[data-avida-freezer-organism]');
    if (freezerItem) {
      drag = {
        kind: 'freezer',
        id: Number(freezerItem.dataset.avidaFreezerOrganism),
        x: event.clientX,
        y: event.clientY
      };
      return;
    }

    const canvas = event.target.closest('#population_canvas');
    if (!canvas) return;
    const cell = cellAt(canvas, event.clientX, event.clientY);
    if (cell >= 0) drag = {kind: 'grid', cell, x: event.clientX, y: event.clientY};
  });

  document.addEventListener('pointermove', event => {
    if (!drag) return;
    if (!drag.moved && Math.hypot(event.clientX - drag.x, event.clientY - drag.y) < 5) return;
    drag.moved = true;
    document.body.classList.add('avida-organism-dragging');
    const target = drag.kind === 'freezer'
      ? document.getElementById('population_canvas')
      : document.getElementById('freezer_organism_section');
    if (target) target.classList.add('is-organism-drop-target');
    event.preventDefault();
  });

  document.addEventListener('pointerup', event => {
    if (!drag) return;
    const current = finishDrag();
    if (!current.moved) return;

    if (current.kind === 'freezer') {
      const canvas = document.getElementById('population_canvas');
      if (!canvas) return;
      const cell = cellAt(canvas, event.clientX, event.clientY);
      if (cell >= 0) emp.Callback(drop_callback, current.id, cell);
      return;
    }

    const freezerSection = document.getElementById('freezer_organism_section');
    if (!freezerSection) return;
    const bounds = freezerSection.getBoundingClientRect();
    if (event.clientX >= bounds.left && event.clientX < bounds.right
        && event.clientY >= bounds.top && event.clientY < bounds.bottom) {
      emp.Callback(freeze_callback, current.cell);
    }
  });

  document.addEventListener('pointercancel', finishDrag);
  window.addEventListener('blur', finishDrag);
});

