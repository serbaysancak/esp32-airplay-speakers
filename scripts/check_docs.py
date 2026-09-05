#!/usr/bin/env python3
"""Harman Kardom repository integrity checker.

Validates what the project claims about itself: that wiki links resolve, that
notes carry the frontmatter the contract requires, that ADR statuses use the
agreed vocabulary, and that decisions locked by an ADR are not silently
contradicted somewhere else in the vault.

This is a documentation check. It never asserts that a physical gate passed.

Usage:
    python3 scripts/check_docs.py            # report problems, exit 1 if any
    python3 scripts/check_docs.py --quiet    # only print the summary
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# Third-party and generated trees. managed_components holds vendored ESP-IDF
# components whose documentation is not ours to police.
SKIP_DIRS = {".git", ".obsidian", "node_modules", "generated",
             "managed_components", "build",
             # Virtual environments carry their own packaged READMEs, which are
             # not this project's notes and would be judged against a
             # frontmatter contract they never agreed to.
             ".venv", "venv", "site-packages", "__pycache__"}

# The vault frontmatter contract applies to notes under docs/ only. Agent and
# skill definitions follow their own tool-defined schema, checked separately.
VAULT_ROOT = "docs"
FRONTMATTER_EXEMPT = {"README.md"}
# Templates ship YYYY-MM-DD placeholders on purpose.
TEMPLATE_DIR = "docs/templates"

# Tool-defined agent and skill files: these need name + description, nothing else.
AGENT_GLOBS = (".claude/agents/*.md", ".cursor/agents/*.md")
SKILL_GLOB = ".agents/skills/*/SKILL.md"

REQUIRED_FRONTMATTER = ("status", "owner", "updated")
ADR_STATUSES = {"proposed", "accepted", "superseded", "rejected"}

# `\|` is the Obsidian escape for an alias pipe inside a markdown table cell.
WIKILINK = re.compile(r"\[\[([^\]|#\\]+)\\?(?:#[^\]|\\]*)?\\?(?:\|[^\]]*)?\]\]")
FRONTMATTER = re.compile(r"\A---\n(.*?)\n---\n", re.DOTALL)
DATE = re.compile(r"^\d{4}-\d{2}-\d{2}$")

# Canonical values locked by an accepted ADR. A hit outside `allowed` means the
# vault contradicts a decision, which is exactly how an agent gets misled.
@dataclass(frozen=True)
class Drift:
    label: str
    pattern: str
    allowed: tuple[str, ...]
    hint: str


DRIFT_RULES = (
    Drift(
        label="board-variant",
        pattern=r"\bN8R8\b",
        allowed=(
            "docs/07-decisions/ADR-0010-esp32-s3-n16r8-board.md",
            "docs/07-decisions/ADR-0012-n8r2-bringup-target.md",
            "docs/02-hardware/board-and-pin-selection.md",
            "docs/05-procurement/bom.md",
            "docs/05-procurement/suppliers.md",
            "docs/05-procurement/research-log.md",
            "docs/08-development-log/",
            "scripts/check_docs.py",
        ),
        hint="ADR-0010 locks the board to N16R8. N8R8 may only appear as an explicit rejected/backup alternative.",
    ),
    Drift(
        label="devkit-variant",
        pattern=r"\bN8R2\b",
        allowed=(
            "docs/07-decisions/ADR-0012-n8r2-bringup-target.md",
            "docs/07-decisions/ADR-0013-airplay-integration-shape.md",
            "docs/07-decisions/README.md",
            "docs/06-testing/devkit-bring-up.md",
            "docs/06-testing/test-log.md",
            "docs/08-development-log/",
            "firmware/README.md",
            "scripts/check_docs.py",
        ),
        hint=("N8R2 is the bring-up devkit of ADR-0012, never the product board. "
              "It may only appear where that distinction is being made."),
    ),
    Drift(
        label="charge-source",
        pattern=r"16[,.]8\s*V\s*CC/CV\s*(şarj\s*)?adapt",
        allowed=(
            "docs/07-decisions/ADR-0009-usb-c-pd-charge-chain.md",
            "docs/power-and-battery-plan.md",
            "docs/05-procurement/",
            "docs/08-development-log/",
            "scripts/check_docs.py",
        ),
        hint="ADR-0009 locks the V1 charge chain to USB-C PD -> 20 V trigger -> XL4015 CC/CV. A ready-made brick is the documented backup only.",
    ),
)

# Claims the contract forbids stating as fact.
FORBIDDEN_CLAIMS = (
    (r"AirPlay\s*2[^.\n]{0,40}(destekleniyor|doğrulandı|kanıtlandı)",
     "AirPlay 2 multiroom support is not confirmed (ADR-0007)."),
    (r"(woofer|tweeter)[^.\n]{0,40}\b8\s*(ohm|Ω)\b[^.\n]{0,20}(olduğu|doğrulandı|kesin)",
     "Individual driver impedance is not measured (G0)."),
)


@dataclass
class Report:
    errors: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)

    def error(self, path: Path, message: str) -> None:
        self.errors.append(f"{path.relative_to(ROOT)}: {message}")

    def warn(self, path: Path, message: str) -> None:
        self.warnings.append(f"{path.relative_to(ROOT)}: {message}")


def markdown_files() -> list[Path]:
    return sorted(
        p for p in ROOT.rglob("*.md")
        if not SKIP_DIRS.intersection(p.relative_to(ROOT).parts)
    )


def parse_frontmatter(text: str) -> dict[str, str] | None:
    match = FRONTMATTER.match(text)
    if not match:
        return None
    fields: dict[str, str] = {}
    for line in match.group(1).splitlines():
        if line.startswith((" ", "-", "\t")) or ":" not in line:
            continue
        key, _, value = line.partition(":")
        fields[key.strip()] = value.strip()
    return fields


def check_wikilinks(path: Path, text: str, stems: dict[str, list[Path]], report: Report) -> None:
    for match in WIKILINK.finditer(text):
        target = match.group(1).strip()
        if (path.parent / f"{target}.md").exists():
            continue
        if (ROOT / f"{target}.md").exists():
            continue
        if (ROOT / "docs" / f"{target}.md").exists():
            continue
        if target.split("/")[-1] in stems:
            continue
        report.error(path, f"wiki link target not found: [[{target}]]")


def check_frontmatter(path: Path, text: str, report: Report) -> None:
    relative = path.relative_to(ROOT)
    if relative.parts[0] != VAULT_ROOT:
        return
    if path.name in FRONTMATTER_EXEMPT:
        return
    fields = parse_frontmatter(text)
    if fields is None:
        report.warn(path, "no frontmatter block")
        return
    for key in REQUIRED_FRONTMATTER:
        if key not in fields:
            report.error(path, f"frontmatter missing '{key}'")
            continue
    updated = fields.get("updated", "")
    if str(relative.parent) == TEMPLATE_DIR:
        return
    if updated and not DATE.match(updated):
        report.error(path, f"frontmatter 'updated' is not YYYY-MM-DD: {updated!r}")
    if path.parent.name == "07-decisions" and path.name.startswith("ADR-"):
        status = fields.get("status", "")
        if status not in ADR_STATUSES:
            report.error(path, f"ADR status {status!r} outside {sorted(ADR_STATUSES)}")


def check_adr_index(report: Report) -> None:
    index = ROOT / "docs/07-decisions/README.md"
    listed = set(WIKILINK.findall(index.read_text(encoding="utf-8")))
    for adr in sorted((ROOT / "docs/07-decisions").glob("ADR-*.md")):
        if adr.stem not in listed:
            report.error(index, f"{adr.stem} is not listed in the ADR index")


def check_drift(path: Path, text: str, report: Report) -> None:
    relative = str(path.relative_to(ROOT))
    for rule in DRIFT_RULES:
        if any(relative.startswith(prefix) for prefix in rule.allowed):
            continue
        if re.search(rule.pattern, text, re.IGNORECASE):
            report.error(path, f"[{rule.label}] {rule.hint}")


def check_forbidden_claims(path: Path, text: str, report: Report) -> None:
    for pattern, message in FORBIDDEN_CLAIMS:
        if re.search(pattern, text, re.IGNORECASE):
            report.error(path, f"unverified claim: {message}")


def check_agent_definitions(report: Report) -> None:
    """Agent and skill files use the tool schema: a name and a description."""
    targets: list[Path] = []
    for pattern in AGENT_GLOBS:
        targets.extend(sorted(ROOT.glob(pattern)))
    targets.extend(sorted(ROOT.glob(SKILL_GLOB)))
    for path in targets:
        fields = parse_frontmatter(path.read_text(encoding="utf-8"))
        if fields is None:
            report.error(path, "agent/skill definition has no frontmatter")
            continue
        for key in ("name", "description"):
            if not fields.get(key):
                report.error(path, f"agent/skill frontmatter missing '{key}'")
        name = fields.get("name", "")
        if name and name != path.stem and path.name != "SKILL.md":
            report.error(path, f"frontmatter name {name!r} does not match filename {path.stem!r}")


def check_ambiguous_stems(files: list[Path], stems: dict[str, list[Path]], report: Report) -> None:
    """Only complain about a duplicated stem if something links to it bare."""
    used: set[str] = set()
    for path in files:
        for target in WIKILINK.findall(path.read_text(encoding="utf-8")):
            if "/" not in target:
                used.add(target.strip())
    for stem in sorted(used):
        paths = stems.get(stem, [])
        if len(paths) > 1:
            joined = ", ".join(str(p.relative_to(ROOT)) for p in paths)
            report.warn(paths[0], f"bare wiki link {stem!r} is ambiguous between: {joined}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--quiet", action="store_true", help="only print the summary line")
    args = parser.parse_args()

    files = markdown_files()
    stems: dict[str, list[Path]] = {}
    for path in files:
        stems.setdefault(path.stem, []).append(path)

    report = Report()
    for path in files:
        text = path.read_text(encoding="utf-8")
        check_wikilinks(path, text, stems, report)
        check_frontmatter(path, text, report)
        check_drift(path, text, report)
        check_forbidden_claims(path, text, report)
    check_adr_index(report)

    check_agent_definitions(report)
    check_ambiguous_stems(files, stems, report)

    if not args.quiet:
        for line in report.errors:
            print(f"ERROR   {line}")
        for line in report.warnings:
            print(f"WARN    {line}")

    print(f"check_docs: {len(files)} files, {len(report.errors)} errors, {len(report.warnings)} warnings")
    return 1 if report.errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
