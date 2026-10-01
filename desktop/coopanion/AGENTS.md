# Desktop extension development

For changes to Coopanion or its Cortico extensions, follow the user's requested
[Cortina development workflow](https://github.com/Pal-AI-Lab/Cortina/blob/454895173ebbac408f0840679d79738185a9e202/AGENTS.md)
and its [World reading map](https://github.com/Pal-AI-Lab/Cortina/blob/454895173ebbac408f0840679d79738185a9e202/app/reading-map.md).
Read this directory's README and the README of every code unit being changed.

Derive extension rules from the pinned `vendor/cortico` checkout: `PHILOSOPHY.md`,
`AGENTS.md`, `docs/extensions.md`, `docs/worlds.md`, and the owning interfaces.
For panels also read `docs/console.md` and `src/web/shared/client-panel.ts`.
Do not modify Cortina's author instructions or templates to make a check pass.

The approved integration plan fixes the workspace layout and dependency versions.
Continue using `packages/`, `vendor/cortico`, and the root `projects/coopanion`;
do not update Cortico to main or create nested package repositories merely to
reproduce Cortina's fresh-project layout. This is an extension of the existing
`desktop-pet` World, with app-level CUA assembly hooks, not a second Persona.

Keep platform mechanics inside their World. Do not modify Cortico Core for
device behavior, write Persona Memory from a World, or treat a renderer's face
as authoritative personality. Report connection and body changes as World events.
Declare configuration in the owning ConfigGroup; app navigation may embed the
declared page but must not duplicate its configuration controls.
Read secrets through WorldContext.secret; credentials belong in the deployment
`.env` and private TLS files. Package directories remain read-only at runtime.

Before delivery run package typechecks/tests, build the console bundle, then run
`pnpm check:extension ../../packages/cortico-world-desktop-pet` and
`pnpm check:extension ../../packages/cortico-world-cua` from `vendor/cortico`.
Check enabled Mosaico declarations as well as the default disabled configuration.
Do not equate dry mounting, simulator operation, physical-board acceptance, or
the user's observed behavior. Record evidence in `docs/coopanion-acceptance.md`
at the workspace root and progress in `.agents/analysis/coopanion-journal.md`.

Save meaningful local checkpoints with explicit staged paths. The user has
authorized publication to `Colezhm/mosaico-coopanion`; this does not authorize
pushing to either upstream repository. Device overwrite still requires the
action-time confirmation specified by the root AGENTS.md.
