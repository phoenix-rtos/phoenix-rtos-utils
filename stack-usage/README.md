configurable static stack-usage analysis tool. Thin wrapper around https://github.com/hbehrens/puncover.

Added features (as compared to puncover-0.7.0):

* stack-usage limits for each entry point
* option to manually (via config) inject missing indirect calls
* ascii result for command-line usage
* json result for integration with other tooling

## Usage

```bash
cd phoenix-rtos-project
python3 -m venv _venv
./_venv/bin/python3 -m pip install -r ./phoenix-rtos-utils/stack-usage/requirements.txt
```

```
# example paths used for checking stm32l4-multi
ELF="_build/armv7m4-stm32l4x6-nucleo/prog/stm32l4-multi"
BUILD_DIR="_build/armv7m4-stm32l4x6-nucleo/"
CONFIG="phoenix-rtos-devices/multi/stm32l4-multi/stack-usage.json"

# show per thread stack usage on stdout and start puncover webserver on http://localhost:8080
./_venv/bin/python3 ./phoenix-rtos-utils/stack-usage/analyze.py --config ${CONFIG} ${ELF} --web

# generate json result (to integrate with tools)
./_venv/bin/python3 ./phoenix-rtos-utils/stack-usage/analyze.py --config ${CONFIG} ${ELF} --json
```

## Coding style recommendations

There are some naming / coding practices that can help to define and keep config up to date.

### thread symbols
```c
/* non-static declarations in c file (not part of external API) */
uint8_t module_nameStack[128];
void * module_nameThread(void *arg);


void module_nameThread(void *arg) {
    ...
    endthread();
}

void module_init(void) {
    beginthread(module_nameThread, 4, module_nameStack, sizeof(module_nameStack), NULL);
}
```

With separate stack symbol size of symbol can be defined as limit for thread

```json
{
    "entry": "module_nameThread",
    "limit": { "type": "symbol-size", "value": "module_nameStack" }
}
```

### function pointers

Keeping consistent functions names between different functions implementing same interface allows using single regex
pattern to define multiple `extra-calls`.

Example from posixsrv (function pointers in `operations_t` struct)
```
{
    "caller": "phoenix-rtos-posixsrv/posixsrv.c/posixsrv_threadMain",
    "callee-patterns": [
        ".*/phoenix-rtos-posixsrv/[^/]*.c/[^/]*_op"
    ]
}
```

## Config file

### options
gcc-base
: GCC executables prefix. Can be full path

page-size
: Platform specific page size used for stack size definitions (default `_start` stack size in phoenix is 3 * page-size)

reserve
: Additional bytes reserved on stack

src-base
: Path to source code root relative to config file. This is used as base for referencing symbols from specific files

threads
: Definition of analyzed symbols and stack limits

extra-calls
: Manually added call graph edges. Calls via function pointers are not detected and need to be specified manually

ignored-functions
: List of symbol names that are ignored (stack usage overwritten to 0)

### example
```json
{
    "gcc-base": "arm-phoenix-",
    "reserve": 128,
    "page-size": 512,
    "src-base": "../../../",
    "threads": [
        {
            "entry": "_start",
            "limit": { "type": "value", "value": 3, "unit": "page" }
        },
        {
            "entry": "module_name",
            "limit": { "type": "value", "value": 128 }
        },
        {
            "entry": "module_nameThread",
            "limit": { "type": "symbol-size", "value": "module_nameStack" }
        }
    ],
    "extra-calls": [
        {
            "caller": "libphoenix/stdio/format.c/format_printBuffer",
            "callees": [ "printf_feed" ]
        },
        {
            "caller": "phoenix-rtos-posixsrv/posixsrv.c/posixsrv_threadMain",
            "callee-patterns": [".*/phoenix-rtos-posixsrv/[^/]*.c/[^/]*_op"]
        }
    ],
    "ignored-functions": [ "module_traceFunction" ]
}
```

## TODO
- add option to use multiple config files to reuse "extra-calls" for specific component
- consider more human friendly config format (ie. with comments)
- document additional overhead (reserve) is needed for syscalls context and signal handlers
