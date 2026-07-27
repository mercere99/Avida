# Avida Analysis Infrastructure — Planning Document

Status: **draft / sketch**  ·  Owner: Charles Ofria  ·  Last updated: 2026-07-27

This document sketches a concrete path from today's *trace-only* analysis toward a general **genome-evaluation** capability, and eventually an interactive **analyze mode**. It records the goals, the current state, the key design decisions, a phased plan, and the open risks. It is a living document — decisions marked **[OPEN]** should be resolved before the phase that depends on them.

---

## 1. Goals

### 1.1 Immediate goal — "evaluate a genome"

Given a genome (from a file, a sequence string, or a live organism), run it **in isolation** through at least one complete gestation and populate its phenotype so its **traits and computed properties are inspectable afterward**:

- `fitness` (= `MetabolicRate(size) / gestation_cost`)
- `gestation_cost`, `metabolic_mult`, `metabolic_base`
- `logic_counts` / the set of tasks performed
- inputs used, execution/error counts

Today none of these survive an analysis (see §3). This is the primitive everything else builds on.

### 1.2 Roadmap analyses (built on the evaluate primitive)

1. **Distribution of fitness effects (DFE) / mutational landscape.** Take a genome, measure its fitness, then generate **every possible single-step mutation** (each site × each alternative instruction), evaluate each, and report the distribution of *relative* fitness effects. Also report **changes in task performance** (which tasks gained/lost per mutation).
2. **Lineage analysis.** Given an ordered series of genomes forming a lineage, identify **every mutation** between consecutive steps and the **phenotypic effect** of each (fitness delta, task-set delta, gestation delta).
3. **Interactive "analyze mode".** A command-line interface to Avida for running the above interactively — load genomes, evaluate, mutate, diff — without a full evolutionary run.

---

## 2. Success criteria & constraints

- **No meaningful slowdown to regular runs.** The evolutionary hot path is `Biota::ForEachOrg` / `Find*` / `Calc*` iterating `active_bits`, plus the `ReserveOrganism` allocation. Those stay exactly as-is — the analysis pool is a **separate** vector (see §4.1), so the population path is untouched.
- **Correctness / isolation is non-negotiable.** An analysis must never place offspring, alter a live organism, advance the live RNG stream, or contribute to global statistics/output.
- **Project rules.** C++23, no exceptions (`std::expected` / `emp_assert` / `emp::notify`), must compile under native clang/gcc **and** Emscripten, Empirical idioms, `emp::Ptr` over raw pointers. Verified = compiles warning-free via `make` (and `make web` where relevant).

---

## 3. Current state (what exists today)

