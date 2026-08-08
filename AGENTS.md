# AGENTS.md

## Communication

- The user is technical but not development-oriented. Explain changes in approachable terms and avoid unnecessary implementation detail.
- Keep final updates concise: what changed, how it was checked, and what the user needs to test.

## Quick-reference commands

- **Before committing**: `npm run check:fast`
- **After changing product sources** (cards, devices, schema, compatibility, generated outputs): `npm run check:product`
- **Type check**: `npm run check:types` (runs `tsc --noEmit`)
- **Full CI-matching check**: `npm run check:ci`
- **Parallel fast check**: `npm run check:parallel`
- **Regenerate all outputs**: `python3 scripts/build.py`
- **Verify generated outputs are current**: `python3 scripts/build.py --check`
- **Install dependencies**: `npm ci` (never `npm install`)
- **Build docs site**: `npm run docs:build`
- **Dev docs server**: `npm run docs:dev`

All checks run through `python3 scripts/check_tasks.py` with named profiles (`fast`, `product`, `ci`, `release`) and individual tasks. `npm test` and `npm run test:ci` are aliases for `check:fast` and `check:ci`.

## Source of truth (critical)

**Never hand-edit generated files.** The authored → generated contract is documented in `dev-docs/source-of-truth.md` and `product/README.md`. Key authored sources:

| Source | Feeds |
|---|---|
| `common/config/card_contract.json` | Web card constants, firmware card constants, docs |
| `common/config/entity_names.json` | Firmware YAML entity lists, web entity catalog |
| `devices/catalog.json` | `devices/manifest.json` (generated), device profiles, screen docs |
| `common/assets/icons.json` | Firmware icon header, icon glyphs, web icon registry |
| `common/config/strings.*.txt` | `components/espcontrol/i18n_generated.h` |
| `src/webserver/` | `docs/public/webserver/*/www.js` (generated per-device bundles) |

After changing any authored source, run its generated rebuild and the matching check. When in doubt, run `npm run check:product` followed by `npm run check:fast`.

Generated files are marked in `.gitattributes` and have a "BEGIN GENERATED" / "END GENERATED" comment fence.

## Architecture

- **Firmware**: C++ LVGL UI in `components/espcontrol/`. `button_grid.h` is the YAML compatibility facade; focused headers and compiled modules own the implementation.
- **Web configurator**: TypeScript in `src/webserver/`. `cards/<card>.ts` holds card-specific registrations; `application/` holds shared setup-page logic. TypeScript compilation covers only `src/webserver/**/*.ts` and `tests/web/**/*.ts`.
- **Shared contract**: `common/config/card_contract.json` is the single source for card types, fields, defaults, options, and migration aliases used by both firmware and web.
- **Devices**: Per-device ESPHome entry points under `devices/<slug>/`. `devices/catalog.json` is the authored source of device metadata.
- **Product snapshot**: `product/product_snapshot.json` is a combined hash of all authored sources; regenerated with `python3 scripts/check_product_snapshot.py --update`.

## Development workflow

- Treat `main` as the stable branch.
- For normal code, firmware, configuration, UI, or documentation changes, create a short-lived branch from the latest `main`.
- Use a separate git worktree for feature or fix work so multiple issues can be developed and tested at the same time without changing `main`.
- Use short, descriptive branch names like `fix-display-timeout` or `update-pr-workflow`; do not include `codex` in branch names or PR titles.
- Infer the branch name from the requested outcome unless the task is ambiguous.
- Keep each branch focused on one bug fix, feature, device change, cleanup, or documentation change.
- If a request starts to include unrelated work, keep the extra work out of the branch unless the user explicitly asks to include it.
- Commit completed changes and push the branch.
- Open a pull request marked ready for review so automated checks and review systems run, instead of merging directly to `main`.
- Leave the pull request open until the user confirms they have tested it.
- Do not close related GitHub issues until the user confirms the fix works.
- Only work directly on `main` when the user explicitly asks for it, or for a tiny emergency/documentation-only change where a PR would add no value.
- After a pull request is merged, clean up its local worktree and branch when practical.

### Working tree rules

- Before editing, check `git status --short --branch`.
- Stage only files that belong to the current change; use explicit paths, never `git add -A` or `git add .`.
- Never `git reset --hard`, `git clean`, or `git restore` unrelated work.
- Only commit generated files when the current source change caused the generator to update them.
- See `dev-docs/working-tree-rules.md` for the full checklist.

## Pull requests

- PR descriptions should explain the purpose of the change, the practical impact, and how it was checked.
- Include clear testing notes so the user can test the branch independently of `main`.
- If firmware needs flashing, name the affected display or device in the PR body.
- Distinguish automated checks from physical device testing. A compile/build pass is not the same as user-confirmed device testing.
- Use the automated PR testing guidance (posted as a comment by the `pr-testing-guidance` workflow) as the starting point for the PR body whenever it is available.
- The PR template is at `.github/pull_request_template.md`.

## Environment & tooling

- Node.js 24+, Python 3, ESPHome CLI (optional; needed for firmware compile/flash).
- CI: `validate` job runs `npm run check:ci` via `python3 scripts/check_tasks.py run ci`. PRs use `ubuntu-latest`; push to `main` uses self-hosted runners.
- ESPHome version pinned in `.github/esphome.env`; Renovate opens PRs to bump it.
- Exclude `.esphome/`, `.worktrees/`, `node_modules/`, generated/vendored paths from searches. Use `.gitignore` as a guide; vendored libraries include `components/libjpeg-turbo-esp32/` and `components/gsl3680/`.

## Extended reference

For deeper guidance on cards, firmware, device profiles, modal layouts, compatibility contracts, failure diagnosis, and task-specific playbooks, see `dev-docs/README.md` and the topic pages it links to.
