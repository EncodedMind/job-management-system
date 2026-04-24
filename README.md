# Job Management System (JMS)

#### Ονοματεπώνυμο: ΑΝΔΡΕΑΚΗΣ ΔΗΜΗΤΡΙΟΣ
#### Αριθμός Μητρώου: 1115202300008

---

## Overview
The Job Management System (JMS) is a Unix process orchestration project that executes and tracks shell jobs through a coordinator/pool architecture.

It is built around low-level IPC mechanisms:
- Named pipes (FIFOs) for coordinator-console and coordinator-pool communication
- Process creation (`fork`) and execution (`execvp`)
- Signal-based lifecycle control (`SIGSTOP`, `SIGCONT`, `SIGTERM`)

The system consists of:
- `jms_coord`: central coordinator and pool manager
- `jms_console`: client console used to submit commands and query status
- `jms_script.sh`: helper utility for inspecting and cleaning output artifacts

All FIFOs and per-job output folders are created under a user-defined working directory.

## Build

Compile both executables (utilizes separate compilation):

```bash
make
```

Clean binaries and object files:

```bash
make clean
```

## Run

### 1) Start the Coordinator

```bash
./jms_coord -l <path> -n <jobs_pool>
```

- `-l <path>`: working directory used for FIFOs and job output folders
- `-n <jobs_pool>`: maximum number of jobs that each pool process can receive during its lifetime

### 2) Start the Console (separate terminal)

Interactive mode:

```bash
./jms_console -w <path>/jms_in -r <path>/jms_out
```

Batch mode (read commands from file, then continue from stdin):

```bash
./jms_console -w <path>/jms_in -r <path>/jms_out -o <operations_file>
```

## Console Command Reference

The following commands are sent from `jms_console` to `jms_coord`.

### `submit <executable> [arg1 arg2 ...]`
Submit a new job for execution.

Example:

```text
submit ls -la
submit /bin/sleep 10
submit grep main jms_coord.cpp
```

Behavior and important notes:
- The coordinator assigns a unique incremental `JobID`.
- The pool returns the OS process id (`PID`) of the spawned job.
- Response format:

```text
JobID: <id>, PID: <pid>
```

- Command tokenization is whitespace-based inside the pool (`istringstream` parsing).
- `submit` must include an executable command. Submitting only `submit` (without payload) is invalid usage.

### `status <jobid>`
Show the state of a specific job.

Output:
- `Active` includes running time in seconds
- `Suspended` or `Finished` are also reported

### `status-all [seconds]`
Show status for:
- all known jobs (if no argument is provided), or
- only jobs submitted in the last `<seconds>` seconds

### `show-active`
List currently active jobs.

### `show-finished`
List finished jobs.

### `show-pools`
List active pools and each pool's current active job count.

### `suspend <jobid>`
Send `SIGSTOP` to an active job and mark it as `Suspended`.

### `resume <jobid>`
Send `SIGCONT` to a suspended job and mark it as `Active`.

The system tracks suspended intervals and computes active runtime as:

$$
\text{active runtime} = \text{now} - \text{start time} - \text{total suspended time}
$$

### `shutdown`
Gracefully terminate the coordinator and all pools.

The final message reports:
- total jobs served
- jobs that were still in progress at shutdown time

## Job Output Layout

For each submitted job, JMS creates a directory under `<path>`:

```text
outputs_<JobID>_<PID>_<YYYYMMDD>_<HHMMSS>/
```

Inside each directory:

```text
stdout_<JobID>
stderr_<JobID>
```

This preserves per-job output and makes post-run auditing straightforward.

## Utility Script

Make the script executable once:

```bash
chmod +x jms_script.sh
```

Run:

```bash
./jms_script.sh -l <path> -c <list|size|purge> [n]
```

Commands:
- `list`: list all `outputs_*` directories
- `size`: show output directory sizes in ascending order
- `size n`: show only the largest `n` output directories
- `purge`: remove all `outputs_*` directories under `<path>`

## Architecture & Design Conventions

- **File Structure & Separate Compilation:** The logic is cleanly divided between `jms_coord.cpp` and `jms_console.cpp` to fulfill the multiple-file requirement. `jms_coord.cpp` was intentionally kept as a single file to avoid complex external dependencies and linking errors with global signal flags (e.g., `pool_shutdown`) across object files.
- **Dynamic Pool Creation:** A new pool is dynamically `fork()`ed when all existing pools have reached their accepted-job limit.
- **Non-blocking Coordinator Reads:** Pool-to-coordinator channels are opened with the `O_NONBLOCK` flag. They are polled without blocking so the coordinator can continuously sweep for finished jobs (`FIN|<JobID>|`) before processing the next user command.
- **Signal-Aware Waiting (`EINTR`):** When a Pool's worker finishes, the `wait()` call is interrupted by `SIGCHLD`. The code explicitly catches `EINTR` cases to safely retry or proceed without unintended pool crashes.
- **Ghost PIDs & State Management:** When a job finishes naturally, its PID is intentionally left in the Pool's `job_pid_to_id` map. If `shutdown` is called later, the Pool safely attempts to `kill()` the finished PID, catches the expected `-1` return value, and continues gracefully without fatal errors.
- **Graceful Termination & Strict Formatting:** `shutdown` propagates `SIGTERM` to the pools, and the pools cascade it down to active worker jobs to prevent zombies. All console outputs strictly match the exact formatting requested in the assignment specifications, purposefully omitting extraneous debug prints to `stdout` to ensure full compatibility with automated grading scripts.

## Demonstration

[![asciicast](https://asciinema.org/a/3MTk97yxhYgqR0Oe.svg)](https://asciinema.org/a/3MTk97yxhYgqR0Oe)