- **Trace path** ([Avida.hpp `TraceGenome`/`TraceOrg`/`TraceOrgForward`](source/core/Avida.hpp)): builds a temporary `organism_t`, runs a **throwaway `MakeAnalysisCopy()` of the hardware**, prints step-by-step text, and **discards everything** on return. The analysis instruction set neutralizes population-mutating callbacks (e.g. `DivideCell` → no-op).
- **Task notes (done).** `AvidaVM::analysis_notes` + `AddNote`, flushed each step by `Trace()`; `EnvironmentLogic::OnAnalyzeOutput` writes `Task performed: <NAME>` via the shared `DetectTask`. This surfaces tasks **in the trace text only** — nothing is recorded into queryable state.
- **Why fitness can't be read today** — `fitness` is a computed *organism property* ([TrackMetabolism `CalcFitness`](source/Modules/TrackMetabolism.hpp#L52)) needing two things the analysis path deliberately does **not** produce:
  - `metabolic_mult` — raised by **rewarded** tasks; analysis doesn't reward → stays `1.0`.
  - `gestation_cost` — set from `parent.Hardware().GetExeCount()` in [`OnOffspringInit`](source/Modules/TrackMetabolism.hpp#L119) at a **completed divide**; analysis neutralizes divide → stays `0` → `fitness` divides by zero.
- **Biota** ([Biota.hpp](source/core/Biota.hpp)) — one contiguous `emp::vector<organism_t> orgs` plus a **single `active_bits`**. Allocation is `active_bits.ToggleZero()` (first zero = first available slot). Capacity is **pre-reserved at startup** to `sum(module.GetOrgReserveCount()) + 1` (e.g. grid `width*height`), so `orgs` **never reallocates mid-run** — this keeps a running organism's memory from moving, and is a deliberate perf win.
- **Query / OrgSet** ([QueryValue.hpp](source/core/QueryValue.hpp), [QueryManager.hpp](source/core/QueryManager.hpp)) — an `OrgSet` is a `BitVector` of biota slots tied to a biota+epoch. Crucially, **everything is relative to `active_bits`**: `all` = `GetActiveOrgSet()`, the constructor **discards non-active slots**, and complement `~` is `GetActiveBits() & ~bits`. So **any organism not in `active_bits` is automatically invisible** to `all`, `~`, and filters.

---

## 4. Core design decisions

### 4.1 Analysis-organism storage & identity — **DECIDED: a separate pool**

Analysis organisms live in their **own vector, entirely separate from the live-population `orgs`** — never merged. This is the central simplifying decision and it makes everything downstream clean:

- **Two independent `(vector, bitvector)` pairs**, each self-contained:
  - `(orgs, active_bits)` — the live population. **Completely unchanged.**
  - `(analysis_orgs, analysis_bits)` — the analysis pool. Same first-zero `ToggleZero` allocation logic, applied to its own vector.
- No union of bitvectors, no `in_use_bits`, no three-way active/analysis/available bookkeeping within one vector — because the two worlds never share a vector. Each bitvector means "occupied slot in *my* vector."
- Analysis orgs are **real `organism_t`s**, so they carry the full phenotype and every registered trait; they are reachable through a dedicated accessor (e.g. `avida.GetAnalysisOrg(local_id)`), never through `active_bits`.

**Allocation model (also decided): declare up front, free when done.** An analysis function states how many organisms it needs, the pool is reserved to hold them **before the analysis runs**, and all of them are released when it finishes:

- Because the pool is reserved before any analysis org runs, **no growth happens mid-analysis** → no memory moves under a running org. Growth only ever happens *between* analyses, when nothing is executing. The problem that motivated the contiguous population layout simply doesn't arise here.
- Because the pool is **separate** from the population's startup reservation, sizing it per-analysis costs nothing to regular runs and needs no up-front global prediction.
- The pool keeps its high-water-mark capacity between analyses (clear `analysis_bits`, don't shrink) to avoid repeated reallocation.
- **Phase 1 is the trivial case: a single analysis org** (count = 1), reused across a sweep with results harvested into an ordinary container (e.g. `emp::vector<double>` of fitnesses, task bitsets). Larger batches use the same declare-N model.

### 4.2 The "evaluate to a completed gestation" primitive

**Decision:** a dedicated analysis org runs **its own hardware** under the analysis instruction set until its **first successful (analysis) divide**, at which point we capture `gestation_cost` and compute fitness. Because the org lives in the separate analysis pool (never in `active_bits`), running its own hardware — rather than a throwaway copy — is safe, and lets results land on a phenotype we keep.

Two behaviors must change *in analysis mode only*:

1. **Analysis Output reward.** Extend `OnAnalyzeOutput` so that, in addition to the trace note, it **tallies the task into the analysis org's own `logic_counts`** and **applies the metabolic reward to that org's `metabolic_mult`** — mirroring the live path but writing **only** to the analysis org. It must **not** touch `EnvironmentLogic::update_counts` (global) or fan out to modules that mutate population/global state.
   - *Audit needed:* confirm which `OnTaskComplete` responders are purely org-local (reward on `org.GetPhenotype()` is safe to reuse) vs. global (must be skipped). `ReactionsManager` reward is documented as org-local; `update_counts` is the known global bit to skip.
2. **Analysis Divide capture.** Replace the neutral `DivideCell` no-op with an analysis variant that, instead of placing offspring, **records `gestation_cost = GetExeCount()`** on the analysis org and **signals gestation-complete** so the evaluate loop can stop and read fitness. Still no offspring, no placement, no population effect.

**Distinction to preserve:**

- `trace <live org>` → keep today's copy-and-print behavior (must not disturb the live org).
- `analyze <genome | org-query>` → build/reuse an analysis org, run to gestation, populate & keep its phenotype for inspection.

**[OPEN]** Should `analyze` also support "run for N cycles then read partial phenotype" (no divide required), for genomes that never reproduce? Likely yes — report `fitness` as 0 / undefined but still expose task counts.

### 4.3 Query / settings isolation ("all", NOT, set algebra)

Isolation is **free** given §4.1. Analysis orgs are in a physically separate vector and never in `active_bits`, so `all`, `~`, and filters — all defined relative to `active_bits` — cannot reference them, and an `OrgSet` (which is tied to the population biota) literally cannot address a slot in the analysis pool. Expose analysis orgs only through a **separate, explicit handle** (an `analysis` accessor / an `analyze{...}` command that returns the analysis org directly).

**Deferred option — domain-tagged sets.** If interactive mode later wants full set *algebra* over analysis organisms (union/intersect/filter across many analysis orgs), give `OrgSet` a `domain` field and refuse mixing domains. Only build this when a concrete use case needs it (rule 2: no speculative generality).

---

## 5. Phased implementation plan

Each phase should end **verified** (compiles warning-free via the appropriate `make` target; the new behavior exercised on a known genome, e.g. org 582).

### Phase 0 — Task notes ✅ (complete)

`analysis_notes` + `AddNote` + `OnAnalyzeOutput`/`DetectTask`. Tasks visible in trace text.

### Phase 1 — The evaluate primitive + inspectable results  ← next

- Add the separate analysis pool (`analysis_orgs` + `analysis_bits`) with declare-count / free-when-done, and an accessor to reach an analysis org (§4.1). Phase 1 uses count = 1.
- Run the analysis org's own hardware to first analysis-divide; add analysis Output-reward and analysis Divide-capture (§4.2) — writing **only** to the analysis org.
- Harvest & expose: after evaluation, the analysis org's `fitness`, `gestation_cost`, `metabolic_mult`, `logic_counts`, inputs are readable.
- Surface via a command: `analyze <genome-sequence>` and `analyze <org-query>`; print the trait table (and optionally keep the org for a follow-up query).
- **Verify:** evaluate org 582's genome; confirm non-zero `gestation_cost`, a `metabolic_mult` reflecting its rewarded tasks, a sane `fitness`, and `logic_counts` matching the trace notes.
- **[OPEN] input determinism:** decide the input policy for evaluation (see §6).

### Phase 2 — Mutational landscape / DFE

- Primitive: `for each site, for each alternative instruction → make mutant genome → evaluate → record (fitness, task-set)`. Reuse one analysis org; accumulate into a results table.
- Report: distribution of **relative** fitness effects (mutant/wild-type), and per-mutation **task gains/losses** (diff of task bitsets).
- **Must use identical inputs across all mutants** (see §6) so differences are causal, not noise.
- **Verify:** on a small genome, spot-check a few hand-computed mutants; confirm neutral mutations give ratio ≈ 1.0.

### Phase 3 — Lineage analysis

- Input: ordered genomes (from a lineage/dumpfile). For each consecutive pair, compute the mutation set (site-level diff) and evaluate both to report fitness/task/gestation deltas per step.
- Reuse Phase 1 evaluate + Phase 2 diffing.
- **[OPEN]** lineage input format (existing genotype dump? new format?).

### Phase 4 — Interactive analyze mode (CLI)

- A REPL/command interface (load, evaluate, mutate, diff, save). Reuses Phases 1–3 as commands. The declare-count pool model already covers whatever org counts it needs.
- **[OPEN]** launch model: a distinct executable/target vs. a `--analyze` flag on `Avida`.

---

## 6. Cross-cutting challenges & risks

- **Input determinism for fair comparison (important).** Fitness depends on the environmental inputs (tasks are checked against `inputs[0/1]`). For DFE and lineage deltas to be meaningful, **every genome in a sweep must be evaluated under the same inputs** (a fixed seed / fixed input set), otherwise fitness differences are confounded by input noise. Today `OnAnalysisOrganism` uses a *copy* of the RNG (reproducible per call) — we need a defined, **stable-across-a-sweep** input policy, possibly evaluating over a fixed *sample* of input pairs and averaging.
- **Reward wiring without side effects.** The analysis reward path must write only to the analysis org; the audit of `OnTaskComplete`/output responders (org-local vs global) is a prerequisite for Phase 1 correctness.
- **Handler reachability.** With the separate pool, handlers reach the phenotype via the analysis-pool accessor rather than the `ANALYSIS_BIOTA_ID` sentinel (which stays meaning "not a live-population slot"). Confirm the analysis Output/Divide handlers can identify *which* analysis org is running.
- **Task-set semantics.** Represent "tasks performed" as a 16-bit set (or `logic_counts`) for cheap diffing. Note `FALSE`/`TRUE` are detected but not rewarded reactions — decide whether analyses count them (consistency with live detection vs. noise).
- **Emscripten.** Keep analysis code free of native-only assumptions; an interactive CLI (Phase 4) needs a portable input model or a web-specific front end.

---

## 7. Summary of recommendations

1. **Analysis orgs are real organisms in a separate pool** — their own `(analysis_orgs, analysis_bits)` pair, never merged with the population. Reach them through a dedicated accessor, never `active_bits`.
2. **Declare count up front, free when done.** The pool is reserved before an analysis runs and released after, so no growth ever happens under a running org and the population's startup reservation is untouched. Phase 1 uses a single reusable org.
3. **Add an `analyze`/evaluate primitive** that runs the org's own hardware to a captured analysis divide, rewarding tasks and recording gestation **only on that org**, so `fitness` and all traits become inspectable.
4. **Query isolation is free** — the separate pool plus `active_bits`-relative semantics mean population sets can't see analysis orgs. Defer domain-tagged sets until interactive mode needs set algebra.
5. **Nail the input policy** before DFE/lineage work — it's the difference between meaningful and noisy fitness effects.
