# Contributing to Numextend

Thanks for considering a contribution! This guide explains the project's
development workflow and — most importantly — **what "done" means for any
change**, so you don't get tripped up by the requirements that make this
library trustworthy.

## Documentation map

| Document | Purpose | Language |
|----------|---------|----------|
| `README.md` | Quick start, module table, build/test commands | English |
| `docs/design.md` | Type semantics (§4–§10), error/edge semantics (§11), testing strategy (§12), performance targets (§13) | Chinese |
| `coding_standard.md` | Coding conventions: naming, formatting, memory, headers, the abbreviation registry | Chinese |

Read `docs/design.md` before touching semantics; read `coding_standard.md`
before writing code.

## Development workflow

```
feature/xxx ──PR──▶ dev ──release──▶ main
```

- **Branch from `dev`**, never from `main`. `dev` is the integration branch
  and must always stay releasable (CI runs on every push and PR).
- **Merge `dev` → `main` only at release points**, then tag (`v0.x.y`) and
  publish a GitHub Release.
- Branch naming: `feature/<name>`, `fix/<name>`, `doc/<name>`, `perf/<name>`.
- Open a PR targeting `dev`; the CI matrix (Linux/macOS/Windows) must pass.

## Building and testing

```sh
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure   # full suite
./build/demo                                 # capability demo
./build/bench                                # performance baseline
```

Extra validation (Windows):

```sh
scripts/build-msvc-debug.bat   # MSVC Debug + CRT debug-heap checks
# then: cd build-dbg\tests && runall.bat    # 0 leaks / 0 corruption
```

Golden tests take a random seed; run extra seeds to shake out flakiness:

```sh
NEX_GOLDEN_SEED=1 ctest -R golden
NEX_GOLDEN_SEED=2 ctest -R golden
```

Coverage (GCC):

```sh
sh scripts/coverage.sh
```

## Code conventions (summary)

The full rules live in `coding_standard.md`; the rules people actually trip on:

- **Naming**: module prefixes (`bigint_bin_*`, `bigdecimal_*`, `nex_convert_*`),
  error/suffix enums end in `_E`, context types in `_ty`.
- **Normalization invariants**: bigfloat mantissas keep the top bit set;
  bigdecimal mantissas have no trailing decimal zeros. Every public output
  must satisfy its invariant (tests assert this).
- **Strong exception safety** (§3.1/§11): on OOM, output parameters are left
  unchanged. The OOM-injection suite enforces this — do not "swallow" OOM.
- **Error-code mapping**: when a lower-layer error crosses a module boundary,
  map `OOM → OOM` faithfully. Mapping OOM to `INVALID` is a defect (it has
  happened; the OOM test catches it).
- **C99, portable**: no compiler-specific extensions; must build clean under
  GCC (`-Wall -Wextra -Wpedantic`) and MSVC.
- All allocations go through `nex/nex_alloc.h` (`nex_malloc`/`nex_realloc`) —
  the OOM-injection hook depends on it.

## Definition of done

A change is not done until **all** of the following are true:

1. **Unit tests** — every new public function covered: happy path, every
   error code, boundary values (0, ±1, limb-carry edges `2^32−1`/`2^32`,
   `10^9−1`/`10^9`, min/max precision).
2. **Golden test** — a line-protocol driver plus a Python exact reference
   (`fractions.Fraction` / `decimal`), registered in `tests/CMakeLists.txt`.
   The reference must be **independent** of the implementation algorithm
   (see `tests/golden_sqrt.py` for the boundary-comparison pattern), not a
   re-implementation of the code under test.
3. **Robustness tests**, as applicable:
   - tie/rounding-boundary construction (`tests/test_rounding_tie.c`);
   - hardware comparison (`tests/test_bigfloat_fuzz.c` — vs `double`/`float`);
   - **OOM injection over the new allocation sites** (`tests/test_oom.c`).
4. **Documentation sync**:
   - `docs/design.md`: revision-log entry, §10 matrix / §N updates,
     appendix-A abbreviation registration for new prefixes;
   - `README.md` if the public surface changes.
5. **Full suite green**: `ctest` 26/26 (or more), plus at least 2 extra
   golden seeds.

If a requirement is impractical for a particular change, say so in the PR —
don't silently skip it.

## Commit messages

```
<module>: <summary>

<optional body: what and why, not how>
```

Descriptive, imperative tone. The project's working language is Chinese
(historical commits are Chinese); English is welcome — just stay consistent
within a change series.

## Submitting issues

Use the issue templates:

- **Bug report**: include the exact input, expected output, actual output,
  and the ctx/version. A minimal repro is worth more than a description.
- **Feature request**: point to the relevant `docs/design.md` section;
  describe the semantics, not the implementation.

## License

By contributing you agree that your work is licensed under the same MIT
license as the project (see `LICENSE`).
