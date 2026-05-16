---
description: Shared implementation skill — load standards, write code + tests, verify via test loop
argument-hint: [description or spec/inflight/*.feature path]
---

# /code — Implementation Engine

Shared skill for writing code and verifying it works. Used directly by users ("build this") and delegated to by `/fix` and `/refactor` for their coding phase.

**No branch pre-flight or BDD check** — callers handle those. `/code` is pure implementation.

---

## Phase 1: Determine Scope

**If $ARGUMENTS points to a spec file** (path matches `spec/inflight/*.feature` or `spec/inflight/**/*.feature`):
- Read the spec file to understand the task context (scenarios, acceptance criteria)
- Note: on success, this spec will be advanced to `spec/review/`

**If $ARGUMENTS is a description** (not a file path):
- Use it as the implementation context. This is a pure code change — no spec advancement.

**If $ARGUMENTS is empty:**
- Ask the user: "What should I implement?"
- Wait for response. This is a pure code change — no spec advancement.

---

## Phase 2: Load Standards

Detect language(s) from file extensions of files in scope:

| Extension | Language | Full Standard File |
|---|---|---|
| `.py` | Python | `python-code-standard.md` |
| `.js`, `.jsx` | JavaScript | `javascript-code-standard.md` |
| `.ts`, `.tsx` | TypeScript | `typescript-code-standard.md` |
| `.sh`, `.bash` | Bash | `bash-code-standard.md` |
| `.java` | Java | `java-code-standard.md` |
| `.cpp`, `.h`, `.hpp` | C++ | `cpp-code-standard.md` |
| `.go` | Go | `go-code-standard.md` |
| `.rs` | Rust | `rust-code-standard.md` |
| `.feature` | Gherkin | `gherkin-code-standard.md` |

For each detected language, read the full standard file. Find it by checking, in order:
1. A `languages/` subdirectory alongside the project's `CLAUDE.md`
2. The same directory as the project's `CLAUDE.md`

If not found, fall back to the brief standard already in context.

---

## Phase 3: Implement

Write the code following all loaded standards. Pay particular attention to:
- Linting commands and formatting rules from the standard
- Docstring/comment format required
- Test file naming and structure
- Any project-specific overrides in `CLAUDE.local.md`

**MANDATORY: Write unit tests** for all new public functions and methods. Follow the testing standards from CLAUDE.md:
- Test both success and failure scenarios
- Use the appropriate test framework for the language
- Place tests in the correct directory per project conventions

---

## Phase 4: Verify Loop

After implementation, verify that tests pass. Repeat until they do (max 3 fix attempts).

**1. Run tests:**
- Invoke `/test` scoped to the affected test files

**2. If tests pass:**
- Proceed to Phase 5.

**3. If tests fail:**
- Increment fix attempt counter
- If fix attempts > 3: inform user "Tests failed after 3 fix attempts — manual intervention needed." and stop
- Analyze the failure output
- If the failure is related to the primary implementation: fix it directly
- If the failure is unrelated to the primary implementation: invoke `/fix` with the failure context
- Return to step 1 of this phase

---

## Phase 5: Complete

**If argument was a spec file:**
- Advance the spec: `git mv` the file from `spec/inflight/<path>` to `spec/review/<path>` (preserving subdirectory structure)
- `git add` the move

**Summary:**
- What was implemented
- Files created/modified
- Test results
- Spec status (advanced to review, or N/A for pure code changes)
