![Nerak C](./images/nerak_scene.png)

## Why Nerak

Nerak is a protocol agnostic declarative framework for building asynchronous web applications in C in a Self Contained System architecture.

* **Self contained systems.** Break apps into fully self contained modules that is enforced by compiler.
* **Durable tasks and events.** Both are persisted. After a crash, incomplete tasks resume at the step where they stopped and undelivered events replay on the next boot.
* **No build configuration.** Compilation, hot code reloading, and HMR are handled by the framework. No build scripts, package managers, or ORMs. SQL and HTML assets are discovered automatically.
* **Memory, concurrency, and I/O managed by the framework.** Application code does not call malloc/free  or manage threads, mutexes, or locks. Queries run as prepared statements. Pipeline steps emit OpenTelemetry spans, logs, and errors automatically.
* **Protocols supported.** HTTP, RNS, NomadNet, Gemini, Finger. Use multiple protocols in the same app.
* **Databases supported.** Sqlite, Postgres, Mysql, Redis, DuckDb. Multi-tenant database support is built in. Use multiple databases and engines in the same app.
* **Bundled modules.** MustacheHtml, HTML, MustacheMarkdown, Markdown, Micron, Datastar, HTMX, Tailwind, DaisyUI, Session Auth, WebAuthN, OTP.

---

## Table of Contents
* [Quick Start](#quick-start)
* [Philosophy](#philosophy)
* [Tooling](#tooling)
* [Built With](#built-with)
* [License](#license)
* **Documentation**
  * [Guide](./GUIDE.md) — build a todo app one concept at a time
  * [Reference](./REFERENCE.md) — full options for every step, helper, and field

---

## Quick Start

Everything runs in Docker. No other local dependencies.

```bash
mkdir myapp && cd myapp
wget https://docker.nightshadecoder.dev/nerak/compose.yml

# Dev server on :3000, telemetry on :4000
# Includes file watching, auto compilation, hot code reloading, HMR
docker compose up
```

Create `app.c` with the example below. Nerak watches for changes and hot-reloads on save. Use your own editor, or attach to the built-in TUI with `docker compose attach nerak` for an integrated editor, LSP, and console.

```c
#include <nerak.h>
#include <http.h>

config(app){
  context("hello", "<h1>Hello, world!</h1>");
  http("home", "/", .get = {http_response("hello")});
}
```

A module is any `.c` file declaring `config(name){...}`; `app.c` declares the `app` module. `config(app)` runs once at boot. `context()` registers a named template inline in the context; `http()` declares the `home` resource mapping `/` to a GET pipeline that renders that template. `http()` comes from `http.h`; the core itself has no notion of HTTP. See the [Guide](./GUIDE.md) for a step-by-step walkthrough.

---

## Philosophy

An application is a data transformation: input arrives, gets transformed, leaves as output. Nerak arranges these into pipelines: ordered lists of steps that turn a request into a response.

All assets and tooling are standard: SQL, JSON, Markdown, HTML, CSS, JS, Micron, business logic is plain C, lldb for debugging, Playwright and Criterion for testing, OpenTelemetry for observability.

### Everything is a String

The web is text: HTTP, RNS, Micron, HTML, Markdown, JSON, SQL. The pipeline context stores and passes data as strings. There is no intermediate parsing or serialization layer. Strings are interpolated into SQL, templates, and URLs with `{{context_key}}`.

### CLAD

Four principles:

* **(C)omposable:** small, independent steps chain into feature pipelines.
* **(L)ocality of Behavior:** behavior is apparent from reading the code. SQL, templates, and logic for a feature live together, not spread across model, view, and controller trees.
* **(A)utonomous:** modules are self-contained: own schemas, migrations, seeds, routes/resources, UI, and logic. The compiler enforces boundaries.
* **(D)omain Based:** each module owns one slice of the app. A `todos` module defines everything related to todos and nothing else.

Inspired by:

* [Data Oriented Design](https://youtu.be/rX0ItVEVjHc)
* [A Philosophy of Software Design](https://youtu.be/bmSAYlu0NcY)
* [CUPID](https://youtu.be/cyZDLjLuQ9g)
* [Self-Contained Systems](https://youtu.be/Jjrencq8sUQ)
* [Locality of Behavior](https://htmx.org/essays/locality-of-behaviour)

---

## Tooling

### Development Environment
Built-in TUI editor with HMR, LSP support, and integrated source control.

### Introspection
`/app_info` is a built-in resource in dev builds. Query it like any other endpoint:
```bash
curl localhost:3000/app_info            # view topology
curl localhost:3000/app_info/resources  # list all resources
curl localhost:3000/app_info/pipelines  # inspect pipelines
curl localhost:3000/app_info/events     # view pub/sub map
curl localhost:3000/app_info/databases  # inspect schemas
```
Production builds omit it; see [Deployment](#deployment).

### Testing
Built-in runners for unit and end-to-end testing; no external framework setup required.
```bash
unit_tests # fast, criterion-based tests
e2e_tests # playwright-powered browser tests
```

### Debugging
Pipeline-aware commands. Halt on individual pipeline steps, step through execution, and inspect the full pipeline context including nested tables and records.
```bash
app_debug # interactive debugger in the TUI
```

### Deployment
Nerak deploys as a standard Docker container. It does not terminate TLS; production deployments place Nerak behind a reverse proxy or load balancer (Nginx, Caddy, AWS ALB) to handle HTTPS.
```bash
app_build # outputs a minimal production Docker image
```

`app_build` runs each module's asset scan once (see [Assets](./REFERENCE.md#assets)) and compiles the results into that module's binary. The production image excludes the file watcher and `/app_info`.

### Observability
Each pipeline step emits OpenTelemetry spans. Logs, traces, errors, and auto-profiling are visualized on the telemetry server at port 4000. No manual instrumentation required.

### Project Management
Ships with integrated infrastructure: source control, issue tracking, wiki, forum, and a project website.

### Built With

| | |
|---|---|
| [C](https://en.cppreference.com/w/c/23) | Language standard |
| [Docker](https://www.docker.com/) | Development environment, production images, stack orchestration |
| [libuv](https://libuv.org/) | Event loops, async I/O, file watching, shared thread pool |
| [libmicrohttpd](https://www.gnu.org/software/libmicrohttpd/) | HTTP server, used by the `http` protocol module |
| [Mustach](https://gitlab.com/jobol/mustach) | Templating and string interpolation, used by `html()`/`markdown()` |
| [Jansson](https://github.com/akheron/jansson) | JSON parsing and generation, used by `json()` and `http_fetch()` |
| [curl](https://curl.se/) | HTTP client, used by `http_fetch()` |
| [Fossil](https://fossil-scm.org/) | Source control, wiki, forum, issue tracker, project site |
| [Fresh](https://getfresh.dev/) | TUI editor |
| [clangd](https://clangd.llvm.org/) | Language server |
| [LLDB](https://lldb.llvm.org/) | Debugger |
| [Criterion](https://github.com/Snaipe/Criterion) | Unit testing |
| [Playwright](https://playwright.dev/) | End-to-end testing |
| [SigNoz](https://signoz.io/) + [OpenTelemetry](https://opentelemetry.io/) | APM, traces, logs, errors, dashboards |

---

## License

Nerak is licensed under the [LGPL](./LICENSE). Your application code can be any license, its a Nerak plugin.
