{{< layout}}
  {{$body}}
    >todos

    `<title`new todo>  `[add`{{url:post:todos}}`title]

    -

    {{^todos_data}}
      no todos
    {{/todos_data}}
    {{#todos_data}}
      {{> todo_item}}
    {{/todos_data}}
  {{/body}}
{{/layout}}
