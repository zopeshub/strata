# C++ review rubric

Every milestone review grades the C++, separately from correctness. Tests
passing is the entry ticket, not the grade. Nine areas, each scored 1–5. The
milestone's **focus areas** (listed in `docs/roadmap.md`) must reach **4** for
the milestone to count as done. Other areas must not regress below 3.

Scores mean the same thing every time:

| score | meaning |
|---|---|
| 1 | Incorrect or unsafe (UB, leaks, races, a wrong contract). Must fix. |
| 2 | Works, but in a way a senior C++ reviewer would reject: C-style, fragile, or unexplained. |
| 3 | Idiomatic and correct; would pass a normal code review. |
| 4 | Deliberate: the choices are the right ones, *and* you can defend the alternatives you rejected. |
| 5 | Exemplary: teaches the reader something; the trade-off is documented (comment or ADR) and measured when performance is claimed. |

A score of 4 or 5 requires that you **explain the choice when asked**. The
tutor will ask. Code you can't defend is scored as if a colleague wrote it.

---

## 1. Ownership and lifetimes

Who owns every resource, and when does it die?

- **1:** raw `new`/`delete`, a dangling `string_view`/`span`, use-after-move,
  a borrowed pointer that outlives its owner, a leak on an error path.
- **3:** `unique_ptr` for sole ownership, values where possible, and
  borrowed parameters as references or views. Error paths are clean (RAII).
- **4+:** lifetimes are documented at API boundaries ("must outlive", "valid
  until"). `shared_ptr` only where ownership is truly shared, and you can say
  why. Member order is chosen for destruction order.
- Probes: *Who frees this? What does this view point into after `Next()`?
  What if this constructor throws halfway?*

## 2. Value semantics and special members

- **1:** a class that owns a resource but is copyable; a move that leaves
  the source double-freeing; self-move-assignment that breaks things.
- **3:** rule of zero by default; rule of five when owning a raw resource;
  moves are `noexcept`; deleted copies where copying is meaningless.
- **4+:** you know which special members the compiler generates and suppresses.
  Moved-from states are specified. `= default` is used deliberately.
  `[[nodiscard]]` is on factories.
- Probes: *Is `vector<FileDescriptor>` growth using move or copy, and how do
  you know? What's the moved-from state?*

## 3. Type and interface design

- **1:** a boolean-parameter soup, stringly-typed options, output parameters
  where a return would do, an interface that permits misuse.
- **3:** strong types where they prevent bugs (`enum class`, small structs).
  `const`-correct. `string_view`/`span` in, owning types out. Minimal public surface.
- **4+:** invalid states are unrepresentable (`std::variant` for sum types,
  factories that can't return half-built objects). The API makes the right
  thing easy (passkey, private constructors, `explicit`).
- Probes: *Can a caller misuse this? What does the type system prevent here?*

## 4. Error handling

- **1:** ignored `Status`/`Result`, swallowed errno, exceptions escaping
  across the engine boundary, retrying `fsync` after failure.
- **3:** every fallible call is checked. Errors carry context (path, op,
  errno). `std::expected` is used with `and_then`/`transform` or early return.
- **4+:** error paths leave state consistent (strong guarantee where it
  matters, e.g. `LogAndApply`). Recoverable and fatal errors are
  distinguished. The sticky error model is understood, not copied.
- Probes: *If this write fails halfway, what state is the DB in? What does the
  caller see?*

## 5. Concurrency and the memory model

- **1:** a data race (TSan finding), a lock held across I/O without
  justification, a condition-variable wait without a predicate, "seq_cst
  everywhere" presented as "it works".
- **3:** a clear lock hierarchy; `std::scoped_lock`/`unique_lock`;
  predicate waits; atomics for counters with a stated ordering.
- **4+:** every atomic access has the weakest correct order with a
  one-line justification (acquire/release pairs are named: "pairs with the
  release in X"). No lock is held across fsync, and you can prove it.
  Shutdown is race-free.
- Probes: *What goes wrong if this is relaxed? Which store does this acquire
  synchronize with? Where exactly is the happens-before edge?*

## 6. Generic programming (templates, concepts, constexpr)

- **1:** templates where a function would do; unconstrained templates with
  cryptic errors; runtime work that could trivially be `constexpr`.
- **3:** concepts or `requires` constrain templates; `constexpr` for
  tables and constants; `if constexpr` instead of tag dispatch.
- **4+:** type erasure is chosen consciously (template front end +
  non-template core, `function_ref`-like vs `std::function`). Code bloat is
  considered. The ranges/iterator concepts are actually satisfied (`static_assert`ed).
- Probes: *What does this template instantiate per call site? Why not
  `std::function`? Does your iterator model `std::input_iterator`? Prove it.*

## 7. Performance awareness

- **1:** an allocation per key on a hot path; copying strings where a view
  would do; O(n²) where O(n log n) is obvious.
- **3:** no gratuitous allocation or copying in hot paths; `reserve` where
  sizes are known; move where ownership transfers.
- **4+:** claims are **measured** (a benchmark in `bench/`, numbers in the
  ADR). You know the cache-line and syscall costs of your design. You pick
  the simple version when the benchmark says it's enough.
- Probes: *What's the allocation count per `Put`? What did the benchmark
  say? Is this optimization worth its complexity?*

## 8. Undefined-behavior hygiene

- **1:** signed overflow, `reinterpret_cast` type punning, reading
  uninitialized memory, out-of-bounds on untrusted input, strict-aliasing
  violations, misaligned access.
- **3:** `memcpy`/`std::bit_cast` for punning; bounds checks before
  arithmetic on untrusted sizes; ASan+UBSan clean.
- **4+:** you can name the rule that makes something UB and the one that
  makes the fix legal (implicit object creation, `std::launder`, lifetime
  start). Untrusted-input parsers are written to be obviously safe.
- Probes: *Is `offset + size` able to overflow here? Is this object's
  lifetime started? Why is this cast OK?*

## 9. Readability and idiom

- **1:** unclear names; 200-line functions; comments that restate the
  code; commented-out code.
- **3:** functions do one thing; names say what, not how; comments explain
  *why* and state invariants; matches `.clang-format`; `clang-tidy` clean.
- **4+:** invariants are stated where they're maintained and asserted in
  debug builds. A newcomer could modify the code safely.
- Probes: *What invariant does this function maintain? Where is it checked?*

---

## Review output format (the tutor uses this every time)

```
## M<n> review: <date>

Gate: tools/check_milestone.sh M<n> [flags] -> PASS/FAIL (paste summary)

| area | score | evidence (file:line) |
|---|---|---|
| 1 ownership | 4 | ... |
| ... | | |

Must fix (score 1 items, correctness bugs):
- file:line: the problem, stated as a question you must answer or a failing scenario you must reproduce. No fix given.

Should fix (to reach 4 in focus areas):
- ...

Questions to answer before re-review:
1. ...

Verdict: DONE | NOT DONE (focus areas below 4: ...)
```

The tutor never writes the fix. It points at the line, names the problem
class, and asks the question that leads to the fix. If you've been stuck
after two rounds, it may point to a hint tier in `docs/hints/`.
