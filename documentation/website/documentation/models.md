---
id: models
title: Models
sidebar_label: Models
---

import FbModels from './fb/models.md';
import MultiSourceSinkRule from './fb/multi_source_sink_rules.md';

The main way to configure the analysis is through defining models for methods.

## Models

A model is an abstract representation of how data flows through a method.

A model essentialy consists of:
* [Sources](#sources): a set of sources that the method produces or receives on parameters;
* [Sinks](#sinks): a set of sinks on the method;
* [Propagation](#propagation): a description of how the method propagates taint coming into it (e.g, the first parameter updates the second, the second parameter updates the return value, etc.);
* [Attach to Sources](#attach-to-sources): a set of features/breadcrumbs to add on an any sources flowing out of the method;
* [Attach to Sinks](#attach-to-sinks): a set of features/breadcrumbs to add on sinks of a given parameter;
* [Attach to Propagations](#attach-to-propagations): a set of features/breadcrumbs to add on propagations for a given parameter or return value;
* [Add Features to Arguments](#add-features-to-arguments): a set of features/breadcrumbs to add on any taint that might flow in a given parameter;
* [Sanitizers](#sanitizers): specifications of taint flows to stop;
* [Modes](#modes): a set of flags describing specific behaviors (see below).

Models can be specified in JSON. For example to mark the string parameter to our `Logger.log` function as a sink we can specify it as
```json
{
  "find": "methods",
  "where": [
    {
      "constraint": "signature",
      "pattern": "Lcom/example/Logger;\\.log:\\(Ljava/lang/String;\\)V"
    }
  ],
  "model": {
    "sinks" : [
      {
        "kind" : "Logging",
        "port" : "Argument(1)"
      }
    ]
  }
}
```

Note that the naming of methods follow the [Dalvik's bytecode format](#method-name-format).

### Method name format

The format used for method names is:

`<className>.<methodName>:(<parameterType1><parameterType2>)<returnType>`

Example: `Landroidx/fragment/app/Fragment;.startActivity:(Landroid/content/Intent;)V`


For the parameters and return types use the following table to pick the correct one (please refer to [JVM doc](https://docs.oracle.com/javase/specs/jvms/se7/html/jvms-4.html#jvms-4.3.2-200) for more details)

* V - void
* Z - boolean
* B - byte
* S - short
* C - char
* I - int
* J - long (64 bits)
* F - float
* D - double (64 bits)

Classes take the form `Lpackage/name/ClassName;` - where the leading `L` indicates that it is a class type, `package/name/` is the package that the class is in.

> **NOTE:** Instance (i.e, non-static) method parameters are indexed starting from 1! The 0th parameter is the `this` parameter in dalvik byte-code. For static method parameter, indices start from 0.

### Access path format

An access path describes the symbolic location of a taint. This is commonly used to indicate where a source or a sink originates from.

An access path is composed of a root and a path.

The root is either:
* `Return`, representing the returned value;
* `Argument(x)` (where `x` is an integer), representing the parameter number `x`;

The path is a (possibly empty) list of field names.

Access paths are encoded as strings, where elements are separated by a dot: `<root>.<field1>.<field2>`

Examples:
* `Argument(1).name` correspond to the field `name` of the second parameter;
* `Return.x` correpsond to the field `x` of the returned value;
* `Return` correspond to the returned value.

### Kinds

A source has a **kind** that describes its content (e.g, user input, file system, etc).
A sink also has a **kind** that describes the operation the method performs (e.g, execute a command, read a file, etc.).
Kinds can be arbitrary strings (e.g, `UserInput`). We usually avoid whitespaces.

### Rules

A rule describes flows that we want to catch (e.g, user input flowing into command execution).
A rule is made of a set of source kinds, a set of sink kinds, a name, a code and a description.

Here is an example of a rule in JSON:
```json
{
  "name": "User input flows into code execution (RCE)",
  "code": 1,
  "description": "Values from user-controlled source may eventually flow into code execution",
  "sources": [
    "UserInput",
  ],
  "sinks": [
    "CodeExecution",
  ]
}
```
Rules used by Mariana Trench can be specified with the `--rules-paths` argument. The default set of rules that run can be found in [configuration/rules.json](https://github.com/facebook/mariana-trench/blob/main/configuration/rules.json).

<MultiSourceSinkRule />

### Sources

Sources describe sources produced or received by a given method. A source can either flow out via the return value or flow via a given parameter. A source has a **kind** that describes its content (e.g, user input, file system, etc).

Here is an example where the source flows by return value:
```java
public static String getPath() {
    return System.getenv().get("PATH");
}
```
The JSON model for this method could be:
```json
{
  "sources": [
    {
      "kind": "UserControlled",
      "port": "Return"
    }
  ]
}
```

Here is an example where the source flows in via an argument:
```java
class MyActivity extends Activity {
  public void onNewIntent(Intent intent) {
    // intent should be considered a source here.
  }
}
```
The JSON model for this method could be:
```json
{
  "sources": [
    {
      "kind": "UserControlled",
      "port": "Argument(1)"
    }
  ]
}
```
Note that the implicit `this` parameter is considered the argument 0.

### Sinks

Sinks describe dangerous or sensitive methods in the code. A sink has a **kind** that represents the type of operation the method does (e.g, command execution, file system operation, etc). A sink must be attached to a given parameter of the method. A method can have multiple sinks.

Here is an example of a sink:
```java
public static String readFile(String path, String extension, int mode) {
    // Return the content of the file path.extension
}
```
Since `path` and `extension` can be used to read arbitrary files, we consider them sinks. We do not consider `mode` as a sink since we do not care whether the user can control it. The JSON model for this method could be:
```json
{
  "sinks": [
    {
      "kind": "FileRead",
      "port": "Argument(0)"
    },
    {
      "kind": "FileRead",
      "port": "Argument(1)"
    }
  ]
}
```

### Return Sinks

Return sinks can be used to describe that a method should not return tainted information. A return sink is just a normal sink with a `Return` port.

### Propagation

Propagations − also called **tito** (Taint In Taint Out) or **passthrough** in other tools − describe how the method propagates taint. A propagation as an **input** (where the taint comes from) and an **output** (where the taint is moved to).

Here is an example of a propagation:
```java
public static String concat(String x, String y) {
  return x + y;
}
```
The return value of the method can be controlled by both parameters, hence it has the propagations `Argument(0) -> Return` and `Argument(1) -> Return`.  The JSON model for this method could be:
```json
{
  "propagation": [
    {
      "input": "Argument(0)",
      "output": "Return"
    },
    {
      "input": "Argument(1)",
      "output": "Return"
    }
  ]
}
```

### Features

Features (also called **breadcrumbs**) can be used to tag a flow and help filtering issues. A feature describes a property of a flow. A feature can be any arbitrary string.

For instance, the feature `via-numerical-operator` is used to describe that the data flows through a numerical operator such as an addition.

Features are very useful to filter flows in the SAPP UI. E.g. flows with a cast from string to integer are can sometimes be less important during triaging since controlling an integer is more difficult to exploit than controlling a full string.

Note that features **do not stop** the flow, they just help triaging.

#### Attach to Sources

*Attach to sources* is used to add a set of [features](#features) on any sources flowing out of a method through a given parameter or return value.

For instance, if we want to add the feature `via-signed` to all sources flowing out of the given method:
```java
public String getSignedCookie();
```
We could use the following JSON model:
```json
{
  "attach_to_sources": [
    {
      "features": ["via-signed"],
      "port": "Return"
    }
  ]
}
```

Note that this is only useful for sources inferred by the analysis. If you know that `getSignedCookie` returns a source of a given kind, you should use a source instead.

#### Attach to Sinks

*Attach to sinks* is used to add a set of [features](#features) on all sinks on the given parameter of a method.

For instance, if we want to add the feature `via-user` on all sinks of the given method:
```java
class User {
  public static User findUser(String username) {
    // The code here might use SQL, Thrift, or anything. We don't need to know.
  }
}
```
We could use the following JSON model:
```json
{
  "attach_to_sinks": [
    {
      "features": ["via-user"],
      "port": "Argument(0)"
    }
  ]
}
```

Note that this is only useful for sinks inferred by the analysis. If you know that `findUser` is a sink of a given kind, you should use a sink instead.

#### Attach to Propagations

*Attach to propagations* is used to add a set of [features](#features) on all propagations from or to a given parameter or return value of a method.

For instance, if we want to add the feature `via-concat` to the propagations of the given method:
```java
public static String concat(String x, String y);
```
We could use the following JSON model:
```json
{
  "attach_to_propagations": [
    {
      "features": ["via-concat"],
      "port": "Return" // We could also use Argument(0) and Argument(1)
    }
  ]
}
```

Note that this is only useful for propagations inferred by the analysis. If you know that `concat` has a propagation, you should model it as a propagation directly.

#### Add Features to Arguments

*Add features to arguments* is used to add a set of  [features](#features) on all sources that **might** flow on a given parameter of a method.

*Add features to arguments* implies *Attach to sources*, *Attach to sinks* and *Attach to propagations*, but it also accounts for possible side effects at call sites.

For instance:
```java
public static void log(String message) {
  System.out.println(message);
}
public void buyView() {
  String username = getParameter("username");
  String product = getParameter("product");
  log(username);
  buy(username, product);
}
```
Technically, the `log` method doesn't have any source, sink or propagation. We can use *add features to arguments* to add a feature `was-logged` on the flow from `getParameter("username")` to `buy(username, product)`. We could use the following JSON model for the `log` method:
```json
{
  "add_features_to_arguments": [
    {
      "features": ["was-logged"],
      "port": "Argument(0)"
    }
  ]
}
```

#### Via-type Features

*Via-type* features are used to keep track of the type of a callable’s port seen at its callsites during taint flow analysis. They are specified in model generators within the “sources” or “sinks” field of a model with the “via_type_of” field. It is mapped to a nonempty list of ports of the method for which we want to create via-type features.

For example, if we were interested in the specific Activity subclasses with which the method below was called:

```java

public void startActivityForResult (Intent intent, int requestCode);

// At some callsite:
ActivitySubclass activitySubclassInstance;
activitySubclassInstance.startActivityForResult(intent, requestCode);

```
we could use the following JSON to specifiy a via-type feature that would materialize as `via-type:ActivitySubclass`:

```json
{
 "sinks": [
   {
     "port": "Argument(1)",
     "kind": "SinkKind",
     "via_type_of": ["Argument(0)"]
   }
 ]
}
```

#### Via-value Features

*Via-value* feature captures the value of the specified callable's port seen at its callsites during taint flow analysis.  They are specified similar to `Via-type` features -- in model generators within the "sources" or "sinks" field of a model with the "via_value_of" field. It is mapped to a nonempty list of ports of the method for which we want to create via-value features.

For example, if we were interested in the specific `mode` with which the method below was called:

```java
public void log (String mode, String message);

class Constants {
  public static final String MODE = "M1";
}

// At some callsite:
log(Constants.MODE, "error message");

```
we could use the following JSON to specifiy a via-value feature that would materialize as `via-value:M1`:

```json
{
 "sinks": [
   {
     "port": "Argument(1)",
     "kind": "SinkKind",
     "via_value_of": ["Argument(0)"]
   }
 ]
}
```

Note that this only works for numeric and string literals. In cases where the argument is not a constant, the feature will appear as `via-value:unknown`.

### Sanitizers

Specifying sanitizers on a model allow us to stop taint flowing through that method. In Mariana Trench, they can be one of three types -
* `sources`: prevent any taint sources from flowing out of the method
* `sinks`: prevent taint from reaching any sinks within the method
* `propagations`: prevent propagations from being inferred between any two ports of the method.

These can be specified in model generators as follows -

```
"find": "methods",
"where": ...,
"model": {
  "sanitizers": [
    {"sanitize": "sources"},
    {"sanitize": "sinks"},
    {"sanitize": "propagations"},
  ],
  ...
}
```

Note, if there are any user-specificed sources, sinks or propagations on the model, sanitizers will not affect them, but it will prevent them from being propagated outward to callsites.

#### Kind-specific Sanitizers

`sources` and `sinks` sanitizers may include a list of kinds (each with or without a partial_label) to restrict the sanitizer to only sanitizing taint of those kinds. (When unspecified, as in the example above, all taint is sanitized regardless of kind).

```
"sanitizers": [
  {"sanitize": "sinks", "kinds": [{"kind": "SinkKindA"}, {"kind": "SinkKindB", "partial_label": "A"}]},
]
```

#### Port-specific Sanitizers

Sanitizers can also specify a specific port ([access path](models.md#access-path-format) root) they sanitize (ignoring all the rest). This field `port` has a slightly different meaning for each kind of sanitizer -
* `sources`: represents the output port through which sources may not leave the method
* `sinks`: represents the input port through which taint may not trigger any sinks within the model
* `propagations`: represents the input port through which a propagation to any other port may not be inferred

For example if the following method
```
public void someMethod(Object argument1, Object argument2) {
  toSink(argument1);
  toSink(argument2);
}
```
had the following sanitizer in its model,

```
"sanitizers": [
  {"sanitize": "sinks", "port": "Argument(1)"},
]
```

Then a source flowing into `argument1` would be able to cause an issue, but not a source flowing into `argument2`.

Kind and port specifications may be included in the same sanitizer.

### Modes

Modes are used to describe specific behaviors of methods. Available modes are:

* `override-default`: do not infer modes of methods using heuristics;
* `skip-analysis`: skip the analysis of the method;
* `add-via-obscure-feature`: add a feature/breadcrumb called `via-obscure:<method>` to sources flowing through this method;
* `taint-in-taint-out`: propagate the taint on arguments to the return value and into the `this` parameter.
* `no-join-virtual-overrides`: do not consider all possible overrides when handling a virtual call to this method.
* `no-collapse-on-propagation`: do not collapse input paths when applying propagations.
* `alias-memory-location-on-invoke`: aliases existing memory location at the callsite instead of creating a new one.


### Default model

A default model is created for each method, except if it is provided by a model generator.
The default model has a set of heuristics:

If the method has no source code, the model is automatically marked with the following modes:
`skip-analysis`, `add-via-obscure-feature`, `taint-in-taint-out`.

If the method has more than 40 overrides, it is marked with the mode `no-join-virtual-overrides`.

Otherwise, the default model is empty (no sources/sinks/propagations).

### Field Models

These models represent user-defined taint on class fields (as opposed to methods, as described in all the previous sections on this page). They are specified in a similar way to method models as described below.

> **NOTE:** Field sources should not be applied to fields that are both final and of a primitive type (`int`, `char`, `float`, etc as well as `java.lang.String`) as the Java compiler optimizes accesses of these fields in the bytecode into accesses of the constant value they hold. In this scenario, Mariana Trench has no way of recognizing that the constant was meant to carry a source.

Example field model generator for sources:
```
"find": "fields",
"where": [
  {
    "constraint": "name",
    "pattern": "SOURCE_EXAMPLE"
  }
],
"model": {
  "sources" : [
    {
      "kind": "FieldSource"
    }
  ]
}
```

Example code:
```java
public class TestClass {
  // Field that we know to be tainted
  public Object SOURCE_EXAMPLE = ...;

  void flow() {
    sink(EXAMPLE, ...);
  }
}
```

Example field model generator for sinks:
```
"find": "fields",
"where": [
  {
    "constraint": "name",
    "pattern": "SINK_EXAMPLE"
  }
],
"model": {
  "sinks" : [
    {
      "kind": "FieldSink"
    }
  ]
}
```

Example code:
```java
public class TestClass {
  public Object SINK_EXAMPLE = ...;

  void flow() {
    SINK_EXAMPLE = source();
  }
}
```

Field signature formats follow the Dalvik bytecode format similar to methods as discussed [above](#method-name-format). This is of the form `<className>.<fieldName>:<fieldType>`.

## Generators

Mariana Trench allows for dynamic model specifications. This allows a user to specify models of methods before running the analysis. This is used to specify sources, sinks, propagation and modes.

Model generators are specified in a generator configuration file, specified by the `--generator-configuration-path` parameter. By default, we use [`default_generator_config.json`](https://github.com/facebook/mariana-trench/blob/main/configuration/default_generator_config.json).

### Example

Examples of model generators are located in the [`configuration/model-generators`](https://github.com/facebook/mariana-trench/tree/main/configuration/model-generators) directory.

Below is an example of a JSON model generator:
```json
{
  "model_generators": [
      {
        "find": "methods",
        "where": [
          { "constraint": "name", "pattern": "toString" }
        ],
        "model": {
          "propagation": [
            {
              "input": "Argument(0)",
              "output": "Return"
            }
          ]
        }
      },
      {
        "find": "methods",
        "where": [
          {
            "constraint": "parent",
            "inner": {
              "constraint": "extends",
              "inner": {
                "constraint": "name", "pattern": "SandcastleCommand"
              }
            }
          },
          { "constraint": "name", "pattern": "Time" }
        ],
        "model": {
          "sources": [
            {
              "kind": "Source",
              "port": "Return"
            }
          ]
        }
      },
      {
        "find": "methods",
        "where": [
          { "constraint": "parent", "inner": { "constraint": "extends", "inner": { "constraint": "name", "pattern": "IEntWithPurposePolicy" }} },
          { "constraint": "name", "pattern": "gen.*" },
          { "constraint": "parameter", "idx": 0, "inner": { "constraint": "type", "kind": "extends", "class": "IViewerContext" } },
          { "constraint": "return", "inner": { "constraint": "extends", "inner": { "constraint": "name", "pattern": "Ent" } } }
        ],
        "model": {
          "modes": ["add-via-obscure-feature"],
          "sinks": [
            {
              "kind": "Sink",
              "port": "Argument(0)",
              "features": ["via-gen"]
            }
          ]
        }
      }
  ]
}
```

### Specification

Each JSON file is a JSON object with a key `model_generators` associated with a list of "rules".

Each "rule" defines a "filter" (which uses "constraints" to specify methods for which a "model" should be generated) and a "model". A rule has the following key/values:

- `find`: The type of thing to find. We support `methods` and `fields`;
- `where`: A list of "constraints". All constraints **must be satisfied** by a method or field in order to generate a model for it. All the constraints are listed below, grouped by the type of object they are applied to:
  - **Method**:
    - `signature`: Expects an extra property `pattern` which is a regex to fully match the full signature (class, method, argument types) of a method;
    - `parent`: Expects an extra property `inner` [Type] which contains a nested constraint to apply to the class holding the method;
    - `parameter`: Expects an extra properties `idx` and `inner` [Type], matches when the idx-th parameter of the function or method matches the nested constraint inner;
    - `return`: Expects an extra property `inner` [Type] which contains a nested constraint to apply to the return of the method;
    - `is_static | is_constructor | is_native | has_code`: Accepts an extra property `value` which is either `true` or `false`. By default, `value` is considered `true`;
    - `number_parameters`: Expects an extra property `inner` [Integer] which contains a nested constraint to apply to the number of parameters (counting the implicit `this` parameter);
    - `number_overrides`: Expects an extra property `inner` [Integer] which contains a nested constraint to apply on the number of method overrides.

  - **Type:**
    - `extends`: Expects an extra property `inner` [Type] which contains a nested constraint that must apply to one of the base classes or itself. The optional property `includes_self` is a boolean that tells whether the constraint must be applied on the type itself or not;
    - `super`: Expects an extra property `inner` [Type] which contains a nested constraint that must apply on the direct superclass;
    - `is_class | is_interface`: Accepts an extra property `value` which is either `true` or `false`. By default, `value` is considered `true`;

  - **Field**:
    - `signature`: Expects an extra property `pattern` which is a regex to fully match the full signature of the field. This is of the form `<className>.<fieldName>:<fieldType>`;
    - `parent`: Expects an extra property `inner` [Type] which contains a nested constraint to apply to the class holding the field;
    - `is_static`: Accepts an extra property `value` which is either `true` or `false`. By default, `value` is considered `true`;

  - **Method, Type or Field:**
    - `name`: Expects an extra property `pattern` which is a regex to fully match the name of the item;
    - `has_annotation`: Expects an extra property `type` and an optional property `pattern`, respectively a string and a regex fully matching the value of the annotation.
    - `visibility`: Expects an extra property `is` which is either `public`, `private` or `protected`; (Note this does not apply to `Field`)

  - **Integer:**
    - `< | <= | == | > | >= | !=`: Expects an extra property `value` which contains an integer that the input integer is compared with. The input is the left hand side.

  - **Any (Method, Type, Field or Integer):**
    - `all_of`: Expects an extra property `inners` [Any] which is an array holding nested constraints which must all apply;
    - `any_of`: Expects an extra property `inners` [Any] which is an array holding nested constraints where one of them must apply;
    - `not`: Expects an extra property `inner` [Any] which contains a nested constraint that should not apply. (Note this is not yet implemented for `Field`s)

- `model`: A model, describing sources/sinks/propagations/etc.
  - **For method models**
    - `sources`*: A list of sources, i.e a source flowing out of the method via return value or flowing in via an argument. A source has the following key/values:
      - `kind`: The source name;
      - `port`**: The source access path (e.g, `"Return"` or `"Argument(1)"`);
      - `features`*: A list of features/breadcrumbs names;
      - `via_type_of`*: A list of ports;
    - `sinks`*: A list of sinks, i.e describing that a parameter of the method flows into a sink. A sink has the following key/values:
      - `kind`: The sink name;
      - `port`: The sink access path (e.g, `"Return"` or `"Argument(1)"`);
      - `features`*:  A list of features/breadcrumbs names;
      - `via_type_of`*: A list of ports;
    - `propagation`*: A list of propagations (also called passthrough) that describe whether a taint on a parameter should result in a taint on the return value or another parameter. A propagation has the following key/values:
      - `input`: The input access path (e.g, `"Argument(1)"`);
      - `output`: The output access path (e.g, `"Return"` or `"Argument(2)"`);
      - `features`*: A list of features/breadcrumbs names;
    - `attach_to_sources`*: A list of attach-to-sources that describe that all sources flowing out of the method on the given parameter or return value must have the given features. An attach-to-source has the following key/values:
      - `port`: The access path root (e.g, `"Return"` or `"Argument(1)"`);
      - `features`: A list of features/breadcrumb names;
    - `attach_to_sinks`*: A list of attach-to-sinks that describe that all sources flowing in the method on the given parameter must have the given features. An attach-to-sink has the following key/values:
      - `port`: The access path root (e.g, `"Argument(1)"`);
      - `features`: A list of features/breadcrumb names;
    - `attach_to_propagations`*: A list of attach-to-propagations that describe that inferred propagations of sources flowing in or out of a given parameter or return value  must have the given features. An attach-to-propagation has the following key/values:
      - `port`: The access path root (e.g, `"Return"` or `"Argument(1)"`);
      - `features`: A list of features/breadcrumb names;
    - `add_features_to_parameters`*: A list of add-features-to-parameters that describe that flows that might flow on the given parameter must have the given features. An add-features-to-parameter has the following key/values:
      - `port`: The access path root (e.g, `"Argument(1)"`);
      - `features`: A list of features/breadcrumb names;
    - `modes`*: A list of mode names that describe specific behaviors of a method;
    - `for_all_parameters`: Generate sources/sinks/propagations/attach_to_* for all parameters of a method that satisfy some constraints. It accepts the following key/values:
      - `variable`: A symbolic name for the parameter;
      - `where`: An optional list of constraints on the type of the parameter;
      - `sources | sinks | propagation`: Same as under "model", but we accept the variable name as a parameter number.
  - `verbosity`*: A logging level, to help debugging. 1 is the most verbose, 5 is the least. The default verbosity level is 5.


  - **For Field models**
    - `sources`*: A list of sources the field should hold. A source has the following key/values:
      - `kind`: The source name;
      - `features`*: A list of features/breadcrumbs names;
    - `sinks`*: A list of sinks the field should hold. A sink has the following key/values:
      - `kind`: The sink name;
      - `features`*: A list of features/breadcrumds names;

In the above bullets,

- `*` denotes optional key/value.
- `**` denotes optional key/value. Default is `"Return"`.

Note, the implicit `this` parameter for methods has the parameter number 0.

### Development

#### When Sources or Sinks don't appear in Results

1. This could be because your model generator did not find any method matching your query. You can use the `"verbosity": 1` option in your model generator to check if it matched any method. For instance:
    ```json
    {
      "model_generators":. [
        {
          "find": "methods",
          "where": /* ... */,
          "model": {
            /* ... */
          },
          "verbosity": 1
        }
      ]
    }
    ```
    When running mariana trench, this should print:
    ```
    INFO Method `...` satisfies all constraints in json model generator ...
    ```

  2. Make sure that your model generator is actually running. You can use the `--verbosity 2` option to check that. Make sure your model generator is specified in `configuration/default_generator_config.json`.
  3. You can also check the output models.
      Use `grep SourceKind models@*` to see if your source or sink kind exists.
      Use `grep 'Lcom/example/<class-name>;.<method-name>:' models@*` to see if a given method exists in the app.

### Override the default model

Mariana trench generates a default model for each method ([see above](models.md#default-model)) using a set of heuristics.

Users can use a [model generator](models.md#generators) to override the default behavior. This can be done to:
* Stop a method from propagating the taint (e.g, `escape` functions);
* Skip the analysis of a slow method;
* Avoid considering all overrides when handling a virtual method call;
* Etc..

In order to do that, one needs to create a model using the `override-default` mode, with any additional modes:
```
auto model = Model(method, /* modes */ Model::Mode::OverrideDefault | Model::Mode::NoJoinVirtualOverrides);
```

NOTE: If you get the error `Attempting to analyze method <name> with no code!`, it probably means that you forgot to mark the method with `Model::Mode::SkipAnalysis`

<FbModels />
