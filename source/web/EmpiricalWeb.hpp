#pragma once

// Empirical's pthread mode assumes that the entire C++ main runtime was moved to a worker and
// therefore forwards every DOM callback to the first pthread.  Avida deliberately keeps its UI
// runtime on the browser thread and uses its pthread only for population updates.  Compile the
// Empirical web layer in main-thread callback mode so requestAnimationFrame and control callbacks
// return to the thread that created their JSWrap entries, not the simulation worker.
#ifdef __EMSCRIPTEN_PTHREADS__
#define AVIDA_WEB_EMSCRIPTEN_PTHREADS __EMSCRIPTEN_PTHREADS__
#undef __EMSCRIPTEN_PTHREADS__
#endif

#include "emp/base/vector.hpp"
#include "emp/web/Animate.hpp"
#include "emp/web/Button.hpp"
#include "emp/web/Canvas.hpp"
#include "emp/web/Div.hpp"
#include "emp/web/Document.hpp"
#include "emp/web/Image.hpp"
#include "emp/web/Input.hpp"
#include "emp/web/JSWrap.hpp"
#include "emp/web/Selector.hpp"
#include "emp/web/Text.hpp"
#include "emp/web/emfunctions.hpp"

#ifdef AVIDA_WEB_EMSCRIPTEN_PTHREADS
#define __EMSCRIPTEN_PTHREADS__ 1
#undef AVIDA_WEB_EMSCRIPTEN_PTHREADS
#endif

