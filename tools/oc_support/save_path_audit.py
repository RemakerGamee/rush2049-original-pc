#!/usr/bin/env python3
"""Read-only inventory of selected Controller Pak-related source call sites."""

import argparse
import bisect
import hashlib
import json
import os
import re
import sys
from pathlib import Path


TARGETS = (
    "rush_native_pfs_8000A970",
    "rush_native_pfs_8000A520",
    "rush_native_pfs_8000B240",
    "osPfsGetFileSize",
)
TARGET_JAL_ADDRESSES = {
    "rush_native_pfs_8000A970": "0x8000a970",
    "rush_native_pfs_8000A520": "0x8000a520",
    "rush_native_pfs_8000B240": "0x8000b240",
    "osPfsGetFileSize": "0x8000aec4",
}
RECOMP_FUNC = re.compile(r"\bRECOMP_FUNC\b")
IDENTIFIER = re.compile(r"[A-Za-z_]\w*")
JAL_COMMENT = re.compile(
    r"^\s*//\s*(0x[0-9A-Fa-f]+)\s*:\s*jal\s+(0x[0-9A-Fa-f]+)\b"
)
CALL_PATTERNS = {name: re.compile(rf"\b{re.escape(name)}\s*\(") for name in TARGETS}
DECLARATION_EXPRESSION_KEYWORDS = {
    "break",
    "case",
    "continue",
    "default",
    "do",
    "else",
    "for",
    "goto",
    "if",
    "return",
    "switch",
    "while",
}


class AuditError(RuntimeError):
    """A source directory cannot be inventoried reliably."""

    def __init__(self, message, source_files=()):
        super().__init__(message)
        self.source_files = tuple(source_files)


class ReportDestinationError(RuntimeError):
    """The report destination aliases an inventoried source input."""


def _mask_comments_and_literals(source):
    """Blank C comments and literals while preserving character offsets."""
    masked = list(source)
    index = 0
    state = "code"
    while index < len(source):
        char = source[index]
        next_char = source[index + 1] if index + 1 < len(source) else ""
        if state == "code":
            if char == "/" and next_char == "/":
                masked[index] = masked[index + 1] = " "
                index += 2
                state = "line_comment"
                continue
            if char == "/" and next_char == "*":
                masked[index] = masked[index + 1] = " "
                index += 2
                state = "block_comment"
                continue
            if char in ('"', "'"):
                quote = char
                masked[index] = " "
                index += 1
                state = ("string", quote)
                continue
            index += 1
            continue
        if state == "line_comment":
            if char == "\n":
                state = "code"
            else:
                masked[index] = " "
            index += 1
            continue
        if state == "block_comment":
            if char == "*" and next_char == "/":
                masked[index] = masked[index + 1] = " "
                index += 2
                state = "code"
            else:
                if char != "\n":
                    masked[index] = " "
                index += 1
            continue
        _, quote = state
        if char == "\\":
            masked[index] = " "
            if index + 1 < len(source):
                if source[index + 1] != "\n":
                    masked[index + 1] = " "
                index += 2
            else:
                index += 1
        elif char == quote:
            masked[index] = " "
            index += 1
            state = "code"
        else:
            if char != "\n":
                masked[index] = " "
            index += 1
    return "".join(masked)


def _matching_delimiter(source, opening, left, right):
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == left:
            depth += 1
        elif source[index] == right:
            depth -= 1
            if depth == 0:
                return index
    return None


def _function_regions(masked):
    """Return source spans for complete RECOMP_FUNC definitions."""
    regions = []
    for macro in RECOMP_FUNC.finditer(masked):
        paren = masked.find("(", macro.end())
        brace = masked.find("{", macro.end())
        if paren < 0 or brace < 0:
            continue
        close_paren = _matching_delimiter(masked, paren, "(", ")")
        if (
            close_paren is None
            or brace < close_paren
            or 0 <= masked.find(";", close_paren, brace)
        ):
            continue
        name_matches = list(IDENTIFIER.finditer(masked[macro.end():paren]))
        if not name_matches:
            continue
        name_match = name_matches[-1]
        name = name_match.group()
        close_brace = _matching_delimiter(masked, brace, "{", "}")
        if close_brace is None:
            continue
        regions.append(
            {
                "name": name,
                "start": macro.start(),
                "body_start": brace + 1,
                "body_end": close_brace,
                "end": close_brace + 1,
            }
        )
    return regions


def _line_number(line_starts, offset):
    return bisect.bisect_right(line_starts, offset)


