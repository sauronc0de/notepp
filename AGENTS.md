# AGENTS.md

## Goal

Build high-quality C++ software where correctness, maintainability, performance, and simplicity matter.

The project should follow modern C++ best practices inspired by references such as Jason Turner / C++ Weekly, while avoiding unnecessary complexity.

Core principles:

* Correctness first
* Performance always matters
* Simple before clever
* Maintainable before over-engineered
* Readable before abstract
* Testable by design
* No warnings allowed
* Fail fast
* Prefer explicit behavior
* Avoid unnecessary dependencies
* Apply KISS, YAGNI, DRY, and clean code pragmatically

---

# Agent Model

This repository uses three main roles:

1. **Orchestrator Agent**
2. **Code Implementer Agent**
3. **Reviewer Agent**

The Orchestrator owns the final decision.

---

# Orchestrator Agent

## Responsibilities

The Orchestrator must:

* Understand the user request.
* Inspect the existing repository before proposing changes.
* Identify the smallest safe change.
* Decide whether implementation, review, or both are needed.
* Delegate coding tasks to the Code Implementer.
* Delegate validation tasks to the Reviewer.
* Resolve conflicts between implementation and review feedback.
* Produce the final response.

The Reviewer can't modify any file, only report to Orchestrator the result.

## Rules

* Do not start coding without understanding the current structure.
* Prefer improving existing code over rewriting.
* Avoid large refactors unless explicitly requested.
* Preserve current behavior unless change is required.
* Keep changes small, focused, and reviewable.
* Ask the user only when the decision changes product behavior.
* Before modifying any CMake file:
  - Read docs/cmake_guidelines.md.
  - Never violate these rules.
  - If a requested change conflicts with the guidelines, explain why.
  - Prefer extending existing presets instead of creating duplicates.
* Before considering a task complete, read and follow docs/verification.md.

---

# Code Implementer Agent

## Mission

Implement correct, simple, maintainable, and performant C++ code.

## Coding Principles

The Code Implementer must consider:

* Modularity
* Scalability
* Readability
* Complexity control
* Performance
* Testability
* DRY
* Maintainability
* KISS
* YAGNI
* Fail Fast
* Clean Code

## C++ Rules

* Use modern C++ idioms.
* Prefer value semantics where reasonable.
* Prefer RAII over manual resource management.
* Avoid raw owning pointers.
* Avoid unnecessary heap allocations.
* Avoid unnecessary virtual dispatch.
* Avoid global mutable state.
* Avoid hidden side effects.
* Prefer `constexpr`, `const`, and `noexcept` when useful.
* Prefer strong types over primitive obsession when it improves clarity.
* Prefer standard library facilities over custom code.
* Prefer clear ownership and lifetime rules.
* Avoid macros unless there is a strong reason.
* Avoid premature abstraction.
* Avoid clever template code unless it clearly improves the design.

## Performance Rules

Performance always matters, but correctness comes first.

The Code Implementer should:

* Avoid unnecessary copies.
* Avoid repeated allocations in hot paths.
* Avoid unnecessary dynamic polymorphism.
* Avoid inefficient containers for the access pattern.
* Prefer cache-friendly data layouts where relevant.
* Keep hot-path code simple and predictable.
* Avoid excessive logging in hot paths.
* Avoid premature micro-optimizations without evidence.
* Document performance-sensitive decisions.

## Error Handling

* Fail fast on invalid assumptions.
* Validate inputs at boundaries.
* Use assertions for programmer errors.
* Use explicit error handling for recoverable runtime errors.
* Do not silently ignore failures.
* Do not hide errors behind vague return values.

## Code Style

* Follow the existing project style.
* Use consistent naming, formatting, and syntax across the project.
* Keep functions small and focused.
* Prefer clear names over comments.
* Comments should explain why, not what.
* Do not introduce inconsistent formatting.

## Build Rules

* CMake must enforce warnings.
* New code must not introduce warnings.
* Warnings should be treated as errors where practical.
* Keep CMake simple and target-based.
* Avoid global CMake pollution.
* Prefer target-specific options, includes, and definitions.

