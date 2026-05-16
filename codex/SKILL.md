---
name: codex
description: Delegate work to gpt-5.5 (or other models) via the codex CLI for implementation, adversarial review, second-opinion peer review, or hard-debugging tasks. Cross-model collaboration catches blind spots same-model double-passes miss.
---

# Codex CLI delegation

`codex` is the OpenAI CLI agent. Headless mode (`codex exec`) lets Claude Code subdelegate work to gpt-5.5 (or another OpenAI model) without an interactive session — codex can read files, edit files, run shell commands, and produce a final report.

## Why use codex from Claude

The two model families have **different structural failure modes**: Claude tends toward timid scope-creep and over-abstraction; gpt-5.5 tends toward overconfident edits and weaker architectural reasoning. When you cross-check one with the other, each catches what the other misses. A same-model second pass usually inherits the same blind spots.

Use codex for:

- **Implementation delegation**: a well-scoped change with clear targets, where another model writing the code lets Claude focus on review and synthesis.
- **Adversarial counter-review**: pipe `(diff + Claude's review)` to codex with prompt "what did Claude miss?" — different failure modes mean different gaps caught.
- **Second-opinion design review**: ship a plan, audit, or architectural draft to codex for adversarial peer review before finalizing.
- **Hard-debugging**: codex with `xhigh` effort is strong on subtle correctness bugs — useful when stuck on a non-obvious failure.
- **Parallel offloading**: when several disjoint tasks need doing, dispatch them to codex concurrently so Claude can stay focused on synthesis.

Don't use codex for:

- Trivial edits Claude can do faster directly — codex setup overhead beats the work.
- Anything requiring Claude's tool breadth (TodoWrite, Skill invocation, parallel agents) — codex is single-shot.
- Long-context synthesis tasks — codex's per-call context is bounded by stdin; Claude's session memory wins.
- Shared-system actions (git push, gh pr create, deploys) — keep those in Claude's hands so the user's authorization model is honored.

## Invocation patterns

### A. Implementation (codex edits files in place)

```bash
codex exec -m gpt-5.5 \
  -c model_reasoning_effort="xhigh" \
  -s workspace-write \
  --skip-git-repo-check \
  --color never \
  -C /absolute/path/to/repo <<'PROMPT'
<the brief — file:line targets, constraints, deliverable, verification command>

CONSTRAINTS:
- <project-specific rules: pre-release / no compat shims, language coverage, agnostic-core, etc.>
- Edit in-place. Do not run `git commit` — the caller commits after counter-review.

DELIVERABLE:
1. Files modified.
2. Verification command must pass before reporting done — run it yourself.
3. Final report ≤150 words: files touched, design decisions on edge cases, brief discrepancies you found and corrected.

DO NOT:
- Touch unrelated files.
- Add ENABLE_X feature flags.
- Modify shared-API modules unless genuinely missing functionality.

Begin.
PROMPT
```

Tail the output (`| tail -300`) — codex's reasoning trace is verbose; the final report is at the end.

### B. Adversarial review (read-only)

```bash
codex exec -m gpt-5.5 \
  -c model_reasoning_effort="xhigh" \
  -s read-only \
  --ephemeral \
  --skip-git-repo-check \
  --color never \
  -C /absolute/path/to/repo <<'PROMPT'
You are the counter-reviewer. Diff produced for this brief:

<<<BRIEF>>>
<verbatim brief text>
<<<END BRIEF>>>

Claude reviewed and concluded: <verdict + bullets>.

DIFF:
<<<DIFF>>>
<git diff output, truncated to critical files if >50KB>
<<<END DIFF>>>

What did Claude miss? Be terse, file:line where you have a concern.
Distinguish blockers from followups. ≤300 words.
PROMPT
```

### C. Design / plan peer review

Same as B but with the plan, design doc, or audit findings inline instead of a diff.

**The single most important thing**: explicitly tell codex to **push back, not validate**. Sycophancy is the default failure mode for design-review prompts. Include words like:

> "Your job is **not** to validate the doc. Your job is to **push back honestly** — find the strategic weaknesses, propose alternative framings, and identify questions the doc isn't asking. Sycophancy is useless here."

Structure the prompt for actionable output:

- **List specific claims/decisions to challenge** — don't say "review the doc," say "challenge claim 1, claim 2, claim 3, recorded default 4." Codex's pushback is much sharper when each target is named explicitly.
- **Per-target verdict required**: keep / modify / replace, with rationale + concrete alternative.
- **Bottom-line single-paragraph verdict** at the top, and a sources-cited list for any factual claims.
- **Length cap** (1500–2500 words is a good range for substantive reviews).