def _is_local_function_declaration(masked, match, region):
    """Identify block-scope prototypes rather than invocations."""
    closing_paren = _matching_delimiter(masked, match.end() - 1, "(", ")")
    if closing_paren is None:
        return False
    following = closing_paren + 1
    while following < region["body_end"] and masked[following].isspace():
        following += 1
    if following >= region["body_end"] or masked[following] != ";":
        return False

    boundary_start = region["body_start"] - 1
    boundary = max(
        masked.rfind(";", boundary_start, match.start()),
        masked.rfind("{", boundary_start, match.start()),
        masked.rfind("}", boundary_start, match.start()),
    )
    prefix = masked[boundary + 1:match.start()].strip()
    if not prefix or re.fullmatch(
        r"(?:[A-Za-z_]\w*|\*)(?:\s*(?:[A-Za-z_]\w*|\*))*", prefix
    ) is None:
        return False
    identifiers = re.findall(r"[A-Za-z_]\w*", prefix)
    return not any(
        identifier in DECLARATION_EXPRESSION_KEYWORDS for identifier in identifiers
    )


def _nearby_jal(source_lines, line_number, expected_target):
    first = max(1, line_number - 6)
    for candidate_line in range(line_number - 1, first - 1, -1):
        match = JAL_COMMENT.match(source_lines[candidate_line - 1])
        if match and match.group(2).lower() == expected_target:
            return {
                "metadata_status": "found",
                "nearby_jal_address": match.group(1).lower(),
                "jal_target_address": match.group(2).lower(),
            }
    return {
        "metadata_status": "not_found",
        "nearby_jal_address": None,
        "jal_target_address": None,
    }


def _target_address(function_name):
    match = re.search(r"(?:^|_)([0-9A-Fa-f]{8})$", function_name)
    return f"0x{match.group(1).lower()}" if match else None


def _excerpt(source_lines, call_line, before=10, after=2, max_chars=1800):
    start = max(1, call_line - before)
    end = min(len(source_lines), call_line + after)
    excerpt = "\n".join(
        f"{number}: {source_lines[number - 1]}" for number in range(start, end + 1)
    )
    if len(excerpt) > max_chars:
        excerpt = excerpt[-max_chars:]
        first_newline = excerpt.find("\n")
        if first_newline >= 0:
            excerpt = "[excerpt truncated]\n" + excerpt[first_newline + 1:]
    return {"start_line": start, "end_line": end, "text": excerpt}


def _source_call(source, path, digest, target, offset, region):
    source_lines = source.splitlines()
    line_starts = [0]
    for match in re.finditer("\n", source):
        line_starts.append(match.end())
    line_number = _line_number(line_starts, offset)
    return {
        "target": target,
        "source_path": str(path),
        "source_sha256": digest,
        "line": line_number,
        "enclosing_recomp_func": region["name"],
        **_nearby_jal(
            source_lines,
            line_number,
            TARGET_JAL_ADDRESSES[target],
        ),
        "argument_setup_excerpt": _excerpt(source_lines, line_number),
    }


def _direct_callers(target_name, sources):
    callers = []
    pattern = re.compile(rf"\b{re.escape(target_name)}\s*\(")
    for item in sources:
        for match in pattern.finditer(item["masked"]):
            region = next(
                (
                    candidate
                    for candidate in item["regions"]
                    if candidate["body_start"] <= match.start() < candidate["body_end"]
                ),
                None,
            )
            if region is None:
                continue
            if _is_local_function_declaration(item["masked"], match, region):
                continue
            line = _line_number(item["line_starts"], match.start())
            callers.append(
                {
                    "source_path": str(item["path"]),
                    "source_sha256": item["sha256"],
                    "line": line,
                    "enclosing_recomp_func": region["name"],
                    **(
                        _nearby_jal(
                            item["source"].splitlines(),
                            line,
                            _target_address(target_name),
                        )
                        if _target_address(target_name)
                        else {
                            "metadata_status": "unavailable_target_address",
                            "nearby_jal_address": None,
                            "jal_target_address": None,
                        }
                    ),
                }
            )
    callers.sort(key=lambda caller: (caller["source_path"], caller["line"]))
    return callers