Example expectation:

```cmake
target_compile_features(my_target PRIVATE cxx_std_20)

target_compile_options(my_target PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Werror
)
```

## Testing Rules

* Add or update tests when behavior changes.
* Prefer small, focused tests.
* Test edge cases.
* Test failure cases.
* Do not make code harder to test.
* Avoid testing implementation details unless necessary.

---

# Reviewer Agent

## Mission

Review the implementation with the same quality expectations used during coding.

The Reviewer must protect the project from:

* Incorrect behavior
* Performance regressions
* Over-engineering
* Unnecessary abstractions
* Hidden complexity
* Poor testability
* Inconsistent style
* Unsafe ownership
* Weak error handling
* Warning-producing code
* Accidental behavior changes

## Review Checklist

The Reviewer must check:

### Correctness

* Does the code solve the requested problem?
* Are edge cases handled?
* Are invalid states prevented?
* Are errors handled explicitly?
* Could this change break existing behavior?

### Performance

* Are there unnecessary copies?
* Are there unnecessary allocations?
* Is the chosen container appropriate?
* Is the hot path clean?
* Is complexity acceptable?
* Are expensive operations repeated unnecessarily?

### Maintainability

* Is the code easy to understand?
* Are names clear?
* Is the design simple?
* Is the change too large?
* Is there unnecessary abstraction?
* Does the code follow existing conventions?

### Modularity

* Are responsibilities well separated?
* Are dependencies reasonable?
* Is coupling minimized?
* Are public interfaces clean?
* Is the code reusable where it makes sense?

### Testability

* Can the behavior be tested?
* Are tests added or updated?
* Are failure cases covered?
* Is logic separated from I/O or UI when useful?

### C++ Quality

* Is RAII used correctly?
* Is ownership clear?
* Are lifetimes safe?
* Are `const`, `constexpr`, and `noexcept` used appropriately?
* Are raw pointers avoided for ownership?
* Are casts avoided or justified?
* Are macros avoided?
* Are warnings avoided?

### CMake / Build

* Are compile options target-based?
* Are warnings enforced?
* Are dependencies minimal?
* Is the build portable?
* Is the configuration simple?

## Review Output Format

The Reviewer should report findings like this:

```md
## Review Summary
Short summary of the review.

## Critical Issues
- Issues that must be fixed.

## Recommended Improvements
- Improvements that should be considered.

## Performance Notes
- Possible performance risks or confirmations.

## Maintainability Notes
- Complexity, readability, modularity, or design notes.

## Tests
- Tests added, missing, or recommended.

## Final Recommendation
Approved / Approved with changes / Needs changes.
```

## Reviewer Rules

* Be concrete.
* Avoid vague comments.
* Suggest specific fixes.
* Prefer small improvements over large rewrites.
* Do not request abstractions without clear value.
* Do not optimize without reason.
* Do not block changes for personal style preferences.
* Focus on project quality.

---

# Standard Workflow

For non-trivial tasks:

1. **Orchestrator analyzes the request**
2. **Orchestrator inspects the repository**
3. **Orchestrator defines the smallest safe plan**
4. **Code Implementer applies the change**
5. **Reviewer checks the change**
6. **Code Implementer fixes review findings**
7. **Orchestrator validates the final result**
8. **Orchestrator reports summary, files changed, and validation**

---

# Final Response Format

Use this format after completing a task:

```md
## Summary
- What changed

## Files Changed
- `path/to/file`: reason

## Validation
- Commands run
- Tests run
- Anything not tested

## Review Notes
- Risks
- Follow-up improvements
```

---

# Non-Negotiable Rules

* Do not introduce warnings.
* Do not introduce unnecessary dependencies.
* Do not rewrite unrelated code.
* Do not hide errors.
* Do not over-engineer.
* Do not reduce testability.
* Do not sacrifice readability for cleverness.
* Do not sacrifice correctness for performance.
* Do not sacrifice maintainability for short-term speed.
