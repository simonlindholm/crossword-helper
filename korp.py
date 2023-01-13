#!/usr/bin/env python3
import argparse
import json
import os
import subprocess
import sys

DEFAULT_CONTEXT = 50
SENTENCE_SEPARATORS = set(".?!")
DIM = "\033[90m"  # "\033[2m"
RESET = "\033[0m"
COLOR = "\033[91m"

def main():
    dir_path = os.path.dirname(os.path.realpath(__file__))
    corpus = os.path.join(dir_path, "korpus")

    parser = argparse.ArgumentParser(
        description="Search corpus."
    )
    parser.add_argument(
        "regex",
        help="rg-compatible regex to search for",
    )
    parser.add_argument(
        "-u",
        "--unrestricted",
        dest="limit_to_word",
        action="store_false",
        help="raw regex; don't restrict to word boundaries",
    )
    parser.add_argument(
        "-I",
        "--no-ignore-case",
        dest="ignore_case",
        action="store_false",
        help="don't ignore letter case (upper vs lower)",
    )
    parser.add_argument(
        "--hidden",
        dest="hidden",
        action="store_const",
        const=1,
        help="find hidden word (overrides -u)",
    )
    parser.add_argument(
        "--very-hidden",
        dest="hidden",
        action="store_const",
        const=2,
        help="find hidden word, split over several (overrides -u)",
    )
    parser.add_argument(
        "-c",
        "--context",
        dest="context",
        metavar="<NUM>",
        type=int,
        default=DEFAULT_CONTEXT,
        help="number of characters to show before/after each match",
    )
    parser.add_argument(
        "-f",
        "--file",
        dest="file",
        metavar="<FILE>",
        help="corpus file",
    )
    args = parser.parse_args()

    context = args.context
    regex = args.regex
    if args.file:
        corpus = args.file
    if args.hidden:
        regex = r"\w" + " ?".join(regex) + r"\w"
    rg_args = ["--line-buffered", "--json"]
    if args.limit_to_word and not args.hidden:
        rg_args.append("--word-regexp")
    if args.ignore_case:
        rg_args.append("--ignore-case")
    rg_args += ["--", regex, corpus]

    corpus_f = open(corpus, "rb")
    use_color = sys.stdout.isatty()

    proc = subprocess.Popen(["rg"] + rg_args, stdout=subprocess.PIPE)
    for line in iter(proc.stdout.readline, b''):
        data = json.loads(line)
        if data["type"] != "match":
            continue
        data = data["data"]
        line_no = data["line_number"]
        base_offset = data["absolute_offset"]
        for m in data["submatches"]:
            start = base_offset + m["start"]
            end = base_offset + m["end"]

            context_bytes = context * 4  # worst-case in utf-8
            full_start = max(0, start - context_bytes)
            full_end = end + context_bytes + 1
            corpus_f.seek(full_start)
            full_bytes = corpus_f.read(full_end - full_start)

            # trim to complete utf-8
            if len(full_bytes) == full_end - full_start:
                while full_bytes and full_bytes[-1] & 0xC0 == 0x80:
                    full_bytes = full_bytes[:-1]
                full_bytes = full_bytes[:-1]
            trim_start = 0
            while full_bytes and full_bytes[0] & 0xC0 == 0x80:
                full_bytes = full_bytes[1:]
                trim_start += 1

            full_bytes = full_bytes.replace(b"\n", b" ")

            word_start = start - full_start - trim_start
            word_end = word_start + (end - start)
            context_before = full_bytes[: word_start].decode("utf-8")[-context: ].rjust(context)
            word = full_bytes[word_start : word_end].decode("utf-8")
            context_after = full_bytes[word_end :].decode("utf-8")[: context].ljust(context)
            if args.hidden:
                context_before = context_before[1:] + word[0]
                context_after = word[-1] + context_after[:-1]
                word = word[1:-1]
                if args.hidden == 2 and " " not in word:
                    continue
            if use_color:
                word = COLOR + word + RESET
                before_sep = next(
                    (
                        i + 1
                        for i in range(len(context_before) - 1, 0, -1)
                        if context_before[i] == ' '
                        and context_before[i - 1] in SENTENCE_SEPARATORS
                    ),
                    0
                )
                after_sep = next(
                    (
                        i + 1
                        for i, c in enumerate(context_after)
                        if c in SENTENCE_SEPARATORS
                        and (i + 1 == len(context_after) or context_after[i+1] == ' ')
                    ),
                    len(context_after)
                )
                context_before = DIM + context_before[:before_sep] + RESET + context_before[before_sep:]
                context_after = context_after[:after_sep] + DIM + context_after[after_sep:] + RESET
            print(context_before + word + context_after)

try:
    main()
except KeyboardInterrupt:
    print("\033[0m", end="")