For docs that depend on external standards/specs (API protocol, security framework, wire format), enable web search and **explicitly tell codex to pin against the actual spec**, not blog examples or older SDK snippets. This catches nominal-compliance errors — designs that look right at the document level but won't conform when implemented (e.g., custom REST verbs labeled as a standard's REST binding when the standard's actual binding uses different verbs).

### D. Strategic decision consultation

Distinct from C: when the question isn't "is this design right?" but "which of these directions should we pick?" — and the user has delegated decision-making to the cross-model second opinion. Open-ended strategic prompts ("what should we do next?") produce diplomatic prose that hedges; **constrained pick-one-of-N prompts force codex to commit to a recommendation it then has to defend.**

Structure:

1. **Where we are** — paragraph stating current shipped state, with commit SHAs / file:line refs. No fluff; codex needs to ground the rec in concrete state.
2. **The open decisions** — for each decision, list 3-5 explicit options labeled (A/B/C/D, then a/b/c if a second decision). Briefly state pros and cons of each so codex doesn't have to re-derive them.
3. **Constraints worth weighting** — pre-release status, ecosystem motion, build-velocity reality, end-state targets. These are inputs codex would otherwise have to guess.
4. **Pick one + defend it** — the prompt's bottom asks for: (a) one recommendation per decision, (b) a sequencing/parallelism call if multiple decisions, (c) **"the strongest 'you're shipping the wrong thing' critique of your recommendation, with your response."** This last is the load-bearing piece — by demanding self-criticism in the same memo, the recommendation has already weathered its weakest angle before it lands.
5. **Format/length cap** — ~600-1200 words. Codex memos drift long without a cap.
6. **Hard constraints** — "no 'wait for X' without a concrete signal that X is imminent" prevents the indefinite-freeze answer; "do not propose external dependencies on unreleased products" prevents speculative roadmap.

Run as `read-only --ephemeral` since the consultation produces a memo, not edits. The memo's value is the per-target verdict + the self-critique paragraph.

**Why this shape works**: codex's failure mode on open strategic prompts is exactly the "diplomatic hedge" — listing options, weighing tradeoffs, suggesting "it depends." That's useless when the user has explicitly delegated the call. The pick-one constraint plus adversarial self-critique extracts an actual position.

## Brief construction discipline

Briefs are the load-bearing artifact of codex partnership — codex reads them once, then produces ~1-3 hours of work that has to land cleanly. A brief that drifts into "do whatever feels right" produces work that drifts proportionally. The patterns below tighten brief quality across all of A/B/C/D.

### Scope honesty: explicit "OUT OF SCOPE" lists

Every implementation brief should end with a 5-8 entry **"What's explicitly NOT in this WU"** list. Each entry names a concrete adjacent thing the brief is NOT doing, with the reason. Examples:

- "Multi-file AGT attachment (a run with two AGT receipts) — `is_file()` check picks one canonical path; multi-file is a follow-up if a customer needs it."
- "HTTP+JSON binding test (parallel to JSON-RPC) — the inbox is shared between bindings; one binding's roundtrip suffices for v1."
- "Customer-shaped e2e cross-runtime test — that's WU4."

Why this works: codex left to its own judgment will absorb adjacent unfinished work, especially when it can see related code that "obviously" needs the same fix. The OUT OF SCOPE list is the explicit license to leave it alone. Without the list, a 200-LOC brief becomes a 600-LOC PR with three other half-fixes that nobody reviews carefully.

The discipline applies to your own brief writing too: if you can't name what's *not* in scope, the scope isn't actually defined yet.

### Framing devices that bound scope

Strategic consultations (pattern D) often produce framing language worth carrying into implementation briefs **verbatim**. Examples from real consultations:

- *"X = recruitment demo, not customer proof"* — bounds the demo's quality bar.
- *"Y = debt repayment, not a grand rewrite"* — bounds a refactor's surface area.
- *"Z = best-effort hot path, not strict verification"* — bounds an integration's failure handling.

Each phrase tells codex what the WU is *for*, which gates a hundred small decisions during implementation. Paraphrasing weakens the bound; quoting preserves it.

### Brief length sanity check

Implementation briefs run 200-400 lines for a single-commit WU. Less than 150 lines usually means missing context (codex will re-derive it imperfectly). More than 500 lines usually means the WU should split — when in doubt, look for an "AND" in the goal statement and split there.

### File-disjoint partitioning is a brief constraint, not a runtime check

When dispatching parallel WUs (per the Parallel dispatch section), the brief itself should declare its file scope: "this WU modifies `src/mana/X.py` and `tests/test_X.py`; it does not touch `src/mana/Y.py`." If two parallel briefs declare overlapping scope, serialize them. Catching this at brief-review time is much cheaper than catching it after both codex runs clobber each other.

## Effort tiers — pick by codebase importance, not task size

`-c model_reasoning_effort="..."`:

