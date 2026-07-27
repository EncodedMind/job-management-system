# Job Management System (JMS)

This repository contains two versions of a Job Management System:

- `pipes/`: the original process-based version that uses FIFOs, `fork`, `execvp`, and signals
- `multi-threaded/`: the later TCP client/server version that uses worker threads and `pthread` synchronization

Both folders are self-contained. Each one has its own source files, `Makefile`, and project-specific README.

## Repository Layout

```text
pipes/            # FIFO-based JMS implementation
multi-threaded/   # TCP, multi-threaded JMS implementation
```

## Build From The Repository Root

This codebase depends on POSIX headers, processes, and pthread APIs.

Use the top-level `Makefile` to build either version or both:

```bash
make
```

Build only the FIFO-based version:

```bash
make pipes
```

Build only the multi-threaded version:

```bash
make multi-threaded
```

Clean both subprojects:

```bash
make clean
```

## How To Run

Run each version from inside its own folder after building it.

### `pipes/`

This version uses named pipes for communication between the console and the coordinator.

Build it with:

```bash
make -C pipes
```

Start the coordinator:

```bash
./pipes/jms_coord -l <path> -n <jobs_pool>
```

Start the console in another terminal:

```bash
./pipes/jms_console -w <path>/jms_in -r <path>/jms_out
```

Optional batch mode:

```bash
./pipes/jms_console -w <path>/jms_in -r <path>/jms_out -o <operations_file>
```

### `multi-threaded/`

This version uses TCP sockets and a thread pool.

Build it with:

```bash
make -C multi-threaded
```

Start the coordinator:

```bash
./multi-threaded/jms_coord -p <port> -l <path> -n <workers>
```

Start the console in another terminal:

```bash
./multi-threaded/jms_console -h <host> -p <port>
```

Optional batch mode:

```bash
./multi-threaded/jms_console -h <host> -p <port> -o <operations_file>
```