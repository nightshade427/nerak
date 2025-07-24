[← Back to README](./README.md)

# Reference

* [Context](#context)
* [Assets](#assets)
* [Databases](#databases)
* [Error and Repair Pipelines](#error-and-repair-pipelines)
* [Event Pipelines](#event-pipelines)
* [Task Pipelines](#task-pipelines)
* [Pipeline Steps](#pipeline-steps)
* [Imperative API](#imperative-api)
* [Conditionals](#conditionals)
* [Iteration](#iteration)
* [Modules and Composition](#modules-and-composition)
* [Additional Modules](#additional-modules)
* [Static Files](#static-files)
* [External Dependencies](#external-dependencies)

### Context

Pipelines read from and write to a shared, scoped key-value store that lives for one request. Every step draws inputs from context and writes outputs back.

Three scopes: `input:xxx` for raw request parameters, `error:xxx` for validation/error data, and unprefixed names for app scope (query results, validated inputs, computed values). `input()` promotes values from `input:` to app scope.

### Assets

Every non-`.c` file is an asset. An asset is loaded into context under its name in each module that seeds it (see below), as if `context(name, contents)` had been called there. Common types are Mustache templates (`*.mustache.html`), Markdown (`*.md`), and SQL (`*.sql`); the rule is general.

An asset's name is the filename's basename (the part before the first dot). `get_todos.sql` seeds `get_todos`, `todos.mustache.html` seeds `todos`, `home.md` seeds `home`. Steps read these like any other context value: `html("todos", "todos_s")`, `sqlite_query({"todos_db", "get_todos", "todos_data"})`, and `.migrations`/`.seeds` entries (`{"create_todos_table"}`).

`html()`, `markdown()`, and the engine `*_query()` steps read a string from context by key and interpret it as a template or SQL. The step interprets whatever is under the key when it runs.

`context(name, value)` does the same seeding from a string instead of a file. Useful for content too small to warrant its own file:
```c
context("hello", "<h1>Hello, world!</h1>"); // then html("hello", "hello_s")
context("ping", "select 1");                // then sqlite_query({"db", "ping"})
```

A module is seeded with every asset from its own folder up to the project root. Assets at the root are shared by all modules; assets inside a module's folder are seen only by that module. The scan runs up the tree, not sideways, so a module never sees another module's folder:

```
.
├── layout.mustache.html # → every module
├── 404.mustache.html # → every module
├── partials/
│   └── footer.mustache.html # → every module (partials/ has no config())
└── todos/
    ├── todos.c # the "todos" module
    ├── todos.mustache.html # → todos
    └── detail/
        └── todo.mustache.html # → todos (detail/ has no config())
```

In dev, the scan is live: editing an asset reloads that file into every module holding it, and saving a `.c` file recompiles and reloads that module alone. A production build runs the scan once and compiles each module's assets into its binary; see [Deployment](./README.md#deployment).

### Databases

Each database engine is a module: `#include` its header (e.g. `#include <sqlite.h>`) to activate it, then register one or more databases with `<engine>(...)`. Migrations and seeds are forward-only and index-based: they run in array order, each applied once, with new ones appended to the end. Both are tracked in a `nerak_meta` table.

Multi-tenant databases use `{{interpolation}}` in `.connection`. Connections are pooled with LRU eviction.

**`.database_key`**: identifier referenced by the first value of `query()` steps.
```c
.database_key = "todos_db"
```

**`.connection`**: engine-specific connection string. Supports `{{interpolation}}` for multi-tenancy.
```c
.connection = "file:{{user_id}}_todo.db?mode=rwc"
```

**`.migrations`**: array of SQL migration entries, applied once each in order. Each entry is a context key holding the SQL.
```c
.migrations = {"create_todos_table", "create_comments_table"}
```

**`.seeds`**: array of SQL seed entries, applied once each in order. Each entry is a context key holding the SQL.
```c
.seeds = {"seed_todos"}
```

Combined:
```c
#include <sqlite.h>

sqlite(
  .database_key = "blog_db",
  .connection = "file:{{user_id}}_blog.db?mode=rwc",
  .migrations = {"create_blogs_table", "create_comments_table"},
  .seeds = {"seed_blogs"}
);
```

**Engine include / query / register:** `#include <sqlite.h>` + `sqlite_query()` + `sqlite()`, and likewise `postgres_*`, `mysql_*`, `redis_*`, `duckdb_*`.

### Error and Repair Pipelines

When a step fails, execution halts and Nerak looks for a handler matching the error code. It checks the resource's own `.errors`/`.repairs` first, then the `error()`/`repair()` handlers in that resource's module, and uses the first match. A resource handler overrides the module's for the same code.

Errors are terminal: the handler sends a response and ends the request. Repairs are resumable: they fix the context and resume the original pipeline at the step after the failure. Repairs resolve first; if no matching repair is found, resolution falls through to errors. Unhandled errors fall through to Nerak's internal handler, which looks for a context template named after the error code, otherwise renders the error message as `text/plain` with the error code as the HTTP status, and surfaces in the TUI console and telemetry.

The `error` scope is shared across `input()` failures and `error_set()` calls: `{{error:name}}`, `{{error_code:name}}`, `{{error_message:name}}`. The raw input value remains in `input:name` for re-rendering forms.

**Resource-scoped (`.errors` / `.repairs` fields):**
```c
http("todos", "/todos",
  .post = { ... },
  .errors = {
    {http_not_found, {
      html("404", "not_found_s"),
      http_response("not_found_s")
    }},
    {http_bad_request, {
      html("form", "form_s"),
      http_response("form_s")
    }}
  },
  .repairs = {
    {http_not_authorized, {run(.call = refresh_session_token)}}
  }
);
```

**Module-scoped (`error()` / `repair()` calls):**
```c
config(todos){
  error(http_error, {
    html("5xx", "error_s"),
    http_response("error_s")
  });
  error(http_not_found, {
    html("404", "not_found_s"),
    http_response("not_found_s")
  });
  repair(http_not_authorized, {run(.call = refresh_session_token)});
  // ... resources ...
}
```

Error codes are plain integers; nothing in the core assigns them meaning. Protocol modules supply convenience names for their own status codes. `http.h` defines `http_bad_request` (400), `http_not_authorized` (401), `http_not_found` (404), `http_error` (500). Define your own for domain-specific errors, e.g. `#define err_quota_exceeded 723`.

### Event Pipelines

Internal pub/sub for cross-module communication. The publisher does not know who listens; the subscriber does not know who emits. Adding a subscriber means adding a new module with a `subscribe(...)` call; the publisher does not change. Activate with `#include <pubsub.h>` in any module that publishes or subscribes.

Events are durable. When a publisher is declared, Nerak creates a `nerak_events` database to track delivery. If the process crashes, undelivered events replay on the next boot.

**`publish(event, .with = {...})`**: declares an outbound event contract. First value is the event name; `.with` lists context keys to pass along.
```c
publish("todo_created",
  .with = {"user_id", "title"}
);
```

**`subscribe(event, { steps })`**: registers a subscriber pipeline keyed by event name.
```c
subscribe("todo_created", {
  sqlite_query({"activity_db", "insert_activity"})
});
```

**`emit(event)`**: a pipeline step that fires the event (see [emit](#emit)).
```c
emit("todo_created")
```

**`.errors` / `.repairs`** *(per subscriber)*: each `subscribe(...)` can declare its own handlers, resolved the same way as resource pipelines (the subscriber's own handlers, then its module's). See [Error and Repair Pipelines](#error-and-repair-pipelines).
```c
subscribe("todo_created", {
  sqlite_query({"activity_db", "insert_activity"})
}, .errors = {{http_error, {run(.call = log_subscriber_failure)}}});
```

Combined:
```c
// todos/todos.c: publisher
config(todos){
  publish("todo_created",
    .with = {"user_id", "title"}
  );
  publish("todo_deleted",
    .with = {"user_id", "todo_id"}
  );

  http("todos", "/todos",
    .post = {
      input({"title", not_empty_input}),
      sqlite_query({"todos_db", "insert_todo"}),
      emit("todo_created"),
      http_redirect("todos")
    }
  );
}

// activity/activity.c: subscriber
config(activity){
  subscribe("todo_created", {
    sqlite_query({"activity_db", "insert_created_activity"})
  });
  subscribe("todo_deleted", {
    sqlite_query({"activity_db", "insert_deleted_activity"})
  });
}
```

### Task Pipelines

A task is a named, reusable pipeline, defined inside a module with `task(name, { pipeline }, ...)`. Registration and invocation are separate. `run_task("name")` runs a task inline as a step in the calling pipeline, for reusable pipelines composed into workflows. `dispatch("name")` runs it as a durable background job and returns immediately; requires `#include <dispatch.h>`. `.cron` runs it in the background on a schedule, no caller.

Dispatched tasks are durable: the dispatch module creates the persistent task tables and checkpoints context after each step, so a crash mid-task resumes at the step where it stopped on the next boot.

Any pipeline or task can call `run()`, `run_worker()`, `run_task()`, and `dispatch()` (the last requires `dispatch.h`).

**`.task_key` — task name *(by order)***: task identifier, invoked via `run_task("name")` or `dispatch("name")`.
```c
task("recount", {
  sqlite_query({"db", "recount_todos"})
});
```

**Pipeline *(by order)***: the task's pipeline body, a brace block.
```c
task("name", {
  sqlite_query({...}),
  emit("done"),
  dispatch("followup")
});
```

**`.accepts`**: context keys to pull from the caller into the task.
```c
task("recount_todos", {
  sqlite_query({"db", "recount"})
}, .accepts = {"user_id"});
```

**`.cron`**: standard cron schedule for recurring tasks (no caller required).
```c
task("daily_digest", {
  sqlite_query({"db", "digest"})
}, .cron = "0 8 * * *");
```

**`.errors` / `.repairs`** *(per task)*: each task can declare its own handlers, resolved the same way as resource pipelines (the task's own handlers, then its module's). See [Error and Repair Pipelines](#error-and-repair-pipelines).
```c
task("send_invoice", {
  http_fetch({http_get, "https://api.billing.dev/invoices/{{invoice_id}}", "inv"})
}, .repairs = {{http_not_authorized, {run(.call = refresh_billing_token)}}});
```

Combined:
```c
// on-demand: dispatched via dispatch("recount_todos")
task("recount_todos", {
  sqlite_query({"todos_db", "recount"})
}, .accepts = {"user_id"});

// recurring: runs on schedule, no caller
task("daily_digest", {
  sqlite_query({"todos_db", "digest"}),
  emit("digest_ready")
}, .cron = "0 8 * * *");
```

### Pipeline Steps

Steps are the units of work in a pipeline. Each receives the current context, acts on it, passes control to the next. All steps accept `.if_context_key`/`.unless_context_key` for [conditional execution](#conditionals), and `.map`/`.map_context_key` for concurrent fan-out across rows of a context table (see [Iteration](#iteration)).

* [input](#input)
* [query](#query)
* [join](#join)
* [run](#run)
* [run_worker](#run_worker)
* [emit](#emit)
* [run_task](#run_task)
* [dispatch](#dispatch)
* [nest](#nest)

These core steps come from `nerak.h` and behave identically under every protocol. The remaining steps ship with a module and are documented alongside it. Any step works anywhere a step is accepted, as long as its module is `#include`d.

#### input

Checks request parameters (query string, form body, URL params) against regex patterns. On success, each value is promoted from `input:name` to app scope. On failure, errors land in `error:name` and a `400 Bad Request` triggers the nearest [error/repair pipeline](#error-and-repair-pipelines). All validations in one call complete before the error fires, so all errors are available together for form re-rendering.

Built-in regex macros are defined in `nerak.h`, with `url_input`, `ipv4_input`, `no_html_input`, and `hex_color_input` added by `http.h`. Define your own the same way: `#define zipcode_input "^\\d{5}$"`.

**`.context_key` *(by order)***: name of the parameter to validate.
```c
input({.context_key = "title", .regex = "^\\S+$", .error_message = "required"})
```

**`.regex` *(by order)***: regex pattern, or a built-in validator macro.
```c
input({.context_key = "email", .regex = email_input, .error_message = "bad email"})
```

**`.error_message` *(by order)***: human-readable error shown via `{{error_message:name}}`.
```c
input({.context_key = "age", .regex = integer_input, .error_message = "must be a number"})
```

**`.optional`**: skip validation when the parameter is absent.
```c
input({.context_key = "filter", .regex = "^(active|done)$", .optional = true})
```

**`.default_value`**: default value injected when the parameter is absent.
```c
input({.context_key = "page", .regex = integer_input, .default_value = "1"})
```

Combined:
```c
input(
  {.context_key = "email", .regex = email_input, .error_message = "must be a valid email"},
  {.context_key = "title", .regex = not_empty_input, .error_message = "cannot be empty"},
  {.context_key = "page", .regex = integer_input, .error_message = "must be a number", .default_value = "1"},
  {.context_key = "filter", .regex = "^(active|done)$", .error_message = "must be 'active' or 'done'", .optional = true},
  {.context_key = "username", .regex = user_input, .error_message = "must be alphanumeric"}
)
```

For checks beyond regex (uniqueness, cross-field rules, lookups), pair `input()` with a query and `run()`:
```c
input({.context_key = "username", .regex = user_input, .error_message = "must be alphanumeric"}),
sqlite_query({"users_db", "find_username", "existing"}),
run(^(){
  auto rows = get("existing");
  if (rows && table_length(rows) > 0)
    error_set("username", (error){http_bad_request, "already taken"});
})
```

**Built-in validators:**
- Strings: `not_empty_input`, `alpha_input`, `alphanumeric_input`, `slug_input`, `no_html_input` *(http.h)*
- Numbers: `integer_input`, `positive_integer_input`, `float_input`, `percent_input`
- Identity: `email_input`, `uuid_input`, `user_input`
- Dates & times: `date_input`, `time_input`, `datetime_input`
- Web *(http.h)*: `url_input`, `ipv4_input`, `hex_color_input`
- Codes: `zip_input`, `phone_input`, `cron_input`
- Security: `token_input`, `base64_input`
- Boolean: `bool_input`, `yes_no_input`, `on_off_input`

#### query

Each engine provides its own query step: `sqlite_query()`, `postgres_query()`, `mysql_query()`, `redis_query()`, `duckdb_query()`. All share the same `database_query_config` shape. By order: first value is the database `.database_key`, second is the `.query` context key holding the SQL, third is the `.context_key` for the result table (even single-row results are tables). Multiple items in one step run **concurrently**. Queries use prepared statements; interpolated `{{values}}` are bound, not spliced. For transactions, put `BEGIN`/`COMMIT`/`ROLLBACK` in the SQL.

**`.database_key` *(by order)***: database name, matching the name a `<engine>(...)` was registered with.
```c
sqlite_query({.database_key = "todos_db", .query = "get_todos", .context_key = "todos_data"})
```

**`.query` *(by order)***: context key holding the SQL to run.
```c
sqlite_query({.database_key = "todos_db", .query = "get_todos", .context_key = "todos_data"})
```

**`.context_key` *(by order)***: context key for the result table. Optional; omit when the result isn't needed (e.g. an insert without `RETURNING`).
```c
sqlite_query({.database_key = "todos_db", .query = "create_todo"}) // no result captured
sqlite_query({.database_key = "todos_db", .query = "get_todos", .context_key = "todos_data"}) // result under "todos_data"
```

**`.error_on_empty`**: when true, raise `404 Not Found` if the query affects/returns zero rows. Default false.
```c
sqlite_query({.database_key = "todos_db", .query = "get_todo", .context_key = "todo", .error_on_empty = true})
```

**`.if_context_key` / `.unless_context_key`** *(per item)*: conditionally include or skip individual queries while running the others concurrently.
```c
sqlite_query(
  {.database_key = "db", .query = "get_todos", .context_key = "todos_data"},
  {.database_key = "db", .query = "get_urgent", .context_key = "urgent", .if_context_key = "show_urgent"}
)
```

Combined:
```c
sqlite_query(
  {.database_key = "todos_db", .query = "get_todos", .context_key = "todos_data"},
  {.database_key = "todos_db", .query = "get_todo", .context_key = "todo", .error_on_empty = true},
  {.database_key = "todos_db", .query = "get_urgent", .context_key = "urgent", .if_context_key = "show_urgent"}
)
```

#### join

Nests records from one context table into each matching record of another, like a SQL JOIN in memory. Useful when records come from separate databases or queries. After the step, each outer record gains a new field holding its matched inner records.

**`.parent_context_key`**: outer table whose records receive nested children.
```c
.parent_context_key = "projects"
```

**`.parent_field_key`**: field on the outer table to match against.
```c
.parent_field_key = "id"
```

**`.child_context_key`**: inner table whose records get nested.
```c
.child_context_key = "todos"
```

**`.child_field_key`**: field on the inner table that points at the outer.
```c
.child_field_key = "project_id"
```

**`.parent_child_join_key`**: new field on outer records holding the matched inner records. (defaults to the child table name)
```c
.parent_child_join_key = "todos"
```

Combined:
```c
join(.parent_context_key = "projects", .parent_field_key = "id", .child_context_key = "todos", .child_field_key = "project_id")
```

**Full context example.** Concurrent query → `join()` → `html()`: fetch parent and children from separate queries, render as one nested structure. Blog + comments, single database:

**`blog.mustache.html`**
```html
<article>
  {{#blog}}
    <h1>{{title}}</h1>
    <div>{{content}}</div>
    <h2>Comments</h2>
    <ul>
      {{#comments}}
        <li>{{body}}</li>
      {{/comments}}
    </ul>
  {{/blog}}
</article>
```

```c
http("blog", "/blogs/:id",
  .get = {
    input({"id", integer_input}),

    // Fetch both concurrently: one query() call, two items
    sqlite_query(
      {"blog_db", "get_blog", "blog"},
      {"blog_db", "get_comments", "comments"}
    ),

    // Nest each comment into its matching blog record
    join(.parent_context_key = "blog", .parent_field_key = "id", .child_context_key = "comments", .child_field_key = "blog_id"),

    // Enter {{#blog}} first; after join(), comments lives INSIDE each blog record
    html("blog", "blog_s"),
    http_response("blog_s")
  }
);
```

Context shape at each step:

```
after query(): { blog: [{id, title, content}],
                 comments: [{id, blog_id, body}, ...] } // two sibling tables

after join(): { blog: [{id, title, content,
                         comments: [{id, blog_id, body}, ...]}] } // nested inside blog
```

#### run

`run()` calls a C function or block inline on the reactor, with access to context via the [Imperative API](#imperative-api). It is where business logic and data shaping lives: enriching query results, aggregating, transforming data between steps, setting flags for [conditional](#conditionals) downstream steps. For short, non-blocking work; call `error_set()` to trigger an error/repair pipeline. Use `run_worker()` instead when the body would stall the reactor.

**`.icall` — block *(by order)***: inline block, for short logic specific to this pipeline. Here, attaching each challenger's opponent id so the template can render two voting forms with the right winner/loser pairing:
```c
run(^(){
  auto const t = get("challengers");
  auto const p0 = table_get(t, 0);
  auto const p1 = table_get(t, 1);
  record_set(p0, "opponent_id", record_get(p1, "id"));
  record_set(p1, "opponent_id", record_get(p0, "id"));
})
```

**`.call`**: reference to a named C function, for logic reuse across pipelines.
```c
run(.call = assign_opponents)
```

Inside blocks and `.call` functions, context, memory, errors, tables, and records are manipulated through the [Imperative API](#imperative-api).

#### run_worker

`run_worker()` takes the same block or `.call` as `run()` but is for blocking or CPU-bound work: external C libraries, blocking I/O, heavy computation. The work is dispatched to the shared thread pool, releasing the reactor; the pipeline resumes on the original reactor when the call returns. Use it when the body would stall a request reactor.

**`.icall` — block *(by order)***: inline block, run on the shared thread pool. Here, rendering Markdown through an external C library and freeing its buffer when the request completes:
```c
run_worker(^(){
  auto const raw = third_party_render_md(get("markdown"));
  defer_free(raw);
  set("html", raw);
})
```

**`.call`**: reference to a named C function, run on the shared thread pool.
```c
run_worker(.call = resize_image)
```

#### emit

Triggers an internal pub/sub event. Subscribers in other modules react in their `subscribe()` pipelines, with no direct dependency on the emitter. See [Event Pipelines](#event-pipelines).

**Event name *(by order)***: name of the event to publish.
```c
emit("todo_created")
```

#### run_task

Runs a named task inline as a step in the calling pipeline; control returns to the next step when it finishes. For reusable pipelines composed into workflows. The task must be defined with `task(name, { ... })`. See [Task Pipelines](#task-pipelines).

**Task name *(by order)***: name of a defined task.
```c
run_task("recount_todos")
```

#### dispatch

Enqueues a named task as a durable background job; the calling pipeline continues immediately. Task reactors pick up queued jobs and execute their pipelines. The task is checkpointed after each step, so a crash mid-task resumes where it stopped. Requires `#include <dispatch.h>`, which provides the persistent task tables. The task must be defined with `task(name, { ... })`. See [Task Pipelines](#task-pipelines).

**Task name *(by order)***: name of a defined task.
```c
dispatch("record_daily_stats")
```

#### nest

Groups multiple steps into a single composite step. Useful when applying one `.if_context_key`/`.unless_context_key` to several steps without repeating it.

**`.steps` *(by order)***: array of steps that run as a unit.
```c
nest({sqlite_query({...}), emit("urgent_todo"), html("urgent", "urgent_s"), http_response("urgent_s")})
```

**`.if_context_key` / `.unless_context_key`**: condition applied to the whole group.
```c
nest({sqlite_query({...}), emit("urgent_todo"), html("urgent", "urgent_s"), http_response("urgent_s")},
  .if_context_key = "is_urgent")
```

---

### Imperative API

Functions called from `run()`/`run_worker()` blocks and `.call` functions to read and write context, alloc memory, raise errors, and manipulate tables and records.

* [context](#context-1)
* [memory](#memory)
* [errors](#errors)
* [tables](#tables)
* [records](#records)

#### context

Read, write, and test context keys, and resolve `{{interpolation}}` against the current scope.

**`get(name)`**: returns the value stored under `name`, or `nullptr` if absent. The returned pointer is whatever was stored: a `string` for scalars, a `table` for query and fetch results.
```c
auto todos = get("todos");
```

**`set(name, value)`**: writes `value` to `name`, exposing it to downstream steps and templates.
```c
set("is_urgent", "1");
```

**`has(name)`**: returns true when `name` exists in the current scope.
```c
if (has("user_id")) { ... }
```

**`format(context_key, pattern)`**: resolves `{{name}}` interpolations in `pattern` against the current context and writes the result into `context_key`. Same scopes and helpers as templates.
```c
format("greeting", "Hello, {{user_name}}");
auto greeting = get("greeting");
```

Combined:
```c
run(^(){
  auto rows = get("todos");
  if (table_length(rows) > 5) {
    set("is_urgent", "1");
    format("banner", "{{user_name}} has more than 5 open todos");
  }
})
```

#### memory

Pipeline-arena allocation and deferred cleanup of foreign pointers. Both clear when the request completes.

**`alloc(bytes)`**: returns a buffer from the pipeline arena. Reclaimed automatically on request completion.
```c
auto buf = alloc(256);
```

**`defer_free(ptr)`**: schedules `free()` for a pointer returned by an external library. Runs when the arena is released.
```c
auto out = third_party_alloc(256);
defer_free(out);
```

Combined:
```c
run_worker(^(){
  auto url = alloc(512);
  build_signed_url(url, 512, get("path"));
  set("signed_url", url);

  auto raw = third_party_render_md(get("markdown"));
  defer_free(raw);
  set("html", raw);
})
```

#### errors

Raise field-scoped errors from `run()` to trigger error/repair pipelines. Keys land in the `error:name` scope, visible to templates as `{{error:name}}`, `{{error_code:name}}`, and `{{error_message:name}}`.

**`error_set(name, err)`**: associates an error with `name` and triggers the nearest [error or repair pipeline](#error-and-repair-pipelines).
```c
error_set("token", (error){ http_bad_request, "token has expired" });
```

**`error_get(name)`**: returns the `error` previously set on `name`.
```c
auto e = error_get("token");
```

**`error_has(name)`**: returns true when `name` has an error.
```c
if (error_has("token")) { ... }
```

Combined:
```c
run(^(){
  auto token = get("token");
  if (!token || strlen(token) < 16) {
    error_set("token", (error){
      http_bad_request,
      "token must be at least 16 characters"
    });
  }
})
```

#### tables

Tables are ordered collections of records, the shape `query()` produces and `http_fetch()` parses JSON into. Use these to build derived results.

**`table_new()`**: returns an empty table in the pipeline arena.
```c
auto t = table_new();
```

**`table_length(t)`**: number of records in `t`.
```c
auto n = table_length(get("todos"));
```

**`table_get(t, i)`**: record at index `i`, or `nullptr` if out of range.
```c
auto first = table_get(get("todos"), 0);
```

**`table_add(t, r)`**: appends `r` to `t`.
```c
table_add(t, record_new());
```

**`table_remove(t, r)`**: removes record `r` from `t`.
```c
table_remove(t, r);
```

**`table_remove_at(t, i)`**: removes the record at index `i`.
```c
table_remove_at(t, 0);
```

Combined:
```c
run(^(){
  auto source = get("raw_users");
  auto active = table_new();
  for (int i = 0; i < table_length(source); i++) {
    auto u = table_get(source, i);
    auto status = record_get(u, "status");
    if (status && strcmp(status, "active") == 0) {
      table_add(active, u);
    }
  }
  set("active_users", active);
})
```

#### records

Records are name-value bags, the shape of one row from `query()` or one object from `http_fetch()`. All values are strings; see [Everything is a String](./README.md#everything-is-a-string).

**`record_new()`**: returns an empty record in the pipeline arena.
```c
auto r = record_new();
```

**`record_get(r, name)`**: string value of `name`, or `nullptr` if absent.
```c
auto title = record_get(r, "title");
```

**`record_set(r, name, value)`**: writes `value` to `name` on `r`.
```c
record_set(r, "title", "New title");
```

**`record_remove(r, name)`**: removes `name` from `r`.
```c
record_remove(r, "draft");
```

Combined:
```c
run(^(){
  auto todos = get("todos");
  for (int i = 0; i < table_length(todos); i++) {
    auto t = table_get(todos, i);
    auto title = record_get(t, "title");
    if (title && strlen(title) > 40) {
      record_set(t, "is_long", "1");
    }
  }
})
```

### Conditionals

Every step accepts `.if_context_key` and `.unless_context_key`, naming a context variable. They work for any context value: validated inputs, query results, framework flags like `is_htmx`, or flags set from `run()`.

**`.if_context_key`**: context key. Step runs only when the value is present.
```c
html("fragment", "frag_s", .if_context_key = "is_htmx")
```

**`.unless_context_key`**: context key. Step runs only when the value is absent.
```c
html("full_page", "page_s", .unless_context_key = "is_htmx")
```

For multi-state branching, set context flags from `run()`, then key downstream steps off them:

```c
run(.call = classify_todo),
html("urgent_confirmation", "urgent_s", .if_context_key = "is_urgent"),
http_response("urgent_s", .if_context_key = "is_urgent"),
html("standard_confirmation", "standard_s", .unless_context_key = "is_urgent"),
http_response("standard_s", .unless_context_key = "is_urgent")
```

### Iteration

`.map` and `.map_context_key` run a step once per row of a context table, all rows **concurrently**, like multiple items in `query()` or `http_fetch()`. With a `.context_key`, results are collected into a table aligned with the input, one entry per row. They differ in how each row reaches the step body: `.map` puts the row's fields in scope as bare `{{interpolations}}`; `.map_context_key` binds the row as a single-row table under a named key.

**`.map`**: name of a context table to iterate over. The row's fields land in scope as bare interpolations.
```c
// One request per row in `users`, all concurrent.
// Each row's `id` fills the URL; responses collected into `profiles`, aligned with `users`.
http_fetch({http_get, "https://api.users.dev/{{id}}", "profiles", .map = "users"})
```

**`.map_context_key`**: context key under which the current row is exposed as a single-row table. Pairs with `.map`.
```c
// Render the `todo` template once per row of `todos`.
// `.map_context_key = "todo_d"` presents the current row as the single-row table `todo_d`
// Rendered fragments are collected into `todo_s`, aligned with `todos`.
html("todo", "todo_s", .map = "todos", .map_context_key = "todo_d")
```

### Modules and Composition

A module is declared with `config(name)` in a `name.c` file, usually inside a matching `name/` folder that holds its assets. A module owns its own resources, databases, migrations, tasks, and event contracts. Nerak scans the project for `config(...)` declarations and loads every module it finds.

A module is seeded with the assets in its folder and every asset up to the project root, at startup. See [Assets](#assets) and [Context](#context).

**`config(name)`**: declares a module.
```c
// todos/todos.c
config(todos){
  // resources, databases, tasks, subscribers ...
}
```

**`error(...)` / `repair(...)`**: module-scoped error and repair handlers (see [Error and Repair Pipelines](#error-and-repair-pipelines)). They cover resources in the same module; a resource's own `.errors`/`.repairs` override them for the same code.

**Pipeline composition.** A request runs the resource's `.all` steps first, then the verb pipeline. Cross-cutting setup — session loading, tenant resolution, access checks — goes in `.all`, where it runs ahead of every verb on that resource.

```c
// todos/todos.c: session loads, resources require login
#include <nerak.h>
#include <http.h>
#include <sqlite.h>
#include <cookie_auth.h>

config(todos){
  http("todos", "/todos",
    .all = {cookie_logged_in(), cookie_session()},
    .get = {
      sqlite_query({"todos_db", "get_todos", "todos_data"}),
      html("todos", "todos_s"),
      http_response("todos_s")
    },
    .post = {
      input({"title", not_empty_input}),
      sqlite_query({"todos_db", "create_todo"}),
      http_redirect("todos")
    }
  );

  http("todo", "/todos/:id",
    .all = {cookie_logged_in(), cookie_session(), input({"id", positive_integer_input})},
    .delete = {
      sqlite_query({"todos_db", "delete_todo", .error_on_empty = true}),
      http_redirect("todos")
    }
  );
}
```

For `DELETE /todos/5` the executed order is the resource `.all` steps in order — `cookie_logged_in()`, `cookie_session()`, `input({"id", ...})` — then the verb pipeline: `sqlite_query({"delete_todo", .error_on_empty = true})`, `http_redirect("todos")`.

**Complete module file.** A `blogs/blogs.c`:

```c
#include <nerak.h>
#include <http.h>
#include <sqlite.h>

config(blogs){
  sqlite(
    "blog_db",
    "file:blogs.db?mode=rwc",
    {"create_blogs_table", "create_comments_table"}
  );

  http("blog", "/blogs/:id",
    .get = {...}
  );
}
```

Place the folder in the project; Nerak loads it on the next scan.

A typical project layout:
```
├── app.c # app module (config(app){ ... })
├── todos/ # todos module
│   ├── todos.c # config(todos){ ... }
│   ├── todos.mustache.html # → todos
│   ├── create_todos_table.sql # → todos
│   └── get_todos.sql # → todos
├── activity/ # activity module
│   └── activity.c # config(activity){ ... }
├── public/ # static files, served directly
│   └── favicon.png
├── layout.mustache.html # → every module
├── 404.mustache.html # → every module
└── 5xx.mustache.html # → every module
```

---

### Additional Modules

Bundled modules. Activate each by `#include`ing its header.

* [Protocols](#protocols)
  * [http](#http)
  * [rns](#rns)
* [htmx](#htmx)
* [datastar](#datastar)
* [tailwind](#tailwind)
* [daisyui](#daisyui)
* [cookie_auth](#cookie_auth)
* [database engines](#database-engines)

#### Protocols

A protocol module registers itself with Nerak and spins up its own reactor type: a dedicated pool of cores handling that protocol's traffic, alongside the task reactors and shared thread pool. `http` is the reference implementation and the one most apps include; `rns` serves Reticulum destinations from the same pipeline model. A new protocol defines its own pipelines, request/response handling, and serialization format, then registers with `config(name)`. NomadNet, Gemini, and Finger fit the same model. An app can run several protocols at once, sharing databases, tasks, and events between them.

##### http

Activate with `#include <http.h>`. Provides the resource/pipeline model (`http()`), HTTP verbs, fetch, response, headers/cookies, redirect/reroute, SSE, and HTML/Markdown/JSON rendering. Mustache and MDM (Mustache + Markdown) templating live here as `html()` and `markdown()`; see [Templates](#templates).

###### HTTP Pipelines

Nerak is resource-based, not route-based. Each `http(...)` defines a named resource and URL with HTTP verb pipelines. `{{url:verb:name}}`, `http_redirect()`, and `http_reroute()` all identify the target by resource name; `:params` are read from the current scope by matching key names. Path specificity is automatic: exact matches (`/todos/active`) take priority over parameterized matches (`/todos/:id`) regardless of definition order.

Clients select a verb via the request method, or by passing `http_method` as a query/form parameter. This lets HTML forms (limited to GET/POST) reach any verb, and gives SSE a connection path: `/todos?http_method=events`. Templates never write that parameter by hand: `{{url:verb:name}}` emits it, along with a CSRF token for state-changing verbs (see [Templates](#templates)).

**Resource name *(by order)***: identifier used by `{{url:verb:name}}`, `http_redirect()`, and `http_reroute()`.
```c
http("todos", "/todos", .get = { ... });
```

**URL pattern *(by order)***: URL pattern. Supports `:params`.
```c
http("todo", "/todos/:id", .get = { ... });
```

**`.all`**: shared steps that run before every verb pipeline on the resource.
```c
http("todo", "/todos/:id",
  .all = { input({"id", integer_input, "must be a number"}) },
  .get = { ... },
  .delete = { ... }
);
```

**`.mime`**: default response content type. Values: `mime_html`, `mime_text`, `mime_event_stream`, `mime_json`, `mime_javascript` (default `mime_html`).
```c
http("feed", "/feed.json", .mime = mime_json, .get = { ... });
```

**`.get` `.post` `.put` `.patch` `.delete`**: verb pipelines: ordered arrays of steps that transform a request into a response.
```c
http("todos", "/todos",
  .get = {
    sqlite_query({"db", "get_todos", "todos_data"}),
    html("todos", "todos_s"),
    http_response("todos_s")
  },
  .post = {
    input({"title", not_empty_input}),
    http_redirect("todos")
  }
);
```

**`.sse`**: persistent SSE channel. The first value is the channel name (supports `{{interpolation}}`); any remaining steps run on connect. Clients reach this pipeline with the `events` verb, which `{{url:events:name}}` emits as `?http_method=events`.
```c
http("todos", "/todos",
  .sse = {"todos:{{user_id}}",
    sqlite_query({"db", "get_todos", "todos_data"}),
    http_sse(.event = "initial", .data = {"{{todos_data}}"})
  }
);
```

**`.errors` / `.repairs`**: resource-scoped error and repair pipelines. See [Error and Repair Pipelines](#error-and-repair-pipelines).
```c
http("todos", "/todos",
  .post = { ... },
  .errors = {{http_bad_request, {
    html("form", "form_s"),
    http_response("form_s")
  }}}
);
```

Combined:
```c
http("todo", "/todos/:id",
  .all = {input({"id", positive_integer_input, "must be a number"})},
  .get = {
    sqlite_query({"todos_db", "get_todo", "todo", .error_on_empty = true}),
    html("todo", "todo_s"),
    http_response("todo_s")
  },
  .patch = {
    input({"title", not_empty_input, "required"}),
    sqlite_query({"todos_db", "update_todo"}),
    http_redirect("todo")
  },
  .delete = {
    sqlite_query({"todos_db", "delete_todo"}),
    http_redirect("todos")
  },
  .sse = {"todo:{{id}}", http_sse(.event = "ready")},
  .errors = {{http_not_found, {
    html("404", "not_found_s"),
    http_response("not_found_s")
  }}}
);
```

###### Templates

Nerak uses Mustache and MDM (Mustache + Markdown) templates, rendered by the `html()` and `markdown()` steps respectively. The full Mustache base spec is supported except dot notation: `{{a.b}}` does not work; use `{{#a}}{{b}}{{/a}}`.

Base-spec features:
- **Interpolation**: `{{name}}` (HTML-escaped), `{{{name}}}` or `{{&name}}` (unescaped).
- **Sections**: `{{#name}}...{{/name}}` renders when truthy and iterates over arrays.
- **Inverted sections**: `{{^name}}...{{/name}}` renders when falsy or empty.
- **Comments**: `{{! ignored }}`.
- **Set delimiters**: `{{=<% %>=}}`.
- **Partials**: `{{> name }}` inlines the asset `name`, rendered against the current scope.
- **Layout inheritance**: `{{< parent}}{{$block}}override{{/block}}{{/parent}}` renders `parent` with each `{{$block}}default{{/block}}` block replaced by the override. Any asset declaring blocks can be a parent.

Built-in helpers use `{{helper:args}}` syntax. Arguments are colon-separated, in order; each can be a literal or a context key.

**`{{precision:field:N}}`**: format a numeric value with N decimal places.
```html
<p>Total: ${{precision:total:2}}</p>
```

**`{{input:field}}`**: raw, unvalidated request parameter from the `input` scope. Used to repopulate form fields after a validation error.
```html
<input name='title' value='{{input:title}}'>
```

**`{{error:field}}`**: truthy when `field` has an error. Used as a Mustache section to conditionally render markup.
```html
{{#error:title}}
  <span class='error'>invalid</span>
{{/error:title}}
```

**`{{error_message:field}}`**: human-readable message for a field error, from `input()`'s message or from `error_set()`.
```html
<span>{{error_message:title}}</span>
```

**`{{error_code:field}}`**: HTTP status code associated with a field error (e.g. `400`, `404`).
```html
<p>Code: {{error_code:title}}</p>
```

**`{{url:verb:name}}`**: resolve a resource to its URL for a given verb. The verb comes first and is always required, then the resource name. `:params` in the URL pattern are read from the current scope by matching key names, so the same helper works per-row inside a section.

For a non-GET verb the helper appends the query string the router needs: `http_method=<verb>`, plus a CSRF token when the verb is state-changing (everything except `get`). The token is a random hash, also set on an httponly/secure/samesite cookie; state-changing requests are verified against it. Nothing else is required to reach a non-GET verb — no hidden inputs, no separate CSRF helper.

```html
<a href='{{url:get:todos}}'>All</a> <!-- /todos -->

{{#todos_data}}
  <a href='{{url:get:todo}}'>{{title}}</a> <!-- /todos/:id, filled per row -->
{{/todos_data}}

<!-- state-changing verbs carry the method and token in the query string -->
{{#todo}}
  <a href='{{url:delete:todo}}'>Delete</a>
{{/todo}}
<!-- /todos/5?http_method=delete&csrf=1f3a... -->

<form method='post' action='{{url:post:todos}}'>
  <input name='title'>
  <button>Add</button>
</form>
<!-- action="/todos?http_method=post&csrf=1f3a..." -->

<!-- multi-param patterns fill every slot from scope -->
<a href='{{url:get:org_todo}}'>Open</a>
<!-- /orgs/{{org}}/todos/{{id}} -->
```

**`{{asset:filename}}`**: resolve a file in `public/` to a cache-busted URL (content checksum + immutable cache headers). See [Static Files](#static-files).
```html
<link rel='stylesheet' href='{{asset:styles.css}}'>
```

---

###### fetch

Makes one or more HTTP requests and stores responses in context. JSON parses into tables and records (nested tables for nested JSON); plain-text responses are stored as strings. Like `query()`, multiple items in one step run **concurrently**.

**`.url`**: request URL; supports `{{interpolation}}`.
```c
http_fetch({.url = "https://api.weather.dev/forecast?city={{city}}", .context_key = "w"})
```

**`.context_key`**: context key for the response.
```c
http_fetch({.url = "https://api.weather.dev/now", .context_key = "weather"})
```

**`.method`**: HTTP method. Defaults to `http_get`. Values: `http_get`, `http_post`, `http_put`, `http_patch`, `http_delete`, `http_events`.
```c
http_fetch({.url = "https://api.dev/charge", .context_key = "r", .method = http_post})
```

**`.headers`**: array of name/value pairs.
```c
http_fetch({.url = "https://api.dev/me", .context_key = "r", .headers = {{"Authorization", "Bearer {{token}}"}}})
```

**`.json_table_key`** / **`.json`**: JSON request body. `.json_table_key` names a context key whose value is serialized; `.json` is a literal JSON string (supports `{{interpolation}}`).
```c
http_fetch({.url = "https://api.dev/charge", .context_key = "receipt", .method = http_post, .json_table_key = "order"})
```

**`.text`**: context key sent as the plain-text request body.
```c
http_fetch({.url = "https://api.dev/log", .context_key = "r", .method = http_post, .text = "raw_body"})
```

**`.if_context_key` / `.unless_context_key`** *(per item)*: conditionally include or skip individual requests while running others concurrently.
```c
http_fetch(
  {.url = "https://api.weather.dev/now", .context_key = "weather"},
  {.url = "https://api.quotes.dev/random", .context_key = "quote", .if_context_key = "show_quote"}
)
```

Combined, single request:
```c
http_fetch({.url = "https://api.payments.dev/charge",
  .context_key = "receipt",
  .method = http_post,
  .json_table_key = "order",
  .headers = {
    {"Authorization", "Bearer {{api_key}}"},
    {"Idempotency-Key", "{{order_id}}"}
  }
})
```

Combined, concurrent fan-out:
```c
http_fetch(
  {.url = "https://api.weather.dev/now?city={{city}}", .context_key = "weather"},
  {.url = "https://api.news.dev/headlines?topic={{topic}}", .context_key = "news"},
  {.url = "https://api.quotes.dev/random", .context_key = "quote"}
)
```

###### sse

Pushes a Server-Sent Event. With `.channel`, the event broadcasts to all clients on that channel. Without it, the event returns to the requesting client. See [HTTP Pipelines](#http-pipelines).

**`.channel` *(by order)***: channel to broadcast on; supports `{{interpolation}}`.
```c
http_sse(.channel = "todos:{{user_id}}", .event = "new_todo", .data = {"{{todo}}"})
```

**`.event`**: SSE `event:` line value.
```c
http_sse(.event = "ping")
```

**`.data`**: array of strings, one per SSE `data:` line (multi-line data).
```c
http_sse(.event = "msg", .data = {"line one", "line two"})
```

**`.comment`**: SSE `:` comment line value, useful for keep-alives.
```c
http_sse(.comment = "keep-alive")
```

Combined:
```c
http_sse(.channel = "todos:{{user_id}}",
  .event = "todo_updated",
  .data = {"id: {{todo_id}}", "title: {{title}}"},
  .comment = "broadcast at {{timestamp}}"
)
```

###### render

Renders a template into the pipeline context. `html()` renders Mustache; `markdown()` renders Markdown-with-Mustache; `json()` renders JSON. All take the same `render_config`.

**`.template_key` *(by order)***: context key holding the template string to render.
```c
html(.template_key = "todos", .context_key = "todos_s")
```

**`.context_key` *(by order)***: context key to write the rendered output to.
```c
html(.template_key = "todos", .context_key = "todos_s")
```

JSON:
```c
json(.template_key = "todos", .context_key = "todos_j")
```

Markdown-with-Mustache:
```c
context("welcome", "# Welcome, {{user_name}}");
markdown(.template_key = "welcome", .context_key = "welcome_s")
```

###### respond

Sends a pipeline context value as the HTTP response.

**`.context_key` *(by order)***: key of the rendered content to send.
```c
http_response(.context_key = "todos_s")
```

**`.status`**: HTTP response status (default `http_ok`). Values: `http_ok` (200), `http_created` (201), `http_redirected` (302), `http_bad_request` (400), `http_not_authorized` (401), `http_not_found` (404), `http_error` (500).
```c
http_response(.context_key = "not_found_s", .status = http_not_found)
```

**`.mime`**: override the response content type. Values: `mime_html`, `mime_text`, `mime_event_stream`, `mime_json`, `mime_javascript`.
```c
http_response(.context_key = "plain_s", .mime = mime_text)
```

Combined:
```c
html("not_found", "not_found_s"),
http_response(.context_key = "not_found_s", .status = http_not_found)
```

###### headers and cookies

Set HTTP response headers and cookies declaratively. Each call sets one `{name, value}` pair; values support `{{interpolation}}`. Repeat the step for each additional header or cookie.

**`.key` / `.value` *(by order)***: the header or cookie name and its value.
```c
http_headers({"X-Request-Id", "{{request_id}}"})
```
```c
http_cookies({"session", "{{session_id}}"})
```

Combined:
```c
http_headers({"X-Request-Id", "{{request_id}}"}),
http_headers({"Cache-Control", "no-store"}),
http_cookies({"session", "{{session_id}}"}),
http_cookies({"theme", "{{theme}}"})
```

###### redirect and reroute

`http_redirect()` returns a 302 to the client, causing the browser to navigate. `http_reroute()` re-enters the router server-side, executing another resource's pipeline within the same request. Both take only the target resource name. `:params` in the target's URL pattern are read from the current context by matching key names.

**Resource name *(by order)***: target resource name. Required `:params` are read from context by name.
```c
http_redirect("todos") // 302 to /todos
http_redirect("todo") // 302 to /todos/{{id}}, id read from context
http_redirect("org_todo") // 302 to /orgs/{{org}}/todos/{{id}}, org and id read from context
http_reroute("todo") // run that pipeline in-process, id read from context
```

##### rns

Activate with `#include <rns.h>`. Provides the resource/pipeline model for [Reticulum](https://reticulum.network/) destinations: `rns()` declares a resource the same way `http()` does, with the same verb pipelines and the same `.all`/`.errors`/`.repairs` fields. There is no `.mime` and no `.sse`; Reticulum has no MIME negotiation, and delivery is message-based.

Pages are written in Micron, Reticulum's markup, rendered by `micron()`.

**Resource name *(by order)***: identifier used by `rns_reroute()`.
```c
rns("todos", "/todos", .get = { ... });
```

**URL pattern *(by order)***: destination path. Supports `:params`.
```c
rns("todo", "/todos/:id", .get = { ... });
```

**`.all`**: shared steps that run before every verb pipeline on the resource.
```c
rns("todo", "/todos/:id",
  .all = {rns_identity(), input({"id", integer_input, "must be a number"})},
  .get = { ... }
);
```

**`.get` `.post` `.put` `.patch` `.delete`**: verb pipelines, identical in shape to their `http()` counterparts.

**`.errors` / `.repairs`**: resource-scoped error and repair pipelines. See [Error and Repair Pipelines](#error-and-repair-pipelines).

**`rns_identity()`**: loads the requesting peer's Reticulum identity hash into context, the RNS equivalent of a session lookup. Put it in `.all` on resources that need to know who is calling.

**`micron(template_key, context_key)`**: renders a Micron template from context into context, the way `html()` renders Mustache. Micron assets are ordinary files (`todos.mustache.mu` seeds `todos`), so Mustache interpolation and sections work inside them.

**`rns_response(context_key)`**: sends a context value back to the requesting peer.

**`rns_reroute(name)`**: re-enters the router in-process and runs another resource's pipeline, like `http_reroute()`. `:params` are read from context by matching key names.

Combined:
```c
#include <nerak.h>
#include <rns.h>
#include <sqlite.h>

config(todos_rns){
  rns("todos", "/todos",
    .all = {rns_identity()},
    .get = {
      sqlite_query({"todos_db", "get_todos", "todos_data", .error_on_empty = true}),
      micron("todos", "todos_s"),
      rns_response("todos_s")
    },
    .post = {
      input({"title", not_empty_input, "cannot be empty"}),
      sqlite_query({"todos_db", "create_todo"}),
      rns_reroute("todos")
    },
    .errors = {
      {404, {micron("no_todos", "no_todos_s"), rns_response("no_todos_s")}}
    }
  );
}
```

The same module can serve both protocols: declare `http(...)` and `rns(...)` resources side by side, sharing queries, tasks, and event subscriptions, and render Mustache for one and Micron for the other.

#### htmx

Activate with `#include <htmx.h>`. Serves the htmx runtime as the `{{> htmx }}` partial, and sets the `is_htmx` context flag on requests carrying the `HX-Request` header. Pair the flag with `.if_context_key`/`.unless_context_key` to return a fragment to htmx and a full page to a direct visit, or use `hx-boost` to upgrade ordinary links and forms into AJAX swaps.

```c
#include <nerak.h>
#include <http.h>
#include <htmx.h>

config(todos){
  http("todos", "/todos",
    .get = {
      sqlite_query({"todos_db", "get_todos", "todos_data"}),
      html("todos_fragment", "frag_s", .if_context_key = "is_htmx"),
      http_response("frag_s", .if_context_key = "is_htmx"),
      html("todos_page", "page_s", .unless_context_key = "is_htmx"),
      http_response("page_s", .unless_context_key = "is_htmx")
    }
  );
}
```

Include the runtime once in the page `<head>`:
```html
<head>{{> htmx }}</head>
<body hx-boost='true'>...</body>
```

#### datastar

Activate with `#include <datastar.h>`. Serves the Datastar runtime as the `{{> datastar }}` partial and provides `datastar()` for pushing reactive fragment and signal patches over an SSE channel. A page opens an SSE connection (a resource `.sse` channel); pipelines push patches to that channel, and Datastar applies them in the DOM.

`datastar()` patches a context value into the page by target (element id or CSS selector). The first value is the channel (supports `{{interpolation}}`).

**`.channel` *(by order)***: channel to push to.
```c
html("todo_row", "todo_row_s"),
datastar(.channel = "todos:{{user_id}}", .target = "#todo-list", .mode = append_mode, .elements = "todo_row_s")
```

**`.target`**: element id or CSS selector for the element to patch; supports `{{interpolation}}`.
```c
.target = "#todo-{{id}}"
```

**`.mode`**: how the rendered fragment is applied to the target (a `datastar_mode`).
```c
.mode = replace_mode
```

**`.elements`**: context key holding the rendered HTML fragment to patch in. Not required for `remove_mode`.
```c
.elements = "todo_row_s"
```

**`.signals`**: context key holding signal state to merge into the client store.
```c
.signals = "ui_state"
```

**`.javascript`**: JavaScript to execute on the client.
```c
.javascript = "window.scrollTo(0, document.body.scrollHeight)"
```

**Patch modes (`datastar_mode`):** `outer_mode`, `inner_mode`, `replace_mode`, `prepend_mode`, `append_mode`, `before_mode`, `after_mode`, `remove_mode`.

Worked example: a POST inserts a row, returns it with `RETURNING`, appends it to every connected client's list.

```c
http("todos", "/todos",
  // Each browser opens this channel and listens for patches.
  .sse = {"todos:{{user_id}}"},

  .post = {
    input({"title", not_empty_input}),
    // RETURNING gives the new row back; capture it under "todo".
    sqlite_query({"todos_db", "insert_todo", "todo", .error_on_empty = true}),
    // Render the new row, then patch it into the list for everyone on the channel.
    html("todo_row", "todo_row_s"),
    datastar(.channel = "todos:{{user_id}}",
      .target = "#todo-list",
      .mode = append_mode,
      .elements = "todo_row_s"
    )
  }
);
```

Removing an element needs only a selector:
```c
datastar(.channel = "todos:{{user_id}}", .target = "#todo-{{id}}", .mode = remove_mode)
```

Datastar sets a context flag on requests it originates, usable with `.if_context_key`.

Include the runtime once in the page `<head>`:
```html
<head>{{> datastar }}</head>
```

#### tailwind

Activate with `#include <tailwind.h>`. Compiles Tailwind utility classes used across the project's templates and serves the stylesheet as the `{{> tailwind }}` partial. Use Tailwind classes directly in templates; no build step or config file required.

```html
<head>{{> tailwind }}</head>
<body class='bg-gray-950 text-white min-h-screen'>
  <h1 class='text-3xl font-bold text-center mb-8'>Vote for which is roundest</h1>
</body>
```

#### daisyui

Activate with `#include <daisyui.h>`. Compiles DaisyUI classes used across the project's templates and serves the stylesheet as the `{{> daisyui }}` partial. Use DaisyUI classes directly in templates; no build step or config file required.

```html
<head>{{> daisyui }}</head>
<body>
  <button class="btn">Click Me</button>
</body>
```

#### cookie_auth

Activate with `#include <cookie_auth.h>`. Cookie-based authentication as pipeline steps. The module is self-contained (per [SCS](./README.md#philosophy)): including the header mounts the `/login`, `/logout`, and `/signup` resources and ships the login page, so `{{url:get:login}}` and `{{url:post:logout}}` resolve with no app code. `cookie_logged_in()` and `cookie_session()` are independent; use either alone or both together.

**`cookie_logged_in()`**: checks the request for the `user_id` cookie. If it is missing, redirects to the login page, which authenticates the visitor, sets the `user_id` cookie, and redirects back. Put it in `.all` on resources that require a signed-in user. It checks for the cookie but does not load the user, so pair it with `cookie_session()` (`cookie_logged_in()` first) when the pipeline also needs the `user` record.
```c
http("todos", "/todos", .all = {cookie_logged_in(), cookie_session()}, .get = { ... });
```

**`cookie_session()`**: reads the `user_id` cookie and loads that user's record into context as `user`. Put it on any resource whose pipelines or templates read the current user. On a public page such as an about or landing page, use it alone with no gate: the header can then show the user when signed in, a sign-in link otherwise.
```c
http("about", "/about", .all = {cookie_session()}, .get = { ... });
```
```html
{{#user}}Signed in as {{short_name}} · <a href='{{url:post:logout}}'>log out</a>{{/user}}
{{^user}}<a href='{{url:get:login}}'>sign in</a>{{/user}}
```

**`cookie_login()` / `cookie_logout()` / `cookie_signup()`**: the actions behind the module's built-in resources. Use them directly only to mount your own auth resources (a custom URL, template, or extra steps) in place of the defaults.
```c
http("login", "/login",
  .get = {
    html("login", "login_s"),
    http_response("login_s")
  },
  .post = {cookie_login()}
);
http("logout", "/logout", .post = {cookie_logout()});
http("signup", "/signup",
  .get = {
    html("signup", "signup_s"),
    http_response("signup_s")
  },
  .post = {cookie_signup()}
);
```

Combined: gate the resources that need auth, then read user fields in that module's templates.
```c
// todos/todos.c
#include <nerak.h>
#include <http.h>
#include <cookie_auth.h>

config(todos){
  http("todos", "/todos",
    .all = {cookie_logged_in(), cookie_session()},
    .get = {
      html("todos", "todos_s"),
      http_response("todos_s")
    }
  );
}
```
```html
<!-- once cookie_session() has loaded the user, templates can read it -->
{{#user}}
  <span>Hi, {{short_name}}</span>
{{/user}}

<!-- the logout link needs the post verb; the url helper carries the method and CSRF token -->
<a href='{{url:post:logout}}'>Log out</a>
```

#### database engines

Each engine is its own module: `#include` its header, then use `<engine>(...)` to register and `<engine>_query({...})` as a pipeline step. They share `database_config` and `database_query_config` from [Databases](#databases) and [query](#query); only `.connection` is engine-specific.

```c
#include <sqlite.h> // sqlite("...", "file:app.db?mode=rwc", ...); sqlite_query({...});
#include <postgres.h> // postgres("...", "postgres://...", ...); postgres_query({...});
#include <mysql.h> // mysql("...", "mysql://...", ...); mysql_query({...});
#include <redis.h> // redis("...", "redis://...", ...); redis_query({...});
#include <duckdb.h> // duckdb("...", "duckdb:analytics.db", ...); duckdb_query({...});
```

### Static Files

Files in `public/` are served directly. Reference them in templates with `{{asset:filename}}`, which resolves to a content-checksummed, cache-busted URL with immutable cache headers. Updates invalidate caches automatically; unchanged files cache forever.

```
public/
├── favicon.png
├── styles.css
└── logo.svg
```

```html
<link rel='icon' href='{{asset:favicon.png}}'>
<link rel='stylesheet' href='{{asset:styles.css}}'>
<img src='{{asset:logo.svg}}' alt='Logo'>
```

This differs from assets like SQL and HTML templates, which are embedded from files anywhere in the project and seeded into context for steps to read by key (see [Assets](#assets) and [Context](#context)). `public/` holds opaque files served to the browser.

### External Dependencies

Drop third-party C libraries into `/vendor`; Nerak compiles and links them with the app. Call into them from `run()`/`run_worker()` steps. Memory returned by a library that must be freed manually is registered with `defer_free()` so it is reclaimed when the request completes (see [memory](#memory)).

```
vendor/
└── cmark/
    ├── cmark.c
    └── cmark.h
```

```c
#include <nerak.h>
#include "vendor/cmark/cmark.h"

void render_markdown(){
  run_worker(^(){
    auto md = get("markdown");
    auto html = cmark_markdown_to_html(md, strlen(md), 0);
    defer_free(html); // library-owned pointer
    set("html_content", html);
  });
}
```

For dependencies that aren't plain source (system packages, build tooling), provide a custom `Dockerfile`. Nerak builds from it instead of the default image.
