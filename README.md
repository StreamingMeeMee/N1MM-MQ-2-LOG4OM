# N1MM-MQ-2-LOG4OM

A small C daemon that consumes N1MM Logger+ `contactinfo` messages (the "a QSO was
logged" broadcast) from a RabbitMQ queue -- as published by
[N1MM-2-MQ](https://github.com/StreamingMeeMee/N1MM-2-MQ) -- and upserts each one into a
Log4OM2 MySQL `log` table. Runs on both Linux and Windows.

## How it works

- Connects to one RabbitMQ queue (config-defined) and consumes messages one at a time.
- Each message's root XML element name is its N1MM message type. Only `contactinfo`
  messages are processed; anything else is acknowledged (removed from the queue) and
  ignored, since this app's only job is turning QSOs into database rows.
- Every child element of a `contactinfo` message (`call`, `mycall`, `band`, `mode`,
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
- **Fixed unit conversion**: N1MM sends `txfreq`/`rxfreq` in tens-of-Hz. Whichever
  column they end up mapped to (by default `freq`/`freqrx`) gets the value converted to
  kHz (divided by 100) -- this one conversion is built in, not configurable.
- **Automatic truncation**: at connect time the app reads the real table's columns
  (name + `character_maximum_length`) from `information_schema.columns`. Any string
  value longer than its target column's limit is truncated to fit rather than failing
  the insert. This is what makes the default `ID` -> `qsoid` mapping work: N1MM's
  32-character GUID is truncated to `qsoid`'s 18 characters.
- Writes via `INSERT ... ON DUPLICATE KEY UPDATE`, so a redelivered message updates
  the existing row instead of creating a duplicate. **This requires whatever column you
  end up using as the dedup key (`qsoid` by default) to actually have a UNIQUE or
  PRIMARY KEY on your table** -- add one if it doesn't, e.g.:
  ```sql
  ALTER TABLE log ADD UNIQUE KEY uq_log_qsoid (qsoid);
  ```
- On a MySQL connection failure, the message is nacked with requeue so it isn't lost,
  and the app retries the MySQL connection with backoff. On a query/data error (bad
  value, constraint violation), the message is nacked without requeue -- discarded
  rather than retried forever. RabbitMQ connection loss is retried the same way.
- In **verbose mode** (`-v`), every received message prints one line to stdout with a
  timestamp, the message type, and what happened to it (`upserted`, `ignored`,
  `requeued (...)`, `discarded (...)`). Without `-v`, nothing is printed to stdout
  (errors/status still go to stderr).

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
    "queue": "n1mm.contactinfo"
  },
  "mysql": {
    "host": "127.0.0.1",
    "port": 3306,
    "username": "log4om",
    "password": "secret",
    "database": "log4om2",
    "table": "log"
  },
  "process_limit": 0,
  "field_map": {
    "call": "callsign",
    "mycall": "stationcallsign",
    "timestamp": "qsodate",
    "ID": "qsoid",
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
  `vhost` [default `/`]) and `queue` (the queue to consume `contactinfo` messages from
  -- this should match one of N1MM-2-MQ's `message_queue_map` targets).
- `mysql`: database connection (`host`, `port` [default 3306], `username`, `password`,
  `database`, `table`).
- `process_limit` (optional, default `0`): if present and non-zero, the app processes
  exactly that many RabbitMQ messages (every message it receives and acts on --
  upserted, ignored, dropped, or requeued -- counts) and then exits cleanly (closing its
  RabbitMQ/MySQL connections first) instead of running forever. `0` or omitting it
  entirely means unlimited, the normal long-running mode. Mainly useful for testing or
  for running the app as a bounded one-shot batch job (e.g. from cron/Task Scheduler).
- `field_map`: optional overrides/drops on top of the same-name default mapping from
  N1MM XML field to MySQL column (see "How it works" above). The example above covers
  the known N1MM-name-vs-Log4OM-column mismatches
  (`call`->`callsign`, `mycall`->`stationcallsign`, `timestamp`->`qsodate`,
  `ID`->`qsoid`, `zone`->`cqzone`, `txfreq`->`freq`, `rxfreq`->`freqrx` -- the last two
  are also where the kHz unit conversion actually takes effect) plus one example of
  dropping a field. Extend it with
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