- `xhigh` — **default for important codebases** (any code the user cares about). Adversarial review, security-sensitive code, hard correctness bugs, anything that ships to users. Significantly more expensive but the cost is small relative to a wrong edit in core systems.
- `high` — fine for scratch projects, throwaway scripts, exploratory work, or tasks the user has explicitly flagged as cheap.
- `medium` / `low` — only when the user has explicitly asked for a quick/cheap pass.

**Default to `xhigh` and don't downgrade to save tokens unless the user specifically asks.** Codex compute is cheap relative to bug cost in any codebase that matters. Cost optimization is downstream of correctness — only consider it after the work is verifiably right.

## Parallel dispatch (concurrent codex exec)

When multiple tasks are file-disjoint, dispatch them concurrently to compress wall-clock time. Codex calls don't share state — each `codex exec --ephemeral` is independent — so the only constraint is **the file system**: two `--workspace-write` processes editing the same file will clobber each other.

### Partitioning rules (do this FIRST, before dispatching)

1. List every file each task will modify.
2. If any file appears in two tasks' modify-lists, those two tasks **must** serialize.
3. Tasks with totally disjoint file sets can run in parallel.
4. Cross-cutting refactors (e.g. "purge X across N files") effectively serialize every adjacent task. Schedule those alone.

### Dispatch pattern

In a single Claude turn, issue ONE Bash call per task with `run_in_background=true`, redirecting output to a per-task log file:

```
# Task A
Bash(command="codex exec -m gpt-5.5 -c model_reasoning_effort=xhigh \\
  -s workspace-write --skip-git-repo-check --color never \\
  -C /abs/path < /tmp/brief_A.md > /tmp/codex_A.log 2>&1",
     run_in_background=true)

# Task B (different file scope)
Bash(command="codex exec ... -C /abs/path < /tmp/brief_B.md > /tmp/codex_B.log 2>&1",
     run_in_background=true)

# Task C
Bash(command="codex exec ... -C /abs/path < /tmp/brief_C.md > /tmp/codex_C.log 2>&1",
     run_in_background=true)
```

All three start. Each Bash call returns a shell ID immediately. Use `BashOutput` to poll, `Monitor` to stream events, or just wait for each to finish and `tail -200 /tmp/codex_X.log` to see its final report.

### Reviewing in parallel-dispatch mode

After all dispatched runs complete:

1. `git diff` on each task's expected files — codex committed nothing, only edits.
2. Run each task's verification command.
3. Per task: write Claude's review, run counter-review (pattern B), commit.

The reviews still run sequentially per task. Only the *implementation* phase is parallelized — that's where the wall-clock saving comes from.

### Cost note

Parallel dispatch multiplies per-second token spend, not total cost. Three `xhigh` calls in parallel finish faster than three sequentially but cost the same in total. If the user is worried about absolute spend (rare), serialize.

### When NOT to parallelize

- Cross-cutting refactors (lots of files touched).
- Tasks that depend on each other (one's diff is input to another's brief).
- When uncertain about file-conflict partitioning — serialize, take a small wall-clock hit, avoid the corruption risk.

### Pipelining work across rounds (cross-round parallelism)

Parallel dispatch as described above is **within a round** — multiple file-disjoint tasks running together. There's a second, often-missed speedup: **across rounds**.

While codex is implementing round N (background `workspace-write`), Claude can usefully run in parallel:

- **Consultation for round N+1** — ship round N+1's draft brief or open design questions to codex `read-only --ephemeral`. Asks "is this brief right? what's missing?" The consultation is read-only and file-disjoint from N's implementation.
- **Brief drafting for round N+1** — pure Claude work, no codex needed; happens while N runs.
- **Spec-doc dispatch for round N+2** — if N+2 is standards-compliant work, kick off the spec-doc dispatch (`docs/design/<feature>-spec.md`) so it's ready to commit by the time N's review finishes.

By the time round N's `git diff` is ready for review, you can have a vetted brief for N+1 already in `/tmp`. That compresses a 4-round arc from ~4× single-round wall-clock to ~2.5× — the bound becomes review-and-commit time, not codex think-time.

**Anti-pattern**: serial wait. Dispatching round N, then *idling on a wakeup* until it finishes, then starting round N+1's brief from scratch. The codex run-time is dead-time for everything except mechanical commit prep.

**Discipline** that keeps this safe:
- N+1's consultation must be **read-only** so it can't touch N's files.
- N+1's brief draft lives in `/tmp` until N is committed; never dispatched mid-N (avoids confusing partial state).
- Wakeup backstops still run — they catch the case where you're pipelining and miss a notification.

### Consultation findings as load-bearing prep for downstream briefs

