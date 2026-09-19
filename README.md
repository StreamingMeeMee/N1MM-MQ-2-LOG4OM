# N1MM-MQ-2-LOG4OM

A small C daemon that consumes N1MM Logger+ `contactinfo` messages (the "a QSO was
logged" broadcast) from a RabbitMQ queue -- as published by
[N1MM-2-MQ](https://github.com/StreamingMeeMee/N1MM-2-MQ) -- and upserts each one into a
Log4OM2 MySQL `log` table. Runs on both Linux and Windows.

## How it works

- Connects to one RabbitMQ queue (`rabbitmq.contactinfo_queue` in the config) and
  consumes messages one at a time.
- **Every message on that queue is assumed to be an N1MM `contactinfo` message** -- the
  root element's name is not checked. Point `contactinfo_queue` only at a queue that
  carries contactinfo; a message of some other type would be processed as if it were
  contactinfo (typically ending up discarded with "no mappable fields", or, if its
  fields happen to match your mapping, written as a row).
- **Two message formats are accepted**, detected from the first non-blank character:
  N1MM's own flat XML (`<contactinfo><call>W1AW</call>...`), or a flat JSON object with
  the same field names (`{"call":"W1AW",...}`), as produced by some XML-to-JSON
  forwarders. In JSON, string values are used as-is, numbers are written as plain
  decimals, `true`/`false` become `1`/`0`, and `null` or an empty `{}`/`[]` becomes an
  empty string (the same as an empty XML element). A non-empty nested object/array as a
  field value isn't supported and makes the message malformed.
- Every field of a `contactinfo` message (`call`, `mycall`, `band`, `mode`,
  `timestamp`, `ID`, ... -- see
  [N1MM's UDP broadcast docs](https://n1mmwp.hamdocs.com/appendices/external-udp-broadcasts/))
  is mapped to a target column in the configured MySQL table via `field_map`:
  - An exact (case-sensitive) entry in `field_map` wins.
  - Otherwise the **default is a column of the same name** as the XML field.
  - A mapping whose value is the string `"null"` (case-insensitive) **drops** that
    field -- it's never written.
  - A field that (after mapping) doesn't match any actual column on the table is
    silently skipped -- you don't need to map every single N1MM field, only the ones
    you want stored.
  - **Blank values** (N1MM sends many empty fields, e.g. `power`, `qth`) are handled by
    column type: for a text column (`varchar`, ...) the empty string is stored as-is; for
    a non-text column (decimal, int, datetime, JSON, ...), where MySQL's strict mode
    rejects `''` (`Incorrect decimal value: ''`), the field is left out of the statement
    entirely so the column's default applies. On a re-delivered message this means an
    empty value never overwrites an existing non-text value.
- **Fixed unit conversion**: N1MM sends `txfreq`/`rxfreq` in tens-of-Hz. Whichever
  column they end up mapped to (by default `freq`/`freqrx`) gets the value converted to
  kHz (divided by 100) -- this one conversion is built in, not configurable.
- **Band conversion**: N1MM's `band` field is a number in MHz (`3.5`, `14`, `144`); it is
  converted to the amateur band name Log4OM expects (`80m`, `20m`, `2m`), whichever
  column it is mapped to. Bands shorter than a metre use ADIF/Log4OM's names (`70cm`,
  `33cm`, `23cm`, `13cm`, `3cm`, `6mm`, ...) rather than a fractional metre. The number
  only has to fall inside a band's range, so a nominal band label and an actual
  frequency (`14.074`) both work. A value that isn't a plain number, or isn't in any
  amateur band (e.g. it's already `80m`), is stored exactly as sent, since `band` is a
  required column.
- **Automatic truncation**: at connect time the app reads the real table's columns
  (name + `character_maximum_length`) from `information_schema.columns`. Any string
  value longer than its target column's limit is truncated to fit rather than failing
  the insert.
- **`qsoid` is calculated, not taken from the message.** It is built from the time the
  message is processed (local time): a 4-digit year, then 2-digit month, day, 24-hour
  hour, minute and second, then a 3-digit random number with leading zeros -- e.g.
  `20260918231937042`, 17 characters, which fits a `varchar(18)` column. Within one
  running app, two messages handled in the same second never get the same random part.
  Any `field_map` entry that maps a field onto `qsoid` (such as N1MM's `ID`) is ignored,
  and a table with no `qsoid` column gets none. A message with nothing else mappable is
  still rejected rather than stored as an id-only row.
- Writes via `INSERT ... ON DUPLICATE KEY UPDATE`. **Because `qsoid` is generated fresh
  for each message, a message that RabbitMQ delivers twice (for example after a dropped
  acknowledgement) is stored twice, as two rows with different `qsoid`s**; the
  `ON DUPLICATE KEY` clause only comes into play if two generated ids ever collide (in
  which case the later message would overwrite the earlier row). It does require `qsoid`
  to be a UNIQUE or PRIMARY KEY, which is normal for Log4OM's `log` table.
- On a MySQL connection failure, the message is nacked with requeue so it isn't lost,
  and the app retries the MySQL connection with backoff. RabbitMQ connection loss is
  retried the same way.
- **Rejected messages**: a message that can't be turned into a row is *rejected*, in
  three cases -- it's **malformed** (neither valid XML nor valid JSON), it has **no
  mappable fields** (nothing in it maps to a real column after `field_map`), or the
  insert fails with a **query error** (bad value, constraint violation; not a
  connection problem). Rejects are handled according to
  `rabbitmq.contactinfo_queue_reject`: if set, the original body is published unchanged
  (with its original properties, persistent) to that queue and the message is
  acknowledged, so nothing is lost and it can be inspected or replayed later. If
  publishing to the reject queue fails, the message is requeued instead. If the option
  isn't set, rejected messages are **discarded permanently** rather than retried
  forever (the app prints a note about this at startup).
- In **verbose mode** (`-v`), every received message prints one line to stdout with a
  timestamp, the message type (always `contactinfo`), and what happened to it
  (`upserted`, `requeued (...)`, `discarded (...)`, or `<reason>, moved to reject queue
  '...'`). For a malformed message the verbose output also shows its raw payload on a
  second line (byte count, then the content with non-printable bytes escaped as `\xNN`,
  capped at 8192 bytes) so you can see what the sender actually put on the queue.
  Without `-v`, nothing is printed to stdout (errors/status still go to stderr).

## Building

### Dependencies

- **rabbitmq-c** (librabbitmq) -- same dependency as N1MM-2-MQ.
- **A MySQL-compatible C client**: MariaDB Connector/C or Oracle's libmysqlclient (both
  expose the same `mysql.h` API).

Install:

- **Debian/Ubuntu**: `sudo apt install librabbitmq-dev libmariadb-dev cmake build-essential`
  (or `libmysqlclient-dev` instead of `libmariadb-dev`)
- **Fedora/RHEL**: `sudo dnf install librabbitmq-devel mariadb-connector-c-devel cmake gcc`
- **Windows (vcpkg)**: `vcpkg install rabbitmq-c libmariadb`, then configure CMake with
  `-DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake`
- **Windows (MSYS2)**: `pacman -S mingw-w64-x86_64-rabbitmq-c mingw-w64-x86_64-libmariadbclient`

### Linux

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/n1mm-mq-2-log4om -v -c config.json
```

### Windows

With vcpkg (MSVC), from a "Developer PowerShell for VS":

```powershell
cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Release
.\build\Release\n1mm-mq-2-log4om.exe -v -c config.json
```

Or with MSYS2/MinGW-w64 (`-G Ninja` or `-G "MinGW Makefiles"` from an MSYS2 MinGW64
shell), if you installed the packages above.

**Running the MinGW build from a plain `cmd.exe`/PowerShell window** (i.e. one that
doesn't have the MSYS2/MinGW64 `bin` directory on `PATH`): MariaDB Connector/C pulls in
its own copy of libcurl and its TLS/HTTP2 stack, adding well over a dozen DLL
dependencies beyond the obvious `libmariadb.dll`. If those aren't found, Windows kills
the process before `main()` even runs -- so it looks like the app "silently exits",
because none of its own error messages ever get a chance to print. CMake (3.21+) handles
this automatically: after each `cmake --build`, every required DLL is copied next to
`n1mm-mq-2-log4om.exe`, so the `build` folder runs standalone. If you're on an older
CMake, you'll get a build warning and need to either upgrade CMake, always run from a
shell with MinGW64's `bin` on `PATH`, or copy the DLLs manually (run
`objdump -p build\n1mm-mq-2-log4om.exe` from an MSYS2 shell to see the direct
dependency, `libmariadb.dll`; the rest are `libmariadb.dll`'s own dependencies).

## Configuration

Copy [`config.example.json`](config.example.json) to `config.json` and edit it:

```json
{
  "rabbitmq": {
    "host": "127.0.0.1",
    "port": 5672,
    "username": "guest",
    "password": "guest",
    "vhost": "/",
    "contactinfo_queue": "n1mm.contactinfo",
    "contactinfo_queue_reject": "n1mm.contactinfo.reject"
  },
  "mysql": {
    "host": "127.0.0.1",
    "port": 3306,
    "username": "log4om",
    "password": "secret",
    "database": "log4om2",
    "table": "log",
    "verify_cert": true
  },
  "process_limit": 0,
  "field_map": {
    "call": "callsign",
    "mycall": "stationcallsign",
    "timestamp": "qsodate",
    "ID": "null",
    "zone": "cqzone",
    "txfreq": "freq",
    "rxfreq": "freqrx",
    "IsRunQSO": "null"
  }
}
```
(abbreviated -- see [`config.example.json`](config.example.json) for the complete,
field-by-field mapping of every `contactinfo` element.)

- `rabbitmq`: broker connection (`host`, `port` [default 5672], `username`, `password`,
  `vhost` [default `/`]) and `contactinfo_queue` (the queue to consume `contactinfo` messages from
  -- this should match one of N1MM-2-MQ's `message_queue_map` targets). Optional
  `contactinfo_queue_reject`: a queue name that
  rejected messages (malformed, no mappable fields, or query errors) are moved to instead
  of being discarded; it's declared (durable) at
  startup and must differ from `contactinfo_queue`.
- `mysql`: database connection (`host`, `port` [default 3306], `username`, `password`,
  `database`, `table`) plus `verify_cert` (optional, default `true`): whether the
  server's TLS certificate chain is validated. The connection is still encrypted either
  way if the server offers TLS -- this only controls whether an untrusted/self-signed
  certificate causes the connection to be rejected. Set to `false` for a private-network
  MySQL/MariaDB server with a self-signed certificate (you'll otherwise see a
  `CERT_E_UNTRUSTEDROOT`/certificate-chain error on connect).
- `process_limit` (optional, default `0`): if present and non-zero, the app processes
  exactly that many RabbitMQ messages (every message it receives and acts on --
  upserted, discarded, or requeued -- counts) and then exits cleanly (closing its
  RabbitMQ/MySQL connections first) instead of running forever. `0` or omitting it
  entirely means unlimited, the normal long-running mode. Mainly useful for testing or
  for running the app as a bounded one-shot batch job (e.g. from cron/Task Scheduler).
- `field_map`: optional overrides/drops on top of the same-name default mapping from
  N1MM XML field to MySQL column (see "How it works" above). The example above covers
  the known N1MM-name-vs-Log4OM-column mismatches
  (`call`->`callsign`, `mycall`->`stationcallsign`, `timestamp`->`qsodate`,
  `zone`->`cqzone`, `txfreq`->`freq`, `rxfreq`->`freqrx` -- the last two
  are also where the kHz unit conversion actually takes effect) plus examples of
  dropping a field (including `ID`, since `qsoid` is generated). Extend it with
  whatever other N1MM fields you want captured under a different column name, or
  dropped entirely.

## Usage

```
n1mm-mq-2-log4om [-v] [-c config.json] [-h]
  -v            verbose mode: print a line to stdout for each received N1MM message (default: off)
  -c <file>     path to JSON config file (default: config.json)
  -h            show help and exit
```

Stop with Ctrl+C -- it closes its RabbitMQ and MySQL connections before exiting.
