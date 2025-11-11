# Porpoise Tool Configuration

Porpoise Tool can be configured using a `config.json` file placed beside the executable.

## Configuration File Location

Place `config.json` in the same directory as `porpoise_tool.exe` (or `porpoise_tool` on Linux/Mac).

```
bin/
├── porpoise_tool.exe
└── config.json          ← Place config here
```

## Configuration Options

### `transpile_sdk_functions` (boolean)

**Default:** `false`

- `false`: SDK functions are **ignored** during transpilation. They will be treated as external functions and not transpiled.
- `true`: SDK functions **will be transpiled** like regular functions.

**Example:**
```json
{
  "transpile_sdk_functions": false
}
```

**When to use:**
- Set to `false` if you have SDK function implementations elsewhere (e.g., from the GameCube SDK)
- Set to `true` if you want to transpile SDK functions to see their implementation

### `skip_stdlib_stubs` (boolean)

**Default:** `false`

- `false`: Include `stdlib_stubs.h` in generated projects (for standalone projects)
- `true`: Skip `stdlib_stubs.h` inclusion (for projects with MSL_C or other standard library headers)

**Example:**
```json
{
  "skip_stdlib_stubs": true
}
```

**When to use:**
- Set to `true` if your project uses MSL_C (Metrowerks Standard Library) headers
- Set to `true` if you're getting redefinition errors (C2371) for functions like `abs`, `feof`, `fflush`, etc.
- Set to `false` for standalone projects without MSL_C

### `ignore_cstd_calls` (boolean)

**Default:** `true`

- `false`: C++ standard library calls will be transpiled (may create stub functions)
- `true`: C++ standard library calls will be **ignored** and replaced with comments

**Example:**
```json
{
  "ignore_cstd_calls": true
}
```

**When to use:**
- Set to `true` if you're transpiling C++ code with `std::` namespace calls
- Set to `true` if you want to avoid transpiling C++ standard library functions like `std::string`, `std::vector`, etc.
- Set to `true` if you're getting errors from C++ mangled names (like `_ZNS...`, `_ZNSt...`)

**What gets ignored:**
- Functions with `std::` prefix (demangled names)
- C++ mangled names starting with `_ZNS` (std:: namespace)
- C++ mangled names starting with `_ZNSt` (std:: namespace with templates)
- Operator new/delete functions

**Note:** When ignored, these calls are replaced with comments like `/* C++ std call ignored: std::string::c_str */`

### `sdk_functions_file` (string)

**Default:** `"sdk_functions.txt"`

Path to the SDK functions signature file. This file should be in the same directory as the executable or a relative path from it.

**Example:**
```json
{
  "sdk_functions_file": "sdk_functions.txt"
}
```

**File format:**
```
FunctionName:param1_type,param2_type,param3_type
OSReport:ptr,int
DCFlushRange:ptr,int
```

Parameter types: `ptr`, `int`, `float`, `double`

### `skip_list_file` (string)

**Default:** `""` (empty - no skip list)

Path to a skip list file containing function names to skip during transpilation. This file should be in the same directory as the executable or a relative path from it.

**Example:**
```json
{
  "skip_list_file": "skip_functions.txt"
}
```

**File format:**
```
# Comments start with #
# One function name per line

fn_80003100
fn_80003200
InitSystem
OSReport
```

**Note:** Command-line skip list file takes precedence over config file.

## Complete Example

```json
{
  "transpile_sdk_functions": false,
  "skip_stdlib_stubs": true,
  "ignore_cstd_calls": true,
  "sdk_functions_file": "sdk_functions.txt",
  "skip_list_file": "skip_functions.txt"
}
```

## Usage

1. Copy `config.json.example` to `config.json` beside your executable
2. Edit `config.json` with your desired settings
3. Run Porpoise Tool - it will automatically load the configuration

## Troubleshooting

### Config file not found

If the config file is not found, Porpoise Tool will use default values. You'll see:
```
Warning: Could not open SDK functions file: sdk_functions.txt
```

This is normal if you don't have a config file yet.

### Config file loaded successfully

When the config file is found and loaded, you'll see:
```
Loaded configuration from: E:\GPT5\Porpoise_Tool\bin\config.json
```

## Priority Order

1. **Command-line arguments** (highest priority)
   - Skip list file specified via command line overrides config file
2. **config.json** file
3. **Default values** (lowest priority)

## See Also

- `FIX_MSL_CONFLICT.md` - Information about MSL_C conflicts
- `skip_functions_example.txt` - Example skip list format
- `sdk_functions.txt` - Example SDK functions file

