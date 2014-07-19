# About cxxprops

cxxprops is a header-only C++14 library for reading and updating Java-like property files, with
additional features like property groups and format preservation.

The library only depends on the standard library.

# Features

* Formatting, comments and property order is preserved when updating properties.
* Can optionally pretty-format.
* utf8 support.
* Prefix groups and nesting to simplify the property files.
* Supports property templates to reduce duplication.
* Straightforward API

# Using the library

Just drop **cxxprops.h** into your project.

The input is any stream. In this example, we're reading
from a file:

```c++
cxxprops::Properties props;

std::ifstream prop("my.config");
props.parse(prop);
```

### Reading properties

```c++
// Read the server host
std::string host = prop.get("server.host");

// As above, but with a default value
std::string host = prop.get("server.host", "localhost);
```

Special support is provided for reading boolean values (true, 1 or yes)

```c++
if (prop.getBool("server.log.trace", false)) {
    ...
}
```

You can also check if a key exists. Note that a key with an empty value
still *exists*. Hence, it is usually better to call props.get and
verify the value. One exception is using the presence of
keys to indicate flags, which is a valid design in certain use cases.

```c++
props.hasKey("settings.debug");
```

### Setting and removing properties

```c++
// If the property doesn't exist, it will be created
props.put("bind", "127.0.0.1");

// A multi-line property. When rendered to a file, the newlines
// are replaced with \ as explained in the property file examples
// below.
props.put("multiline", "this takes \nmultiple \nlines");
```

To add a new property with a comment before it
```
props.putEmptyLine();
props.putComment("A new property");
props.put("somekey", "some value");
```

To remove a property:
```
props.remove("bind");
```

### Get all keys and values:

```c++
std::cout << "Keys:" << std::endl;
for (const auto& key : props.keys())
    std::cout << key << ",";

std::cout << std::endl;

std::cout << "Values:" << std::endl;
for (const auto& val : props.values())
    std::cout << val << ",";

std::cout << std::endl;
}
```

### Rendering properties and saving to file

After adding, updating or removing properties, text() is called to
render the properties as text. This string can then be saved to a file.

```c++
std::string properties = props.text();

// Save to file using ofstream or whatever you'd like
// ...
```

By default, formatting and whitespaces are preserved.

Alternatively, pretty printing can be forced by passing *true* to text()

```c++
std::string properties = props.text(true);
```

Pretty printing will collapse multiple empty lines into one, and remove
leading whitespaces from keys.

# Property file examples

### Simple string properties and comments

```
# A comment starts with # or !
myvalue = abc

# Strings can also be quoted, which are stripped when
# read by your program
othervalue = "some value"

# In either case, internal quotes are preserved
quotes1 = some value with a "quoted" word
quotes2 = "some value with a "quoted" word"

# To quote the entire string, use two quotes
quotes = ""quoting the entire string with "an internal quote" works too""

# Leading spaces
leading = \ \ Two leading spaces in the value

# Or use quotes to put spaces at both ends. The quotes
# are removed when read (escape, as above, to keep them)
spaces = "  to spaces before and after  "

```

### Multiple lines
```
multiline = a few \
            words on separate \
            lines
```

### Keys with no values
```
# Both keys exists, but has an empty value
key1 =
key2
```

### Format preservation
```
# By default, everything
 # will be
    # preserved, including white spaces

    prop.indented = some value
```

### Boolean values
```
# The API supports "getBool" which returns true if a value
# contains "true", "yes" or "1", otherwise false is returned.
# You can still treat the property as a string
production = true
```

### Unicode
With full utf8- and multi line support, cxxprops is a suitable format
for translation files. In a real  application, you would probably
split the translations into separate locale files, possibly combined
with prefix groups.

```
# Translations
greeting.english = Good morning
greeting.chinese = 早晨好
greeting.finnish = Hyvää huomenta
```

### Prefix groups
Instead of repeating a prefix like "server." on multiple properties, prefix
groups can be used to simplify the property files.

Prefixes and prefix groups can be mixed and matched freely:

```
server
{
    name = servername
    log.level = debug
}
```

which is the same as

```
server
{
    name = servername

    log
    {
        level = debug
    }
}
```

and

```
server.name = servername
server.log.level = debug
```

In all cases, your code will read the log level using "server.log.level" as key.

### Template variables
If you have duplicate key/value pairs, consider using template variables:

```
<logsettings>
log
{
    level = debug
    output = console
}
</logsettings>
```

Using the template:

```
backend.database
{
    host = db-host
    port = 1234

    %logsettings%
}

backend.appserver
{
    apikey = 0xfe4cce
    host = app-server-test

    %logsettings%
}

```

You can optionally keep the templates in a separate file. Simply prepend it to
the property file before calling parse(...)
