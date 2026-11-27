# Agent working instructions

## Branch cleanup

- After a task branch is merged into the repository's main/default branch,
  delete that task branch both remotely and locally when present.
- Verify the merge first, including squash/rebase merges. Do not delete an
  unmerged branch or lose uncommitted work. Switch to the main/default branch
  before deleting the checked-out task branch.
- Apply this rule to branches created or used for this task; do not delete
  unrelated branches. This repository currently uses `master` as its default
  branch.

## Docker cleanup

- Delete containers created for the task when they are no longer needed,
  including after successful tests, failures, or cancellation.
- Prefer `docker run --rm` for disposable containers. Give task containers
  explicit names or labels so ownership and cleanup can be verified.
- Check for leftover task containers before finishing; stopping a Docker CLI
  process alone does not prove the container was removed.
- Remove only the task's containers. Do not prune unrelated containers or
  delete images, volumes, or persistent data as part of container cleanup.
