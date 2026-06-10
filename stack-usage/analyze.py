from pathlib import Path
from contextlib import redirect_stdout, redirect_stderr
import subprocess
from dataclasses import dataclass, asdict
import logging
import argparse
import shutil
import json
import sys
import ast
import io
import re

from puncover.builders import ElfBuilder
from puncover.gcc_tools import GCCTools
from puncover.collector import Collector


def detect_gnu_stack_size(gcc_base: str, elf: Path):
    cmd = [f"{gcc_base}readelf", "--wide", "--program-headers", elf]
    raw = subprocess.run(cmd, stdout=subprocess.PIPE).stdout.decode("ascii")

    # GNU ld's option '-z stack-size=<size>' adds program header with type GNU_STACK
    # This is parsed during process_load(32|64) in phoenix-rtos-kernel/proc/process.c
    if "GNU_STACK" not in raw:
        return None

    # Example output from readelf
    # > Program Headers:
    # >  Type           Offset   VirtAddr   PhysAddr   FileSiz MemSiz  Flg Align
    # >  [...]
    # >  GNU_STACK      0x000000 0x00000000 0x00000000 0x00000 0x00800 RWE 0x10
    for line in raw.splitlines():
        if "GNU_STACK" not in line:
            continue

        m = re.fullmatch(" +GNU_STACK +(0x[0-9a-fA-F]+ ){5}.*", line)
        assert m
        stack_size = int(m.group(1).rstrip(), 16)
        return stack_size


class AppCollector(Collector):
    def __init__(self, gcc_tools, src_base, extra_calls, ignored_functions):
        super().__init__(gcc_tools)
        self._src_base = src_base
        self._extra_calls = extra_calls
        self._ignored_functions = ignored_functions

    def enhance_call_tree(self):
        super().enhance_call_tree()

        errors = 0

        class ResolveError(Exception):
            pass

        def _resolve(name):
            if "/" in name:
                name = self._src_base.removeprefix("/") + "/" + name
                f = self.symbol(name, qualified=True)
            else:
                f = self.symbol(name, qualified=False)

            if f is None:
                logging.error(f"symbol not found: {name}")
                raise ResolveError(f"symbol not found: {name}")
            return f

        for extra_call in self._extra_calls:
            try:
                caller = _resolve(extra_call["caller"])
            except ResolveError:
                errors += 1
                continue

            for callee in extra_call.get("callees", []):
                try:
                    self.add_function_call(caller, _resolve(callee))
                except ResolveError:
                    errors += 1

            pattern: str
            for pattern in extra_call.get("callee-patterns", []):
                found = 0
                for func in self.all_functions():
                    name = self.qualified_symbol_name(func) if "/" in pattern else func["name"]
                    if re.fullmatch(pattern, name):
                        found += 1
                        self.add_function_call(caller, func)
                if not found:
                    logging.warning(f"no matches for {pattern=} {extra_call=}")
                    errors += 1

        for name in self._ignored_functions:
            try:
                f = _resolve(name)
                f["deepest_callee_tree"] = f.get("stack_size", 0), [f]
            except ResolveError:
                errors += 1


# TODO: determine what is worst case overhead (interrupts?, signals?, syscalls?), add comment, and adjust value
DEFAULT_RESERVE = 128


# extracted from puncover main
def run_webserver(builder, host, port):
    import puncover
    from flask import Flask
    from puncover import renderers
    from puncover.middleware import BuilderMiddleware

    builder.build_if_needed()
    base = Path(puncover.__file__).parent
    app = Flask(__name__, template_folder=base / "templates", static_folder=base / "static")
    renderers.register_jinja_filters(app.jinja_env)
    renderers.register_urls(app, builder.collector)
    app.wsgi_app = BuilderMiddleware(app.wsgi_app, builder)
    app.run(host=host, port=port)
    return


def analyze_orphans(c: AppCollector, config):
    entry_points = set(t["entry"] for t in config["threads"])
    reachable = set()
    children = [c.symbols_by_name[name] for name in entry_points]

    # add all class handlers as roots
    for func in c.all_functions():
        name = func["name"]
        if name.startswith("class"):
            children.append(func)
        if "_handle" in name or name.startswith("handle"):
            children.append(func)

    while children:
        symbol = children.pop(0)
        address = symbol["address"]
        if address in reachable:
            continue
        reachable.add(address)
        children.extend(symbol["callees"])

    for func in c.all_functions():
        if func["address"] in reachable:
            continue

        name = func["name"]
        usage = func["deepest_callee_tree"][0]
        size = func["size"]
        if size > 300:
            print(f"UNREACHABLE: {name} {size}")

    by_size = list(sorted(c.all_functions(), key=lambda f: f["deepest_callee_tree"][0]))

    for func in by_size:
        if func["callers"]:
            continue

        name = func["name"]
        if name in entry_points:
            continue

        usage = func["deepest_callee_tree"][0]
        if name.startswith("sys_"):
            assert usage == 0
            continue
        if name.startswith("class"):
            continue
        if "_handle" in name or name.startswith("handle"):
            continue
        size = func["size"]
        name = c.qualified_symbol_name(func)
        if usage > 20:
            logging.warning(f"ORPHAN: {name} code={size} stack={usage}")