When pattern C or D produces a memo with concrete file:line refs, those refs become **precise targets in the next round's implementation brief.** A strategic consultation that says "the missing CLI surface is at `src/mana/cli/evidence.py:8` and the StateDB query already exists at `src/mana/state.py:944`" gives you brief content you couldn't have written without the consultation — the prep work is *already done* by the time you draft the brief.

Workflow:
1. Run pattern C/D consultation → memo lands with file:line refs.
2. Quote those refs **verbatim** in the next implementation brief.
3. Codex implementing the brief sees its own previous research as input, which keeps the implementation aligned with the strategy.

The reverse anti-pattern: paraphrasing the consultation in your own words. That loses the precision that made the consultation valuable. If the memo named line 944, the brief names line 944.

This is also why pattern D consultations are worth doing even when you think you know the answer — the value isn't always the recommendation; sometimes it's the **research surface** the recommendation produces. A 30-minute consultation that catalogs every related code site can save 90 minutes of re-discovery during implementation across 4-5 follow-on briefs.

## Output handling

- Codex output is large (kilobytes to megabytes) due to reasoning trace + tool calls. Always pipe to `| tail -200` or read from a file rather than into Claude's context directly.
- The final report is at the *end* of the output — that's the actionable part.
- File modifications happen in real-time during the run; you can `git diff` to see them while codex is still running (no need to wait).

## Sandbox modes

- `read-only` — codex can read files and run read-only commands. Safe for review.
- `workspace-write` — codex can edit files inside the workspace and run shell commands. Use for implementation. Codex won't touch files outside `-C` directory.
- `danger-full-access` — no restrictions. **Avoid unless explicitly justified.**

Always pair `workspace-write` with a clear "do not run `git commit` / `git push`" instruction in the brief — codex will sometimes commit otherwise, and Claude usually wants to commit after counter-review.

## Resume / fork

`codex exec resume --last` continues the most recent session. Useful when codex hit a transient error mid-task and needs to retry without losing context. `--ephemeral` (used in pattern B) suppresses session persistence — use when you don't need to resume.

## Models available

- `gpt-5.5` — strongest OpenAI model available via codex; default for serious work.
- Other OpenAI models accessible if needed (e.g. cheaper variants for explicitly-cheap tasks).
- `--oss` flag enables open-source providers (lmstudio/ollama) for local-only work; use when avoiding upstream API costs is the priority.

## Common gotchas

- **`-C` must be absolute.** Relative paths fail or use the wrong cwd.
- **`--skip-git-repo-check`** is required when running outside a git repo OR when the repo state is something codex would balk at (uncommitted changes are usually fine, but unusual states aren't).
- **Heredoc quoting**: use `<<'PROMPT'` (single-quoted) so `$VAR` interpolation doesn't accidentally expand. Prompt content with literal `$` survives.
- **Output truncation**: when `| tail -N` truncates a real failure trace, re-read the persisted full log at the path Claude Code reports.
- **Session persistence**: codex writes session files by default. Use `--ephemeral` for one-shot work to keep `~/.codex/sessions/` clean.
- **Unintended commits**: codex will sometimes `git commit` even when not asked. Always include "do not run `git commit`" in the brief; verify with `git log --oneline -n 5` after each call.

## When to follow up with a counter-review pass

Always, on important codebases. Cross-model review is the highest-leverage quality gate available because it catches structural blind spots that no test suite or linter exposes. The pattern: after codex finishes implementing, Claude reviews → Claude pipes (diff + Claude's review) to a second `codex exec -s read-only` asking "what did Claude miss?" → if codex flags blockers, iterate; otherwise commit.

For low-stakes work, one pass is fine. For anything user-facing or core, do the second pass.

## Spec-doc-first for standards-compliant or wire-format work

When the target conforms to an external standard (an API protocol, a wire format, a security framework, an interchange schema) and the spec is non-trivial, **don't dispatch implementation directly**. Sequence:

1. **Spec-doc dispatch** (`-s read-only` if codex can write the doc to disk via a separate writer call, or `-s workspace-write` scoped to `docs/`): codex authors a design doc that profiles over the standard with version pins, citations to the actual spec, validation rules, and worked examples. Commit the doc.
2. **Implementation dispatch** (`-s workspace-write`): codex implements against the *committed* spec. The spec is the source of truth; any deviation is visible at review time.

Why this matters: **nominal compliance** ("we sign with JWS", "we expose a REST endpoint", "we emit OpenTelemetry") is worse than no compliance because it trains downstream consumers against a wrong contract. The spec-doc step catches gaps between what you *think* the standard requires and what it *actually* requires — at the doc level, where iteration is cheap. By the time the implementer dispatches, the contract is unambiguous and pinned.

Use pattern C (design peer review with adversarial framing) on the spec doc itself before implementing — that's where standards-compliance gaps surface most cheaply.

The cost is one extra round-trip; the payoff is a wire format that conforms by construction rather than by hope.
