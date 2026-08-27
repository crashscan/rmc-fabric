# Wrapper Scripts

These wrapper scripts implement the runtime control surface for `network-observationd` in the **Monit-only** deployment model.

They are installed under:

```text
/usr/libexec/network-observation/
```

## Files

```text
start-network-observationd
stop-network-observationd
check-network-observationd
```

## Purpose

These scripts exist to keep service lifecycle logic out of Monit configuration and out of the daemon binary itself.

They provide a stable control interface for:

- startup sequencing
- shutdown sequencing
- pidfile handling
- D-Bus readiness checks
- health probing

Monit calls these scripts instead of invoking `network-observationd` directly.

## Common configuration

All scripts source:

```text
/etc/network-observation/network-observationd.conf
```

This file provides runtime settings such as:

- daemon path
- CLI path
- runtime directory
- pidfile path
- log file path
- transport selection
- timeouts
- D-Bus reply timeout

## `start-network-observationd`

Starts the daemon and validates that it becomes operational.

### Responsibilities

1. source runtime config
2. wait for system D-Bus to become available
3. create the runtime directory
4. refuse duplicate live starts
5. launch `network-observationd`
6. write the pidfile
7. verify D-Bus readiness using `GetReady`
8. fail and clean up if readiness is not reached in time

### Success criteria

The script exits successfully only if:

- the process is running
- the pidfile exists
- the D-Bus service answers
- `GetReady` returns `true`

## `stop-network-observationd`

Stops the daemon cleanly.

### Responsibilities

1. read the pidfile
2. send `SIGTERM`
3. wait for bounded shutdown
4. escalate to `SIGKILL` if needed
5. remove the pidfile

### Behavior

- if the pidfile is stale or the process is already gone, the script removes the pidfile and exits successfully
- shutdown is idempotent

## `check-network-observationd`

Performs a health check used by Monit.

### Responsibilities

1. verify pidfile exists
2. verify process is alive
3. verify D-Bus method `GetReady` succeeds
4. verify the returned value is `true`

### Why this matters

A running process is not enough for this daemon.

The service is only useful if it:

- owns its D-Bus name
- responds to method calls
- has completed initialization

So the health check validates **application readiness**, not just liveness.

## Monit integration

Typical Monit usage:

```monit
check process network-observationd with pidfile /run/network-observation/network-observationd.pid
    start program = "/usr/libexec/network-observation/start-network-observationd" with timeout 60 seconds
    stop program  = "/usr/libexec/network-observation/stop-network-observationd" with timeout 60 seconds

    if failed program "/usr/libexec/network-observation/check-network-observationd" then restart
```

## Manual use

These scripts can also be used directly for debugging:

```sh
/usr/libexec/network-observation/start-network-observationd
/usr/libexec/network-observation/check-network-observationd
/usr/libexec/network-observation/stop-network-observationd
```

## Design notes

### Why wrappers instead of direct Monit commands?
Using wrappers keeps:

- pidfile logic
- timeout handling
- readiness checks
- cleanup behavior

in one place.

This makes the deployment easier to debug and safer to change.

### Why no `/etc/init.d/` script?
In this deployment model, Monit is the only service manager.  
That avoids split ownership between SysV init and Monit.

### Why no D-Bus activation?
The daemon is intended to be started explicitly by Monit at boot and supervised continuously, not lazily on first D-Bus use.

## Future improvements

Possible future enhancements to the wrappers:

- support daemon flags for pidfile/logging directly
- use a dedicated D-Bus probe helper instead of inline `dbus-send`
- add structured logging to syslog
- add retry/backoff if the system bus is temporarily unavailable