def _scan_source_directory(source_directory):
    root = Path(source_directory)
    if not root.is_dir():
        raise AuditError(f"Source directory does not exist or is not a directory: {root}")

    source_paths = sorted(
        (path for path in root.rglob("*.c") if path.is_file()),
        key=lambda path: str(path.resolve()),
    )
    canonical_paths = tuple(path.resolve() for path in source_paths)
    sources = []
    for source_path in canonical_paths:
        try:
            data = source_path.read_bytes()
            source = data.decode("utf-8")
        except (OSError, UnicodeError) as error:
            raise AuditError(
                f"Unable to read UTF-8 source file {source_path}: {error}",
                canonical_paths,
            ) from error

        digest = hashlib.sha256(data).hexdigest()
        masked = _mask_comments_and_literals(source)
        line_starts = [0]
        line_starts.extend(match.end() for match in re.finditer("\n", source))
        sources.append(
            {
                "path": source_path,
                "sha256": digest,
                "source": source,
                "masked": masked,
                "line_starts": line_starts,
                "regions": _function_regions(masked),
            }
        )

    source_inventory = [
        {"source_path": str(item["path"]), "source_sha256": item["sha256"]}
        for item in sources
    ]
    calls = []
    for item in sources:
        for target in TARGETS:
            for match in CALL_PATTERNS[target].finditer(item["masked"]):
                region = next(
                    (
                        candidate
                        for candidate in item["regions"]
                        if candidate["body_start"] <= match.start() < candidate["body_end"]
                    ),
                    None,
                )
                if region is None:
                    continue
                if _is_local_function_declaration(item["masked"], match, region):
                    continue
                calls.append(
                    _source_call(
                        item["source"],
                        item["path"],
                        item["sha256"],
                        target,
                        match.start(),
                        region,
                    )
                )
    calls.sort(
        key=lambda call: (
            call["source_path"],
            call["line"],
            TARGETS.index(call["target"]),
        )
    )
    for call in calls:
        callers = _direct_callers(call["enclosing_recomp_func"], sources)
        call["direct_callers"] = callers
        call["direct_caller_status"] = "found" if callers else "unresolved"

    return {
        "schema_version": 1,
        "source_root": str(root.resolve()),
        "source_file_count": len(sources),
        "source_files": source_inventory,
        "calls": calls,
        "coin_progress": "UNKNOWN",
        "circuit_progress": "UNKNOWN",
        "gameplay_triggers": "UNKNOWN",
        "errors": [],
    }, canonical_paths


def _base_error_report(message, legacy_requests=None):
    return {
        "schema_version": 1,
        "source_root": None,
        "source_file_count": 0,
        "source_files": [],
        "calls": [],
        "coin_progress": "UNKNOWN",
        "circuit_progress": "UNKNOWN",
        "gameplay_triggers": "UNKNOWN",
        "legacy_requests": legacy_requests or {},
        "errors": [message],
    }


def _reject_report_aliases(report_path, source_paths):
    try:
        report_resolved = Path(report_path).resolve()
    except (OSError, RuntimeError) as error:
        raise ReportDestinationError(
            f"Unable to resolve report destination {report_path}: {error}"
        ) from error
    for source_path in source_paths:
        try:
            aliases = report_resolved == source_path or os.path.samefile(
                report_resolved, source_path
            )
        except FileNotFoundError:
            aliases = report_resolved == source_path
        except OSError as error:
            raise ReportDestinationError(
                f"Unable to safely compare report destination {report_path} "
                f"with scanned source {source_path}: {error}"
            ) from error
        if aliases:
            raise ReportDestinationError(
                f"Report destination {report_path} aliases scanned source "
                f"{source_path}; refusing to overwrite source input."
            )


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source-dir",
        help="Explicit RecompiledFuncs-probe source directory to scan recursively",
    )
    parser.add_argument("--trace-coin", help="Unsupported legacy coin-progress trace request")
    parser.add_argument(
        "--trace-circuit", help="Unsupported legacy Circuit-progress trace request"
    )
    parser.add_argument("--save-report", help="Save JSON inventory to this path")
    args = parser.parse_args(argv)

    legacy_requests = {}
    if args.trace_coin is not None:
        legacy_requests["trace_coin"] = {"status": "unsupported"}
    if args.trace_circuit is not None:
        legacy_requests["trace_circuit"] = {"status": "unsupported"}

    source_paths = ()
    exit_code = 0
    if args.source_dir:
        try:
            report, source_paths = _scan_source_directory(args.source_dir)
        except AuditError as error:
            source_paths = error.source_files
            report = _base_error_report(str(error), legacy_requests)
            exit_code = 2
    else:
        report = _base_error_report(
            "An explicit --source-dir is required for source-call inventory.",
            legacy_requests,
        )
        exit_code = 2

    if legacy_requests:
        report["legacy_requests"] = legacy_requests
        for request in legacy_requests:
            report["errors"].append(
                f"{request.replace('_', '-')} is unsupported; gameplay progress remains UNKNOWN."
            )
        exit_code = 2

    if args.save_report:
        try:
            _reject_report_aliases(args.save_report, source_paths)
            Path(args.save_report).write_text(
                json.dumps(report, indent=2) + "\n",
                encoding="utf-8",
            )
        except (OSError, ReportDestinationError) as error:
            message = f"Unable to save audit report to {args.save_report}: {error}"
            report["report_write_error"] = message
            report["errors"].append(message)
            print(message, file=sys.stderr)
            exit_code = 2

    json.dump(report, sys.stdout, indent=2)
    sys.stdout.write("\n")
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