def main():
    parser = argparse.ArgumentParser()

    parser.add_argument("elf", type=Path)
    parser.add_argument(
        "--json",
        action="store_true",
        help="write analysis result in json format. When enabled exit code is success event when overflow is detected.",
    )
    parser.add_argument(
        "--reserve",
        type=int,
        default=None,
        help=f"decreases stack size limit by fixed amount. If not specified defaults to {DEFAULT_RESERVE}",
    )
    parser.add_argument("--no-color", action="store_true")
    parser.add_argument(
        "--gcc-base", help="Toolchain prefix: `arm-phoenix-`. Full path can be used too `/…/bin/arm-phoenix-`"
    )
    parser.add_argument("--src-base", help="Base source file used for extra-calls paths", default=None)
    parser.add_argument("--config", type=Path)

    group = parser.add_argument_group("web server")
    group.add_argument("--web", help="Run puncover web ui", action="store_true")
    group.add_argument("--web-port", default=8080, type=int)
    group.add_argument("--web-host", default="localhost")

    args = parser.parse_args()

    logging.basicConfig(level=logging.INFO)

    if args.config is not None:
        config = json.loads(args.config.read_text())
    else:
        config = {"threads": [{"entry": "_start", "limit": {"type": "GNU_STACK"}}]}

    reserve = args.reserve if args.reserve is not None else config.get("reserve", DEFAULT_RESERVE)
    gcc_base = args.gcc_base or config.get("gcc-base")
    if gcc_base is None:
        print(f"ERROR: gcc-base was not specified", file=sys.stderr)
        sys.exit(1)

    if "/" not in gcc_base:
        gcc_path = shutil.which(gcc_base + "gcc")
        if gcc_path is None:
            print(f"ERROR: {gcc_base}gcc was not found in PATH", file=sys.stderr)
            sys.exit(1)
        gcc_base = gcc_path.removesuffix("gcc")

    src_root = None

    elf_file = args.elf
    su_dir = elf_file.parent.parent
    assert su_dir.parent.name == "_build"

    src_base = (
        args.src_base or ((args.config or Path("/missing")).parent / config.get("src-base", Path.cwd())).resolve()
    )
    assert src_base.exists()

    extra_calls = tuple(config.get("extra-calls", []))
    for call in extra_calls:
        expected_keys = {"comments", "caller", "callees", "callee-patterns"}
        unknown_keys = set(call).difference(expected_keys)
        assert not unknown_keys, f"{unknown_keys=} {call=}"
        assert set(call).intersection({"callees", "callee-patterns"})

    ignored_functions = tuple(config.get("ignored-functions", []))

    c = AppCollector(GCCTools(gcc_base), str(src_base.absolute()), extra_calls, ignored_functions)
    builder = ElfBuilder(c, src_root, elf_file, su_dir)

    analysis_log = io.StringIO()
    try:
        with redirect_stderr(analysis_log):
            with redirect_stdout(analysis_log):
                builder.build_if_needed()
                c.build_symbol_name_index()
    except Exception:
        sys.stderr.write(analysis_log.getvalue())
        raise

    analyze_orphans(c, config)

    @dataclass
    class StackInfo:
        entry: str
        usage: int
        limit: int

    results = []

    for thread in config["threads"]:
        entry = thread["entry"]
        entry_symbol = c.symbols_by_name[entry]
        limit_type = thread["limit"]["type"]
        limit: int
        if limit_type == "value":
            limit = thread["limit"]["value"]
            assert isinstance(limit, int)
            unit = thread["limit"].get("unit", "bytes")
            assert unit in {"page", "bytes"}
            if unit == "page":
                limit *= config["page-size"]
            elif unit == "bytes":
                limit *= 1
            else:
                raise Exception("unknown unit: {unit}")

        elif limit_type == "expr":
            limit = ast.literal_eval(thread["limit"]["value"])
            assert isinstance(limit, int)
        elif limit_type == "GNU_STACK":
            detected = detect_gnu_stack_size(gcc_base, elf_file)
            if detected is None:
                raise RuntimeError("limit for default thread not found in elf file")
            limit = detected
        elif limit_type == "symbol-size":
            stack_symbol = c.symbols_by_name[thread["limit"]["value"]]
            limit = stack_symbol["size"]
        else:
            raise ValueError(f"unsupported thread limit detection type: {limit_type}")

        results.append(StackInfo(entry=entry, usage=entry_symbol["deepest_callee_tree"][0], limit=limit))

    overflow = [x.entry for x in results if x.usage + reserve > x.limit]

    if args.json:
        print(json.dumps({"threads": [asdict(x) for x in results], "overflow": bool(overflow)}))
        return

    print(f"  USED |  LIMIT | entry point")
    print(f"-------+--------+------------")
    for info in results:
        if info.usage > info.limit:
            style = "\x1b[31;1m"
        elif info.usage + reserve > info.limit:
            style = "\x1b[33;1m"
        else:
            style = ""

        if args.no_color:
            print(f"{info.usage:6d} | {info.limit:6d} | {info.entry}")
        else:
            print(f"{style}{info.usage:6d}\x1b[0m | {info.limit:6d} | {info.entry}")

    if overflow:
        print()
        print(f"ERROR: potential overflow in following threads: {overflow}")

    if not args.web:
        sys.exit(1 if overflow else 0)

    if overflow:
        print()
        print("Urls to overflowing symbols:")
        for entry in overflow:
            entry_symbol = c.symbols_by_name[entry]
            qualified_name = c.qualified_symbol_name(entry_symbol)
            print(f"http://{args.web_host}:{args.web_port}/path/" + qualified_name)
    print()
    run_webserver(builder, args.web_host, args.web_port)
    return


if __name__ == "__main__":
    main()
