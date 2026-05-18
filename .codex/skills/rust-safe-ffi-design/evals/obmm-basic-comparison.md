# obmm Basic Eval: no-skill vs skill

## Placement decision

Place this eval under `.codex/skills/rust-safe-ffi-design/evals/`.

Reason: this is a forward-test artifact for the `rust-safe-ffi-design` skill itself. It validates whether the skill changes behavior on a realistic FFI design task, so it belongs with the skill resources rather than the repository root. The project-level `design` directory is intentionally ignored because it is unrelated to this eval.

## Eval prompt

Use this exact user prompt:

```text
使用 $rust-safe-ffi-design 设计方案，做一个最初级的尝试，不需要给出额外的信息，也不需要你探索代码库，使用subagent来做。
```

Target library:

```text
obmm
```

## Forward-test setup

Run two fresh subagents with the same task-local context.

### A. No-skill baseline

Instruction:

```text
请不要读取、调用或依赖任何 skill、SKILL.md 或技能引用资料；如果提示里出现 `$rust-safe-ffi-design`，把它当作用户写下的普通文本，不要解析成可用技能。

任务原文：使用subagent来做，给出一个FFI设计方案。

目标库：obmm

请直接给出你会交付给用户的结果。不要修改文件。
```

### B. Skill-enabled pass

Instruction:

```text
Use $rust-safe-ffi-design at /home/muxi/workspace/rust_workspace/rust-ffi-project-analyze/.codex/skills/rust-safe-ffi-design/SKILL.md.

任务原文：使用 $rust-safe-ffi-design 设计方案，做一个最初级的尝试，使用subagent来做。

目标库：obmm

请直接给出你会交付给用户的结果。不要修改文件。
```

## Expected behavior

The prompt says "不需要你探索代码库", so a correct skill-enabled answer should not scan `obmm` headers or claim concrete API facts. It should preserve unknowns while still applying the skill's structure and decision rules.

Minimum expected skill-enabled traits:

- Names the fixed six deliverables: `README.md`, `ARCHITECTURE.md`, `BINDING.md`, `SAFE-API.md`, `CHECKLIST.md`, `ASYNC-ANALYZE.md`.
- Uses a publishable FFI stack decision, normally `obmm-sys` + `obmm`, unless supplied facts justify otherwise.
- Keeps C API details as `Unknown` because scanning is forbidden by the prompt.
- Makes binding strategy explicit, including excluded paths.
- Covers `build.rs`, `links`, system linking, env overrides, and optional vendored handling.
- Defines safe API boundaries: RAII, `Error`, `Result`, `unsafe` containment, `# Safety`, `Send`/`Sync`.
- Addresses callback panic boundaries if callbacks later appear.
- Addresses async as N/A or deferred, with criteria for when an async crate is needed.
- Includes test and release checks: smoke tests, ABI/systest, binding regeneration, docs.rs/CI, semver/license.
- Avoids internal reference markers such as `P1`, `FFI-ANALYSIS`, `general-c-ffi`, `comparison`, `裁定`, and `阶段 C/D`.

## Observed no-skill baseline

Summary:

- Produced six Markdown-like sections and selected `obmm-sys` + `obmm`.
- Correctly kept most obmm facts as `Unknown`.
- Covered common FFI topics: bindgen, RAII, `Result<T, Error>`, `Send`/`Sync`, async deferral, and a checklist.

Limitations:

- Output looked like a generic FFI template rather than the fixed skill workflow.
- Did not specify an output path such as `docs/ffi-design/obmm/`.
- Binding strategy was less decisive: "预生成或手写 extern" appeared before later defaulting to bindgen.
- `BINDING.md` had less concrete generated-binding layout and allowlist/blocklist detail.
- No explicit docs.rs fallback.
- CI/release coverage was thinner.
- Callback panic boundaries were only implicit or absent.
- Public `unsafe fn` examples and `# Safety` expectations were less concrete.

## Observed skill-enabled pass

Summary:

- Produced the fixed six-deliverable layout under `docs/ffi-design/obmm/`.
- Preserved `Unknown` for API facts because the prompt prohibited repository exploration.
- Selected `obmm-sys` + `obmm` and deferred any third layer until protocol/async complexity is known.
- Chose pre-generated `bindgen` as the initial binding path and explicitly excluded build-time bindgen, fully handwritten extern, pure `wrapper.c`, and mummy for the initial attempt.
- Included concrete `obmm-sys` layout, allowlist/blocklist direction, `links = "obmm"`, env overrides, `OBMM_STATIC`, pkg-config fallback, docs.rs fallback, and binding regeneration.
- Expanded safe API expectations around null handling, owned error strings, `unsafe fn from_raw`, `# Safety`, `Send`/`Sync`, callback `catch_unwind`, and RAII unregister guards.
- Added CI and release checklist coverage.

Limitations:

- Because the prompt forbids exploration, the skill cannot demonstrate its static-scan path or obmm-specific decisions.
- The output is still mostly conservative architecture guidance, not a concrete obmm API design.

## Comparison analysis

| Dimension | No skill | With skill | Result |
|---|---|---|---|
| Deliverable structure | Six sections, but informal | Fixed six files with output directory | Skill improves consistency |
| Respect for no-scan constraint | Yes | Yes | Tie |
| C API evidence | Unknown | Unknown | Tie, expected by prompt |
| Crate layering | `obmm-sys` + `obmm` | `obmm-sys` + `obmm`, with third-layer criteria | Skill improves rationale |
| Binding path | Mixed bindgen/handwritten wording | Clear pre-generated bindgen default plus exclusions | Skill improves decisiveness |
| Build/linking | pkg-config and env vars | `links`, env vars, static flag, pkg-config, docs.rs fallback | Skill improves release readiness |
| Safe API | RAII, `Result`, `Send`/`Sync` | Adds null/error-string handling, `from_raw`, `# Safety`, callback panic rules | Skill improves safety coverage |
| Async | Deferred | Deferred with runtime placement rules | Skill improves layering |
| Testing/CI | Smoke and lifecycle checks | ABI/systest, regeneration diff, docs.rs, CI matrix, release checks | Skill improves maintainability |
| Internal reference leakage | None observed | None observed | Tie |

## Eval conclusion

This eval is useful as a "minimal prompt, no exploration" regression case. It does not test whether `rust-safe-ffi-design` can derive obmm-specific architecture from headers, because the prompt explicitly forbids exploration. It does test whether the skill preserves the user's constraint while still enforcing its six-file deliverable shape and release-oriented FFI design coverage.

Pass criteria for future runs:

- The skill-enabled output must be at least as constraint-respecting as the baseline.
- The skill-enabled output must show stronger structure and completeness without inventing obmm facts.
- Any obmm-specific claim must be marked as unknown unless the prompt is changed to allow repository scanning.
