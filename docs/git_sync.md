# Git Sync

Notepp can synchronize an existing project Git repository between devices. The feature is global, uses the installed `git` executable and is disabled by default.

## Setup

1. Install Git and configure your normal system credential manager or SSH keys.
2. Clone the same repository separately on every device.
3. Select the repository root as the Notepp project folder. The current branch must track a named remote branch.
4. Right-click the Explorer background and enable **Git Sync**.
5. Use **Sync now** to verify the configuration.

Notepp does not create repositories, configure remotes, select branches, or store credentials.

## Automatic behavior

When enabled:

- Opening Notepp attempts a bounded `git pull --ff-only` before project state is loaded. The window may appear only after that bounded command completes or times out.
- Switching projects saves the old project, attempts to commit and push it, then attempts a fast-forward pull of the new project before loading it.
- Closing Notepp atomically saves all project files, including layout state, then runs commit/push on its serialized Git worker. The window remains responsive in its closing state until the bounded operation finishes.
- Network, authentication, missing-Git, repository, upstream, dirty-tree, timeout and divergence failures are reported but never prevent local editing or exit.

A failed push leaves the local commit intact for a later **Sync now** retry. A failed project save skips Git so incomplete canonical state is never committed.

## Safety policy

Notepp only uses fast-forward pulls and ordinary pushes to the branch's exact configured upstream. It never automatically runs reset, clean, rebase, stash, force push, branch replacement, merge conflict resolution, repository initialization or remote configuration.

All commands are launched without a shell, receive null standard input, use bounded output and timeouts, disable Git/SSH askpass and commit signing prompts, and request noninteractive lookup from the system credential manager. Credentials remain owned by system Git.

## Status and recovery

The Explorer shows Disabled, Syncing, Synced, Local changes, Ahead, Behind, Diverged, Offline, Git unavailable, Not a repository, No upstream, Conflict or Error. Hover the status or open the Explorer context menu for details. The last result is stored in the device-local Notepp settings and shown after restart.

If histories diverge, close Notepp and resolve the Git history using normal Git tools; Notepp will not select a winner. If a project file changes externally while Notepp has unsaved content, Notepp preserves local bytes in a sibling `notepp-local-conflict` recovery file instead of overwriting the external version.

## Sequential-device handoff

For the intended one-device-at-a-time workflow:

1. Close Notepp on device A and confirm its Git status is synchronized.
2. Open Notepp on device B; it pulls before loading the project.
3. If either device was offline, use **Sync now** after reconnecting.

Keep independent backups or remote-provider retention enabled. Git synchronization is version history, not a substitute for tested backups.
