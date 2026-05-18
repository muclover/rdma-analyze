# rust-safe-ffi-design iteration-2 benchmark

| Eval | With skill | Without skill | Winner |
|---|---:|---:|---|
| obmm-full-static-design | 11/11 | 9/11 | with_skill |
| obmm-no-scan-constraint | 8/8 | 5/8 | with_skill |
| obmm-draft-review | 9/9 | 9/9 | tie |

Overall pass rate:

- with_skill: 28/28 = 1.00
- without_skill: 23/28 = 0.82

Conclusion: with_skill wins overall after the eval prompt and scoring fixes. It is especially stronger on the no-scan constrained design because it preserves Unknowns while still covering release-grade architecture, binding, safe API, CI/docs.rs and async decisions. Full static design also favors the skill. Draft review is a tie: both outputs identify the key obmm FFI risks and propose a publishable replacement architecture.

Scoring changes from iteration-1:

- Producing design documents is expected and not penalized.
- Eval prompts ask for design documentation without prescribing a fixed number of files.
- Internal reference marker checks are applied to the design output itself, not to verification-command text outside the deliverable.
