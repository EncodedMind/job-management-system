# Job Management System (JMS) - Multi-threaded TCP Coordinator

#### Ονοματεπώνυμο: ΑΝΔΡΕΑΚΗΣ ΔΗΜΗΤΡΙΟΣ
#### Αριθμός Μητρώου: 1115202300008

---

## Overview

The Job Management System (JMS) is a highly concurrent, multi-threaded server architecture that executes and tracks shell jobs.

Moving beyond IPC pipelines, this iteration utilizes a true Client-Server model over TCP/IP. The system strictly adheres to the Monitor synchronization pattern (using `pthread_mutex_t` and `pthread_cond_t`) to orchestrate a Thread Pool, entirely avoiding busy-waiting.

The system consists of:

* `jms_coord`: The central multi-threaded server managing network traffic, shared memory states, and the worker thread pool.
* `jms_console`: The client application used to connect to the server, submit commands, and query system status.

## Build

The project utilizes separate compilation and modular design. Compile the executables:

```bash
make

```

*(Note: The `Makefile` explicitly links the POSIX threads library using the `-pthread` compiler and linker flag).*

Clean binaries and object files:

```bash
make clean

```

## Run

### 1) Start the Coordinator (Server)

```bash
./jms_coord -p <port> -l <path> -n <workers>

```

* `-p <port>`: The TCP port the server will bind to and listen on.
* `-l <path>`: The working directory for all dynamically generated job output folders. The server will clean this directory upon startup.
* `-n <workers>`: The exact number of worker threads spawned in the pool. This dictates the maximum number of concurrent active jobs.

### 2) Start the Console (Client)

Interactive mode:

```bash
./jms_console -h <host> -p <port>

```

Batch mode (read commands from a file, seamlessly transition to standard input upon EOF):

```bash
./jms_console -h <host> -p <port> -o <operations_file>

```

---

## TCP Communication Protocol

To solve the issue of arbitrary TCP stream boundaries (where `read()` might return partial or merged data), the system implements a custom **Length-Prefixed Text Protocol**.

All transmissions adhere to the following format:
`<ByteLength>|<MessagePayload>`

For example, when submitting a command, the console actually transmits: `13|submit ls -la`. The receiving socket peeks at the delimiter `|`, extracts the integer length, and uses a custom `read_all()` loop to guarantee the exact message payload is retrieved before passing it to the execution logic.

---

## Console Command Reference

The system assumes standard, well-formed input as per the assignment specifications (e.g., providing valid integers for arguments like `[n]`, and providing execution payloads alongside the `submit` command).

### `submit <executable> [arg1 arg2 ...]`

Pushes a new job into the shared queue and signals an idle worker thread.

* Returns: `JobID: <id>`
* The command string is parsed using `std::istringstream` (whitespace tokenization) right before the `execvp` call.

### `status <jobid>`

Show the state of a specific job (`Queued`, `Active`, or `Finished`). Active jobs report their running time in seconds.

### `status-all [n]`

Show the status for all known jobs. If the optional integer `n` is provided, it filters the output to only display jobs submitted within the last `n` seconds.

### `show-active`

List the JobIDs of all currently active jobs.

### `show-workers`

Displays the state of the thread pool. Returns the thread ID (in hexadecimal), its current state (`idle` or `running JobID X`), and the total number of jobs that specific thread has completed.

### `show-finished`

List the JobIDs of all completed jobs.

### `shutdown`

Gracefully terminates the server using the following sequence:

1. Flags the global state as shutting down and calculates the final statistics.
2. Broadcasts the condition variable to wake all idle threads, allowing them to exit.
3. Severs the main TCP listening socket utilizing `shutdown(server_socket, SHUT_RDWR)` to instantly unblock the main thread's `accept()` loop without race conditions.
4. Uses `pthread_join` to wait for active workers to naturally finish their running child processes.
5. Discards queued jobs and exits.

---

## Job Output Layout

For each executed job, the system dynamically generates a timestamped directory under `<path>`:

```text
outputs_<JobID>_<PID>_<YYYYMMDD>_<HHMMSS>/

```

Inside each directory:

```text
stdout_<JobID>
stderr_<JobID>

```

The child process utilizes `dup2` to redirect these file descriptors before calling `execvp`, ensuring thread-safe output isolation.

---

## Architecture & Software Engineering Decisions

### 1. Modular Controller-Service Architecture

The server is explicitly decoupled into distinct modules:

* **`jms_coord.cpp` (Controller):** Handles execution flow, argument parsing, socket binding, and thread creation.
* **`protocol.cpp` (Network Layer):** A black-box API handling all `read_all` / `write_all` system calls and stream encoding.
* **`job_manager.cpp` (Data/State):** Houses the global `std::unordered_map` data structures allowing O(1) constant-time status lookups and job insertions.
* **`commands.cpp` (Business Logic):** Houses the isolated implementation logic for the 7 available user commands.

### 2. Strict Thread Safety & `localtime_r`

Following robust POSIX threading standards, the system relies exclusively on reentrant functions. For example, directory timestamps are generated using `localtime_r` rather than `localtime`. While technically invoked inside a strictly single-threaded child process post-`fork()`, enforcing the use of thread-safe system calls across the entire codebase prevents accidental shared-memory overwrites from statically allocated structs.

### 3. Multiple Consoles & Asynchronous Shutdown

The server utilizes detached handler threads for every unique client connection. If "Console A" issues a `shutdown` command, the OS will eventually kill the server process and close all TCP connections.

If a secondary "Console B" is concurrently blocked waiting for user input (`stdin`), it will not crash. When the user eventually submits a command, the console attempts to write to the severed socket using `send(..., MSG_NOSIGNAL)`. This suppresses the fatal `SIGPIPE` signal, allowing the console to gracefully detect the broken pipe, print an error, and cleanly exit.

### 4. Security Considerations & Malicious Input (`rm -rf /`)

The system executes arbitrary shell binaries. Attempting to secure the coordinator by "blacklisting" specific strings (e.g., blocking `rm -rf /`) is a known security anti-pattern, as command injection can easily bypass basic string filters (e.g., `/bin/rm`, `r\m`).

**Design Choice:** The system relies entirely on OS-level Principle of Least Privilege. By running the `./jms_coord` process as a standard user, malicious commands simply encounter `Permission denied` errors and fail to harm the host OS.

Furthermore, interactive shell commands (like `vi`, `nano`, or `top`) fail gracefully because they are detached from a TTY terminal via `dup2` output redirection. While enterprise implementations would utilize `chroot` jails or `seccomp` (Secure Computing Mode) BPF filters to fully sandbox the `execvp` child process, these were deliberately omitted to guarantee compilation compatibility across testing environments lacking the `libseccomp` dependency and `sudo` root privileges.

---

## Demonstration

