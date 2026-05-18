# rust-safe-ffi-design iteration-1 benchmark

| Eval | With skill | Without skill | Winner |
|---|---:|---:|---|
| obmm-full-static-design | 11/11 | 7/11 | with_skill |
| obmm-no-scan-constraint | 4/8 | 4/8 | tie |
| obmm-draft-review | 6/9 | 8/9 | without_skill |

Overall pass rate:

- with_skill: 21/28 = 0.75
- without_skill: 19/28 = 0.68

Conclusion: with_skill is better overall on these evals, mainly because the full static design output is much more complete and release-oriented. The result is not clean: the skill underperforms on prompt discipline. It failed to enforce the fixed six-file structure in the no-scan case, generated files under `docs/ffi-design/obmm/` even though the eval asked for output text, and leaked the internal-marker check regex in the draft-review final response. Those generated docs were removed after scoring.
