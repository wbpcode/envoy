#!/usr/bin/env python3

import argparse
import functools
import logging
import multiprocessing
import os
import pathlib
import re
import subprocess
import stat
import sys
import traceback
import shutil
from functools import cached_property
from typing import Callable, Dict, List, Pattern, Tuple, Union

# The way this script is currently used (ie no bazel) it relies on system deps.
# As `pyyaml` is present in `envoy-build-ubuntu` it should be safe to use here.
import yaml

import paths

logger = logging.getLogger(__name__)


class FormatConfig:
    """Provides a format config object based on parsed YAML config."""

    def __init__(self, path: str) -> None:
        self.path = path

    def __getitem__(self, k):
        return self.config.__getitem__(k)

    @cached_property
    def buildifier_path(self) -> str:
        """Path to the buildifer binary."""
        return paths.get_buildifier()

    @cached_property
    def buildozer_path(self) -> str:
        """Path to the buildozer binary."""
        return paths.get_buildozer()

    @cached_property
    def clang_format_path(self) -> str:
        """Path to the clang-format binary."""
        return os.getenv("CLANG_FORMAT", "clang-format-14")

    @cached_property
    def config(self) -> Dict:
        """Parsed YAML config."""
        # TODO(phlax): Ensure the YAML is valid/well-formed."""
        return yaml.safe_load(pathlib.Path(self.path).read_text())

    @cached_property
    def dir_order(self) -> List[str]:
        """Expected order of includes in code."""
        return self["dir_order"]

    @cached_property
    def paths(self) -> Dict[str, Union[Tuple[str, ...], Dict[str, Tuple[str, ...]]]]:
        """Mapping of named paths."""
        paths = self._normalize("paths", cb=lambda paths: tuple(f"./{p}" for p in paths))
        paths["build_fixer_py"] = self._build_fixer_path
        paths["header_order_py"] = self._header_order_path
        return paths

    @cached_property
    def re(self) -> Dict[str, Pattern[str]]:
        """Mapping of named regular expressions."""
        return {k: re.compile(v) for k, v in self["re"].items()}

    @cached_property
    def re_multiline(self) -> Dict[str, Pattern[str]]:
        """Mapping of named multi-line regular expressions."""
        return {k: re.compile(v, re.MULTILINE) for k, v in self["re_multiline"].items()}

    @cached_property
    def replacements(self) -> Dict[str, str]:
        """Mapping of subsitutions to be replaced in code."""
        return self["replacements"]

    @cached_property
    def suffixes(self) -> Dict[str, Union[Tuple[str, ...], Dict[str, Tuple[str, ...]]]]:
        """Mapping of named file suffixes for target files."""
        return self._normalize("suffixes")

    @property
    def _build_fixer_path(self) -> str:
        return os.path.join(os.path.dirname(os.path.abspath(sys.argv[0])), "envoy_build_fixer.py")

    @property
    def _header_order_path(self) -> str:
        return os.path.join(os.path.dirname(os.path.abspath(sys.argv[0])), "header_order.py")

    def _normalize(
            self,
            config_type: str,
            cb: Callable = tuple) -> Dict[str, Union[Tuple[str, ...], Dict[str, Tuple[str, ...]]]]:
        config = {}
        for k, v in self[config_type].items():
            if isinstance(v, dict):
                config[k] = {}
                for key in ("include", "exclude"):
                    if key in v:
                        config[k][key] = cb(v[key])
            else:
                config[k] = cb(v)
        return config


class FormatChecker:

    def __init__(self, args):
        self.args = args
        self.config_path = args.config_path
        self.operation_type = args.operation_type
        self.target_path = args.target_path
        self.api_prefix = args.api_prefix
        self.envoy_build_rule_check = not args.skip_envoy_build_rule_check
        self._include_dir_order = args.include_dir_order

    @cached_property
    def build_fixer_check_excluded_paths(self):
        return (
            tuple(self.args.build_fixer_check_excluded_paths)
            + self.config.paths["build_fixer"]["exclude"])

    @cached_property
    def config(self) -> FormatConfig:
        return FormatConfig(self.config_path)

    @cached_property
    def include_dir_order(self):
        return ",".join(
            self._include_dir_order if self._include_dir_order else self.config["dir_order"])

    @property
    def namespace_check(self):
        return self.args.namespace_check

    @cached_property
    def namespace_check_excluded_paths(self):
        return (
            tuple(self.args.namespace_check_excluded_paths)
            + self.config.paths["namespace_check"]["exclude"])

    @cached_property
    def namespace_re(self):
        return re.compile("^\s*namespace\s+%s\s*{" % self.namespace_check, re.MULTILINE)

    # Map a line transformation function across each line of a file,
    # writing the result lines as requested.
    # If there is a clang format nesting or mismatch error, return the first occurrence
    def evaluate_lines(self, path, line_xform, write=True):
        error_message = None
        format_flag = True
        output_lines = []
        for line_number, line in enumerate(self.read_lines(path)):
            if line.find("// clang-format off") != -1:
                if not format_flag and error_message is None:
                    error_message = "%s:%d: %s" % (path, line_number + 1, "clang-format nested off")
                format_flag = False
            if line.find("// clang-format on") != -1:
                if format_flag and error_message is None:
                    error_message = "%s:%d: %s" % (path, line_number + 1, "clang-format nested on")
                format_flag = True
            if format_flag:
                output_lines.append(line_xform(line, line_number))
            else:
                output_lines.append(line)
        # We used to use fileinput in the older Python 2.7 script, but this doesn't do
        # inplace mode and UTF-8 in Python 3, so doing it the manual way.
        if write:
            pathlib.Path(path).write_text('\n'.join(output_lines), encoding='utf-8')
        if not format_flag and error_message is None:
            error_message = "%s:%d: %s" % (path, line_number + 1, "clang-format remains off")
        return error_message

    # Obtain all the lines in a given file.
    def read_lines(self, path):
        with open(path) as f:
            for l in f:
                yield l[:-1]
        yield ""

    # Read a UTF-8 encoded file as a str.
    def read_file(self, path):
        return pathlib.Path(path).read_text(encoding='utf-8')

    # look_path searches for the given executable in all directories in PATH
    # environment variable. If it cannot be found, empty string is returned.
    def look_path(self, executable):
        if executable is None:
            return ''
        return shutil.which(executable) or ''

    # path_exists checks whether the given path exists. This function assumes that
    # the path is absolute and evaluates environment variables.
    def path_exists(self, executable):
        if executable is None:
            return False
        return os.path.exists(os.path.expandvars(executable))

    # executable_by_others checks whether the given path has execute permission for
    # others.
    def executable_by_others(self, executable):
        st = os.stat(os.path.expandvars(executable))
        return bool(st.st_mode & stat.S_IXOTH)

    # Check whether all needed external tools (clang-format, buildifier, buildozer) are
    # available.
    def check_tools(self):
        error_messages = []

        clang_format_abs_path = self.look_path(self.config.clang_format_path)
        if clang_format_abs_path:
            if not self.executable_by_others(clang_format_abs_path):
                error_messages.append(
                    "command {} exists, but cannot be executed by other "
                    "users".format(self.config.clang_format_path))
        else:
            error_messages.append(
                "Command {} not found. If you have clang-format in version 12.x.x "
                "installed, but the binary name is different or it's not available in "
                "PATH, please use CLANG_FORMAT environment variable to specify the path. "
                "Examples:\n"
                "    export CLANG_FORMAT=clang-format-14.0.0\n"
                "    export CLANG_FORMAT=/opt/bin/clang-format-14\n"
                "    export CLANG_FORMAT=/usr/local/opt/llvm@14/bin/clang-format".format(
                    self.config.clang_format_path))

        def check_bazel_tool(name, path, var):
            bazel_tool_abs_path = self.look_path(path)
            if bazel_tool_abs_path:
                if not self.executable_by_others(bazel_tool_abs_path):
                    error_messages.append(
                        "command {} exists, but cannot be executed by other "
                        "users".format(path))
            elif self.path_exists(path):
                if not self.executable_by_others(path):
                    error_messages.append(
                        "command {} exists, but cannot be executed by other "
                        "users".format(path))
            else:

                error_messages.append(
                    "Command {} not found. If you have {} installed, but the binary "
                    "name is different or it's not available in $GOPATH/bin, please use "
                    "{} environment variable to specify the path. Example:\n"
                    "    export {}=`which {}`\n"
                    "If you don't have {} installed, you can install it by:\n"
                    "    go get -u github.com/bazelbuild/buildtools/{}".format(
                        path, name, var, var, name, name, name))

        check_bazel_tool('buildifier', self.config.buildifier_path, 'BUILDIFIER_BIN')
        check_bazel_tool('buildozer', self.config.buildozer_path, 'BUILDOZER_BIN')

        return error_messages

    def check_namespace(self, file_path):
        for excluded_path in self.namespace_check_excluded_paths:
            if file_path.startswith(excluded_path):
                return []

        nolint = "NOLINT(namespace-%s)" % self.namespace_check.lower()
        text = self.read_file(file_path)
        if not self.namespace_re.search(text) and not nolint in text:
            return [
                "Unable to find %s namespace or %s for file: %s" %
                (self.namespace_check, nolint, file_path)
            ]
        return []

    # To avoid breaking the Lyft import, we just check for path inclusion here.
    def allow_listed_for_protobuf_deps(self, file_path):
        return (
            file_path.endswith(self.config.suffixes["proto"])
            or file_path.endswith(self.config.suffixes["repositories_bzl"])
            or any(file_path.startswith(path) for path in self.config.paths["protobuf"]["include"]))

    # Real-world time sources should not be instantiated in the source, except for a few
    # specific cases. They should be passed down from where they are instantied to where
    # they need to be used, e.g. through the ServerInstance, Dispatcher, or ClusterManager.
    def allow_listed_for_realtime(self, file_path):
        if file_path.endswith(".md"):
            return True
        return file_path in self.config.paths["real_time"]["include"]

    def allow_listed_for_register_factory(self, file_path):
        if not file_path.startswith("./test/"):
            return True

        return any(
            file_path.startswith(prefix)
            for prefix in self.config.paths["register_factory_test"]["include"])

    def allow_listed_for_serialize_as_string(self, file_path):
        return file_path in self.config.paths["serialize_as_string"]["include"]

    def allow_listed_for_std_string_view(self, file_path):
        return file_path in self.config.paths["std_string_view"]["include"]

    def allow_listed_for_json_string_to_message(self, file_path):
        return file_path in self.config.paths["json_string_to_message"]["include"]

    def allow_listed_for_histogram_si_suffix(self, name):
        return name in self.config.suffixes["histogram_with_si"]["include"]

    def allow_listed_for_std_regex(self, file_path):
        return file_path.startswith(
            "./test") or file_path in self.config.paths["std_regex"]["include"]

    def allow_listed_for_grpc_init(self, file_path):
        return file_path in self.config.paths["grpc_init"]["include"]

    def allow_listed_for_unpack_to(self, file_path):
        return file_path.startswith("./test") or file_path in [
            "./source/common/protobuf/utility.cc", "./source/common/protobuf/utility.h"
        ]

    def allow_listed_for_raw_try(self, file_path):
        # TODO(chaoqin-li1123): Exclude some important extensions from ALLOWLIST.
        return file_path in self.config.paths["raw_try"]["include"] or file_path.startswith(
            "./source/extensions")

    def deny_listed_for_exceptions(self, file_path):
        # Returns true when it is a non test header file or the file_path is in DENYLIST or
        # it is under tools/testdata subdirectory.

        return (file_path.endswith('.h') and not file_path.startswith("./test/") and not file_path in self.config.paths["exception"]["include"]) or file_path in self.config.paths["exception"]["exclude"] \
            or self.is_in_subdir(file_path, 'tools/testdata')

    def allow_listed_for_build_urls(self, file_path):
        return file_path in self.config.paths["build_urls"]["include"]

    def is_api_file(self, file_path):
        return file_path.startswith(self.api_prefix)

    def is_build_file(self, file_path):
        basename = os.path.basename(file_path)
        if basename in {"BUILD", "BUILD.bazel"} or basename.endswith(".BUILD"):
            return True
        return False

    def is_external_build_file(self, file_path):
        return self.is_build_file(file_path) and (
            file_path.startswith("./bazel/external/")
            or file_path.startswith("./tools/clang_tools"))

    def is_starlark_file(self, file_path):
        return file_path.endswith(".bzl")

    def is_workspace_file(self, file_path):
        return os.path.basename(file_path) == "WORKSPACE"

    def is_build_fixer_excluded_file(self, file_path):
        for excluded_path in self.build_fixer_check_excluded_paths:
            if file_path.startswith(excluded_path):
                return True
        return False

    def has_invalid_angle_bracket_directory(self, line):
        if not line.startswith(self.config["include_angle"]):
            return False
        path = line[len(self.config["include_angle"]):]
        slash = path.find("/")
        if slash == -1:
            return False
        subdir = path[0:slash]
        return subdir in self.config.dir_order

    # simple check that all flags are sorted.
    def check_runtime_flags(self, file_path, error_messages):
        previous_flag = ""
        for line_number, line in enumerate(self.read_lines(file_path)):
            if line.startswith("RUNTIME_GUARD"):
                match = self.config.re["runtime_guard_flag"].match(line)
                if not match:
                    error_messages.append("%s does not look like a reloadable flag" % line)
                    break

                if previous_flag:
                    if line < previous_flag and match.groups(
                    )[0] not in self.config["unsorted_flags"]:
                        error_messages.append(
                            "%s and %s are out of order\n" % (line, previous_flag))
                previous_flag = line

    def check_file_contents(self, file_path, checker):
        error_messages = []
        if file_path.endswith("source/common/runtime/runtime_features.cc"):
            # Do runtime alphabetical order checks.
            self.check_runtime_flags(file_path, error_messages)

        def check_format_errors(line, line_number):

            def report_error(message):
                error_messages.append("%s:%d: %s" % (file_path, line_number + 1, message))

            checker(line, file_path, report_error)

        evaluate_failure = self.evaluate_lines(file_path, check_format_errors, False)
        if evaluate_failure is not None:
            error_messages.append(evaluate_failure)

        return error_messages

    def fix_source_line(self, line, line_number):
        # Strip double space after '.'  This may prove overenthusiastic and need to
        # be restricted to comments and metadata files but works for now.
        line = self.config.re["dot_multi_space"].sub(". ", line)

        if self.has_invalid_angle_bracket_directory(line):
            line = line.replace("<", '"').replace(">", '"')

        # Fix incorrect protobuf namespace references.
        for invalid_construct, valid_construct in self.config.replacements[
                "protobuf_type_errors"].items():
            line = line.replace(invalid_construct, valid_construct)

        # Use recommended cpp stdlib
        for invalid_construct, valid_construct in self.config.replacements["libcxx"].items():
            line = line.replace(invalid_construct, valid_construct)

        # Fix code conventions violations.
        for invalid_construct, valid_construct in self.config.replacements["code_convention"].items(
        ):
            line = line.replace(invalid_construct, valid_construct)

        return line

    # We want to look for a call to condvar.waitFor, but there's no strong pattern
    # to the variable name of the condvar. If we just look for ".waitFor" we'll also
    # pick up time_system_.waitFor(...), and we don't want to return true for that
    # pattern. But in that case there is a strong pattern of using time_system in
    # various spellings as the variable name.
    def has_cond_var_wait_for(self, line):
        wait_for = line.find(".waitFor(")
        if wait_for == -1:
            return False
        preceding = line[0:wait_for]
        if preceding.endswith("time_system") or preceding.endswith("timeSystem()") or \
                preceding.endswith("time_system_"):
            return False
        return True

    def is_api_proto(self, file_path):
        return file_path.endswith(self.config.suffixes["proto"]) and self.is_api_file(file_path)

    # Determines whether the filename is either in the specified subdirectory, or
    # at the top level. We consider files in the top level for the benefit of
    # the check_format testcases in tools/testdata/check_format.
    def is_in_subdir(self, filename, *subdirs):
        # Skip this check for check_format's unit-tests.
        if filename.count("/") <= 1:
            return True
        for subdir in subdirs:
            if filename.startswith('./' + subdir + '/'):
                return True
        return False

    # Determines if given token exists in line without leading or trailing token characters
    # e.g. will return True for a line containing foo() but not foo_bar() or baz_foo
    def token_in_line(self, token, line):
        index = 0
        while True:
            index = line.find(token, index)
            # the following check has been changed from index < 1 to index < 0 because
            # this function incorrectly returns false when the token in question is the
            # first one in a line. The following line returns false when the token is present:
            # (no leading whitespace) violating_symbol foo;
            if index < 0:
                break
            if index == 0 or not (line[index - 1].isalnum() or line[index - 1] == '_'):
                if index + len(token) >= len(line) or not (line[index + len(token)].isalnum()
                                                           or line[index + len(token)] == '_'):
                    return True
            index = index + 1
        return False

    def check_source_line(self, line, file_path, report_error):
        # Check fixable errors. These may have been fixed already.
        if line.find(".  ") != -1:
            report_error("over-enthusiastic spaces")
        if self.is_in_subdir(file_path, 'source',
                             'include') and self.config.re["x_envoy_used_directly"].match(line):
            report_error(
                "Please do not use the raw literal x-envoy in source code.  See Envoy::Http::PrefixValue."
            )
        if self.has_invalid_angle_bracket_directory(line):
            report_error("envoy includes should not have angle brackets")
        for invalid_construct, valid_construct in self.config.replacements[
                "protobuf_type_errors"].items():
            if invalid_construct in line:
                report_error(
                    "incorrect protobuf type reference %s; "
                    "should be %s" % (invalid_construct, valid_construct))
        for invalid_construct, valid_construct in self.config.replacements["libcxx"].items():
            if invalid_construct in line:
                report_error(
                    "term %s should be replaced with standard library term %s" %
                    (invalid_construct, valid_construct))
        for invalid_construct, valid_construct in self.config.replacements["code_convention"].items(
        ):
            if invalid_construct in line:
                report_error(
                    "term %s should be replaced with preferred term %s" %
                    (invalid_construct, valid_construct))
        # Do not include the virtual_includes headers.
        if self.config.re["virtual_include_headers"].search(line):
            report_error("Don't include the virtual includes headers.")

        # Some errors cannot be fixed automatically, and actionable, consistent,
        # navigable messages should be emitted to make it easy to find and fix
        # the errors by hand.
        if not self.allow_listed_for_protobuf_deps(file_path):
            if '"google/protobuf' in line or "google::protobuf" in line:
                report_error(
                    "unexpected direct dependency on google.protobuf, use "
                    "the definitions in common/protobuf/protobuf.h instead.")
        if line.startswith("#include <mutex>") or line.startswith("#include <condition_variable"):
            # We don't check here for std::mutex because that may legitimately show up in
            # comments, for example this one.
            report_error(
                "Don't use <mutex> or <condition_variable*>, switch to "
                "Thread::MutexBasicLockable in source/common/common/thread.h")
        if line.startswith("#include <shared_mutex>"):
            # We don't check here for std::shared_timed_mutex because that may
            # legitimately show up in comments, for example this one.
            report_error("Don't use <shared_mutex>, use absl::Mutex for reader/writer locks.")
        if not self.allow_listed_for_realtime(
                file_path) and not "NO_CHECK_FORMAT(real_time)" in line:
            if "RealTimeSource" in line or \
              ("RealTimeSystem" in line and not "TestRealTimeSystem" in line) or \
              "std::chrono::system_clock::now" in line or "std::chrono::steady_clock::now" in line or \
              "std::this_thread::sleep_for" in line or " usleep(" in line or "::usleep(" in line:
                report_error(
                    "Don't reference real-world time sources; use TimeSystem::advanceTime(Wait|Async)"
                )
            if self.has_cond_var_wait_for(line):
                report_error(
                    "Don't use CondVar::waitFor(); use TimeSystem::waitFor() instead.  If this "
                    "already is TimeSystem::waitFor(), please name the TimeSystem variable "
                    "time_system or time_system_ so the linter can understand.")
        duration_arg = self.config.re["duration_value"].search(line)
        if duration_arg and duration_arg.group(1) != "0" and duration_arg.group(1) != "0.0":
            # Matching duration(int-const or float-const) other than zero
            report_error(
                "Don't use ambiguous duration(value), use an explicit duration type, e.g. Event::TimeSystem::Milliseconds(value)"
            )
        if not self.allow_listed_for_register_factory(file_path):
            if "Registry::RegisterFactory<" in line or "REGISTER_FACTORY" in line:
                report_error(
                    "Don't use Registry::RegisterFactory or REGISTER_FACTORY in tests, "
                    "use Registry::InjectFactory instead.")
        if not self.allow_listed_for_unpack_to(file_path):
            if "UnpackTo" in line:
                report_error(
                    "Don't use UnpackTo() directly, use MessageUtil::unpackToNoThrow() instead")
        # Check that we use the absl::Time library
        if self.token_in_line("std::get_time", line):
            if "test/" in file_path:
                report_error("Don't use std::get_time; use TestUtility::parseTime in tests")
            else:
                report_error("Don't use std::get_time; use the injectable time system")
        if self.token_in_line("std::put_time", line):
            report_error("Don't use std::put_time; use absl::Time equivalent instead")
        if self.token_in_line("gmtime", line):
            report_error("Don't use gmtime; use absl::Time equivalent instead")
        if self.token_in_line("mktime", line):
            report_error("Don't use mktime; use absl::Time equivalent instead")
        if self.token_in_line("localtime", line):
            report_error("Don't use localtime; use absl::Time equivalent instead")
        if self.token_in_line("strftime", line):
            report_error("Don't use strftime; use absl::FormatTime instead")
        if self.token_in_line("strptime", line):
            report_error("Don't use strptime; use absl::FormatTime instead")
        if self.token_in_line("strerror", line):
            report_error("Don't use strerror; use Envoy::errorDetails instead")
        # Prefer using abseil hash maps/sets over std::unordered_map/set for performance optimizations and
        # non-deterministic iteration order that exposes faulty assertions.
        # See: https://abseil.io/docs/cpp/guides/container#hash-tables
        if "std::unordered_map" in line:
            report_error(
                "Don't use std::unordered_map; use absl::flat_hash_map instead or "
                "absl::node_hash_map if pointer stability of keys/values is required")
        if "std::unordered_set" in line:
            report_error(
                "Don't use std::unordered_set; use absl::flat_hash_set instead or "
                "absl::node_hash_set if pointer stability of keys/values is required")
        if "std::atomic_" in line:
            # The std::atomic_* free functions are functionally equivalent to calling
            # operations on std::atomic<T> objects, so prefer to use that instead.
            report_error(
                "Don't use free std::atomic_* functions, use std::atomic<T> members instead.")
        # Block usage of certain std types/functions as iOS 11 and macOS 10.13
        # do not support these at runtime.
        # See: https://github.com/envoyproxy/envoy/issues/12341
        if self.token_in_line("std::any", line):
            report_error("Don't use std::any; use absl::any instead")
        if self.token_in_line("std::get_if", line):
            report_error("Don't use std::get_if; use absl::get_if instead")
        if self.token_in_line("std::holds_alternative", line):
            report_error("Don't use std::holds_alternative; use absl::holds_alternative instead")
        if self.token_in_line("std::make_optional", line):
            report_error("Don't use std::make_optional; use absl::make_optional instead")
        if self.token_in_line("std::monostate", line):
            report_error("Don't use std::monostate; use absl::monostate instead")
        if self.token_in_line("std::optional", line):
            report_error("Don't use std::optional; use absl::optional instead")
        if not self.allow_listed_for_std_string_view(
                file_path) and not "NOLINT(std::string_view)" in line:
            if self.token_in_line("std::string_view", line) or self.token_in_line("toStdStringView",
                                                                                  line):
                report_error(
                    "Don't use std::string_view or toStdStringView; use absl::string_view instead")
        if self.token_in_line("std::variant", line):
            report_error("Don't use std::variant; use absl::variant instead")
        if self.token_in_line("std::visit", line):
            report_error("Don't use std::visit; use absl::visit instead")
        if " try {" in line and file_path.startswith(
                "./source") and not self.allow_listed_for_raw_try(file_path):
            report_error(
                "Don't use raw try, use TRY_ASSERT_MAIN_THREAD if on the main thread otherwise don't use exceptions."
            )
        if "__attribute__((packed))" in line and file_path != "./envoy/common/platform.h":
            # __attribute__((packed)) is not supported by MSVC, we have a PACKED_STRUCT macro that
            # can be used instead
            report_error(
                "Don't use __attribute__((packed)), use the PACKED_STRUCT macro defined "
                "in envoy/common/platform.h instead")
        if self.config.re["designated_initializer"].search(line):
            # Designated initializers are not part of the C++14 standard and are not supported
            # by MSVC
            report_error(
                "Don't use designated initializers in struct initialization, "
                "they are not part of C++14")
        if " ?: " in line:
            # The ?: operator is non-standard, it is a GCC extension
            report_error("Don't use the '?:' operator, it is a non-standard GCC extension")
        if line.startswith("using testing::Test;"):
            report_error("Don't use 'using testing::Test;, elaborate the type instead")
        if line.startswith("using testing::TestWithParams;"):
            report_error("Don't use 'using testing::Test;, elaborate the type instead")
        if self.config.re["test_name_starting_lc"].search(line):
            # Matches variants of TEST(), TEST_P(), TEST_F() etc. where the test name begins
            # with a lowercase letter.
            report_error("Test names should be CamelCase, starting with a capital letter")
        if self.config.re["old_mock_method"].search(line):
            report_error("The MOCK_METHODn() macros should not be used, use MOCK_METHOD() instead")
        if self.config.re["for_each_n"].search(line):
            report_error("std::for_each_n should not be used, use an alternative for loop instead")

        if not self.allow_listed_for_serialize_as_string(file_path) and "SerializeAsString" in line:
            # The MessageLite::SerializeAsString doesn't generate deterministic serialization,
            # use MessageUtil::hash instead.
            report_error(
                "Don't use MessageLite::SerializeAsString for generating deterministic serialization, use MessageUtil::hash instead."
            )
        if not self.allow_listed_for_json_string_to_message(
                file_path) and "JsonStringToMessage" in line:
            # Centralize all usage of JSON parsing so it is easier to make changes in JSON parsing
            # behavior.
            report_error(
                "Don't use Protobuf::util::JsonStringToMessage, use TestUtility::loadFromJson.")

        if self.is_in_subdir(file_path, 'source') and file_path.endswith('.cc') and \
            ('.counterFromString(' in line or '.gaugeFromString(' in line or
             '.histogramFromString(' in line or '.textReadoutFromString(' in line or
             '->counterFromString(' in line or '->gaugeFromString(' in line or
                '->histogramFromString(' in line or '->textReadoutFromString(' in line):
            report_error(
                "Don't lookup stats by name at runtime; use StatName saved during construction")

        if self.config.re["mangled_protobuf_name"].search(line):
            report_error("Don't use mangled Protobuf names for enum constants")

        hist_m = self.config.re["histogram_si_suffix"].search(line)
        if hist_m and not self.allow_listed_for_histogram_si_suffix(hist_m.group(0)):
            report_error(
                "Don't suffix histogram names with the unit symbol, "
                "it's already part of the histogram object and unit-supporting sinks can use this information natively, "
                "other sinks can add the suffix automatically on flush should they prefer to do so."
            )

        normalized_target_path = file_path
        if not normalized_target_path.startswith("./"):
            normalized_target_path = f"./{normalized_target_path}"
        if not self.allow_listed_for_std_regex(normalized_target_path) and "std::regex" in line:
            report_error(
                "Don't use std::regex in code that handles untrusted input. Use RegexMatcher")

        if not self.allow_listed_for_grpc_init(file_path):
            grpc_init_or_shutdown = line.find("grpc_init()")
            grpc_shutdown = line.find("grpc_shutdown()")
            if grpc_init_or_shutdown == -1 or (grpc_shutdown != -1
                                               and grpc_shutdown < grpc_init_or_shutdown):
                grpc_init_or_shutdown = grpc_shutdown
            if grpc_init_or_shutdown != -1:
                comment = line.find("// ")
                if comment == -1 or comment > grpc_init_or_shutdown:
                    report_error(
                        "Don't call grpc_init() or grpc_shutdown() directly, instantiate "
                        + "Grpc::GoogleGrpcContext. See #8282")

        if not self.included_for_memcpy(file_path) and \
           not ("test/" in file_path) and \
           ("memcpy(" in line) and \
           not ("NOLINT(safe-memcpy)" in line):
            report_error(
                "Don't call memcpy() directly; use safeMemcpy, safeMemcpyUnsafeSrc, safeMemcpyUnsafeDst or MemBlockBuilder instead."
            )

        if self.deny_listed_for_exceptions(file_path):
            # Skpping cases where 'throw' is a substring of a symbol like in "foothrowBar".
            if "throw" in line.split():
                comment_match = self.config.re["comment"].search(line)
                if comment_match is None or comment_match.start(0) > line.find("throw"):
                    report_error(
                        "Don't introduce throws into exception-free files, use error "
                        + "statuses instead.")

        if "lua_pushlightuserdata" in line:
            report_error(
                "Don't use lua_pushlightuserdata, since it can cause unprotected error in call to"
                + "Lua API (bad light userdata pointer) on ARM64 architecture. See "
                + "https://github.com/LuaJIT/LuaJIT/issues/450#issuecomment-433659873 for details.")

        if file_path.endswith(self.config.suffixes["proto"]):
            exclude_path = ['v1', 'v2']
            result = self.config.re["proto_validation_string"].search(line)
            if result is not None:
                if not any(x in file_path for x in exclude_path):
                    report_error("min_bytes is DEPRECATED, Use min_len.")

    def check_build_line(self, line, file_path, report_error):
        if "@bazel_tools" in line and not (self.is_starlark_file(file_path)
                                           or file_path.startswith("./bazel/")
                                           or "python/runfiles" in line):
            report_error(
                "unexpected @bazel_tools reference, please indirect via a definition in //bazel")
        if not self.allow_listed_for_protobuf_deps(file_path) and '"protobuf"' in line:
            report_error(
                "unexpected direct external dependency on protobuf, use "
                "//source/common/protobuf instead.")
        if (self.envoy_build_rule_check and not self.is_starlark_file(file_path)
                and not self.is_workspace_file(file_path)
                and not self.is_external_build_file(file_path) and "@envoy//" in line):
            report_error("Superfluous '@envoy//' prefix")
        if not self.allow_listed_for_build_urls(file_path) and (" urls = " in line
                                                                or " url = " in line):
            report_error("Only repository_locations.bzl may contains URL references")

    def fix_build_line(self, file_path, line, line_number):
        if (self.envoy_build_rule_check and not self.is_starlark_file(file_path)
                and not self.is_workspace_file(file_path)
                and not self.is_external_build_file(file_path)):
            line = line.replace("@envoy//", "//")
        return line

    def fix_build_path(self, file_path):
        self.evaluate_lines(file_path, functools.partial(self.fix_build_line, file_path))

        error_messages = []

        # TODO(htuch): Add API specific BUILD fixer script.
        if not self.is_build_fixer_excluded_file(file_path) and not self.is_api_file(
                file_path) and not self.is_starlark_file(file_path) and not self.is_workspace_file(
                    file_path):
            if os.system("%s %s %s" %
                         (self.config.paths["build_fixer_py"], file_path, file_path)) != 0:
                error_messages += ["envoy_build_fixer rewrite failed for file: %s" % file_path]

        if os.system("%s -lint=fix -mode=fix %s" % (self.config.buildifier_path, file_path)) != 0:
            error_messages += ["buildifier rewrite failed for file: %s" % file_path]
        return error_messages

    def check_build_path(self, file_path):
        error_messages = []

        if not self.is_build_fixer_excluded_file(file_path) and not self.is_api_file(
                file_path) and not self.is_starlark_file(file_path) and not self.is_workspace_file(
                    file_path):
            command = "%s %s | diff %s -" % (
                self.config.paths["build_fixer_py"], file_path, file_path)
            error_messages += self.execute_command(
                command, "envoy_build_fixer check failed", file_path)

        if self.is_build_file(file_path) and file_path.startswith(self.api_prefix + "envoy"):
            found = False
            for line in self.read_lines(file_path):
                if "api_proto_package(" in line:
                    found = True
                    break
            if not found:
                error_messages += ["API build file does not provide api_proto_package()"]

        command = "%s -mode=diff %s" % (self.config.buildifier_path, file_path)
        error_messages += self.execute_command(command, "buildifier check failed", file_path)
        error_messages += self.check_file_contents(file_path, self.check_build_line)
        return error_messages

    def fix_source_path(self, file_path):
        self.evaluate_lines(file_path, self.fix_source_line)

        error_messages = []

        if not file_path.endswith(self.config.suffixes["proto"]):
            error_messages += self.fix_header_order(file_path)
        error_messages += self.clang_format(file_path)
        return error_messages

    def check_source_path(self, file_path):
        error_messages = self.check_file_contents(file_path, self.check_source_line)

        if not file_path.endswith(self.config.suffixes["proto"]):
            error_messages += self.check_namespace(file_path)
            command = (
                "%s --include_dir_order %s --path %s | diff %s -" % (
                    self.config.paths["header_order_py"], self.include_dir_order, file_path,
                    file_path))
            error_messages += self.execute_command(
                command, "header_order.py check failed", file_path)
        command = ("%s %s | diff %s -" % (self.config.clang_format_path, file_path, file_path))
        error_messages += self.execute_command(command, "clang-format check failed", file_path)
        return error_messages

    # Example target outputs are:
    #   - "26,27c26"
    #   - "12,13d13"
    #   - "7a8,9"
    def execute_command(self, command, error_message, file_path, regex=None):
        regex = regex or self.config.re["line_number"]
        try:
            output = subprocess.check_output(command, shell=True, stderr=subprocess.STDOUT).strip()
            if output:
                return output.decode('utf-8').split("\n")
            return []
        except subprocess.CalledProcessError as e:
            if (e.returncode != 0 and e.returncode != 1):
                return ["ERROR: something went wrong while executing: %s" % e.cmd]
            # In case we can't find any line numbers, record an error message first.
            error_messages = ["%s for file: %s" % (error_message, file_path)]
            for line in e.output.decode('utf-8').splitlines():
                for num in regex.findall(line):
                    error_messages.append("  %s:%s" % (file_path, num))
            return error_messages

    def fix_header_order(self, file_path):
        command = "%s --rewrite --include_dir_order %s --path %s" % (
            self.config.paths["header_order_py"], self.include_dir_order, file_path)
        if os.system(command) != 0:
            return ["header_order.py rewrite error: %s" % (file_path)]
        return []

    def clang_format(self, file_path):
        command = "%s -i %s" % (self.config.clang_format_path, file_path)
        if os.system(command) != 0:
            return ["clang-format rewrite error: %s" % (file_path)]
        return []

    def check_format(self, file_path, fail_on_diff=False):
        error_messages = []
        orig_error_messages = []
        # Apply fixes first, if asked, and then run checks. If we wind up attempting to fix
        # an issue, but there's still an error, that's a problem.
        try_to_fix = self.operation_type == "fix"
        if self.is_build_file(file_path) or self.is_starlark_file(
                file_path) or self.is_workspace_file(file_path):
            if try_to_fix:
                orig_error_messages = self.check_build_path(file_path)
                if orig_error_messages:
                    error_messages += self.fix_build_path(file_path)
                    error_messages += self.check_build_path(file_path)
            else:
                error_messages += self.check_build_path(file_path)
        else:
            if try_to_fix:
                orig_error_messages = self.check_source_path(file_path)
                if orig_error_messages:
                    error_messages += self.fix_source_path(file_path)
                    error_messages += self.check_source_path(file_path)
            else:
                error_messages += self.check_source_path(file_path)

        if error_messages:
            return ["From %s" % file_path] + error_messages
        if not error_messages and fail_on_diff:
            return orig_error_messages
        return error_messages

    def check_format_return_trace_on_error(self, file_path, fail_on_diff=False):
        """Run check_format and return the traceback of any exception."""
        try:
            return self.check_format(file_path, fail_on_diff=fail_on_diff)
        except:
            return traceback.format_exc().split("\n")

    def check_owners(self, dir_name, owned_directories, error_messages):
        """Checks to make sure a given directory is present either in CODEOWNERS or OWNED_EXTENSIONS
    Args:
      dir_name: the directory being checked.
      owned_directories: directories currently listed in CODEOWNERS.
      error_messages: where to put an error message for new unowned directories.
    """
        found = False
        for owned in owned_directories:
            if owned.startswith(dir_name) or dir_name.startswith(owned):
                found = True
                break
        if not found:
            error_messages.append(
                "New directory %s appears to not have owners in CODEOWNERS" % dir_name)

    def check_format_visitor(self, arg, dir_name, names, fail_on_diff=False):
        """Run check_format in parallel for the given files.
        Args:
          arg: a tuple (pool, result_list, owned_directories, error_messages)
            pool and result_list are for starting tasks asynchronously.
            owned_directories tracks directories listed in the CODEOWNERS file.
            error_messages is a list of string format errors.
          dir_name: the parent directory of the given files.
            names: a list of file names.
        """

        # Unpack the multiprocessing.Pool process pool and list of results. Since
        # python lists are passed as references, this is used to collect the list of
        # async results (futures) from running check_format and passing them back to
        # the caller.
        pool, result_list, owned_directories, error_messages = arg

        # Sanity check CODEOWNERS.  This doesn't need to be done in a multi-threaded
        # manner as it is a small and limited list.
        source_prefix = './source/'
        core_extensions_full_prefix = './source/extensions/'
        # Check to see if this directory is a subdir under /source/extensions
        # Also ignore top level directories under /source/extensions since we don't
        # need owners for source/extensions/access_loggers etc, just the subdirectories.
        if dir_name.startswith(
                core_extensions_full_prefix) and '/' in dir_name[len(core_extensions_full_prefix):]:
            self.check_owners(dir_name[len(source_prefix):], owned_directories, error_messages)

        # For contrib extensions we track ownership at the top level only.
        contrib_prefix = './contrib/'
        if dir_name.startswith(contrib_prefix):
            top_level = pathlib.PurePath('/', *pathlib.PurePath(dir_name).parts[:2], '/')
            self.check_owners(str(top_level), owned_directories, error_messages)

        dir_name = normalize_path(dir_name)

        # TODO(phlax): improve class/process handling - this is required because if these
        #   are not cached before the class is sent into the pool, it only caches them on the
        #   forked proc
        self.build_fixer_check_excluded_paths
        self.namespace_check_excluded_paths
        self.namespace_re
        self.config.replacements
        self.config.dir_order

        for file_name in names:
            result = pool.apply_async(
                self.check_format_return_trace_on_error, args=(dir_name + file_name, fail_on_diff))
            result_list.append(result)

    # check_error_messages iterates over the list with error messages and prints
    # errors and returns a bool based on whether there were any errors.
    def check_error_messages(self, error_messages):
        if error_messages:
            for e in error_messages:
                print("ERROR: %s" % e)
            return True
        return False

    def included_for_memcpy(self, file_path):
        return file_path in self.config.paths["memcpy"]["include"]


def normalize_path(path):
    """Convert path to form ./path/to/dir/ for directories and ./path/to/file otherwise"""
    if not path.startswith("./"):
        path = "./" + path

    isdir = os.path.isdir(path)
    if isdir and not path.endswith("/"):
        path += "/"

    return path


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Check or fix file format.")
    parser.add_argument(
        "operation_type",
        type=str,
        choices=["check", "fix"],
        help="specify if the run should 'check' or 'fix' format.")
    parser.add_argument(
        "target_path",
        type=str,
        nargs="?",
        default=".",
        help="specify the root directory for the script to recurse over. Default '.'.")
    parser.add_argument(
        "--config_path",
        default="./tools/code_format/config.yaml",
        help="specify the config path. Default './tools/code_format/config.yaml'.")
    parser.add_argument(
        "--fail_on_diff",
        action="store_true",
        help="exit with failure if running fix produces changes.")
    parser.add_argument(
        "--add-excluded-prefixes", type=str, nargs="+", help="exclude additional prefixes.")
    parser.add_argument(
        "-j",
        "--num-workers",
        type=int,
        default=multiprocessing.cpu_count(),
        help="number of worker processes to use; defaults to one per core.")
    parser.add_argument("--api-prefix", type=str, default="./api/", help="path of the API tree.")
    parser.add_argument(
        "--skip_envoy_build_rule_check",
        action="store_true",
        help="skip checking for '@envoy//' prefix in build rules.")
    parser.add_argument(
        "--namespace_check",
        type=str,
        nargs="?",
        default="Envoy",
        help="specify namespace check string. Default 'Envoy'.")
    parser.add_argument(
        "--namespace_check_excluded_paths",
        type=str,
        nargs="+",
        default=[],
        help="exclude paths from the namespace_check.")
    parser.add_argument(
        "--build_fixer_check_excluded_paths",
        type=str,
        nargs="+",
        default=[],
        help="exclude paths from envoy_build_fixer check.")
    parser.add_argument(
        "--bazel_tools_check_excluded_paths",
        type=str,
        nargs="+",
        default=[],
        help="exclude paths from bazel_tools check.")
    parser.add_argument(
        "--include_dir_order",
        type=str,
        default="",
        help="specify the header block include directory order.")
    args = parser.parse_args()

    format_checker = FormatChecker(args)

    excluded_prefixes = format_checker.config.paths["excluded"]
    if args.add_excluded_prefixes:
        excluded_prefixes += tuple(args.add_excluded_prefixes)

    # Check whether all needed external tools are available.
    ct_error_messages = format_checker.check_tools()
    if format_checker.check_error_messages(ct_error_messages):
        sys.exit(1)

    def check_visibility(error_messages):
        command = (
            "git diff $(tools/git/last_github_commit.sh) -- source/extensions/* %s |grep '+.*visibility ='"
            % "".join([f"':(exclude){c}' " for c in format_checker.config["visibility_excludes"]]))
        try:
            output = subprocess.check_output(command, shell=True, stderr=subprocess.STDOUT).strip()
            if output:
                error_messages.append(
                    "This change appears to add visibility rules. Please get senior maintainer "
                    "approval to add an exemption to check_visibility tools/code_format/check_format.py"
                )
            output = subprocess.check_output(
                "grep -r --include BUILD envoy_package source/extensions/*",
                shell=True,
                stderr=subprocess.STDOUT).strip()
            if output:
                error_messages.append(
                    "envoy_package is not allowed to be used in source/extensions BUILD files.")
        except subprocess.CalledProcessError as e:
            if (e.returncode != 0 and e.returncode != 1):
                error_messages.append("Failed to check visibility with command %s" % command)

    def get_owners():
        with open('./OWNERS.md') as f:
            maintainers = ["@UNOWNED"]
            for line in f:
                if "Senior extension maintainers" in line:
                    return maintainers
                m = format_checker.config.re["maintainers"].search(line)
                if m is not None:
                    maintainers.append("@" + m.group(1).lower())

    # Returns the list of directories with owners listed in CODEOWNERS. May append errors to
    # error_messages.
    def owned_directories(error_messages):
        owned = []
        try:
            maintainers = get_owners()

            with open('./CODEOWNERS') as f:
                for line in f:
                    # If this line is of the form "extensions/... @owner1 @owner2" capture the directory
                    # name and store it in the list of directories with documented owners.
                    m = format_checker.config.re["codeowners_extensions"].search(line)
                    if m is not None and not line.startswith('#'):
                        owned.append(m.group(1).strip())
                        owners = format_checker.config.re["owner"].findall(m.group(2).strip())
                        if len(owners) < 2:
                            error_messages.append(
                                "Extensions require at least 2 owners in CODEOWNERS:\n"
                                "    {}".format(line))
                        maintainer = len(set(owners).intersection(set(maintainers))) > 0
                        if not maintainer:
                            error_messages.append(
                                "Extensions require at least one maintainer OWNER:\n"
                                "    {}".format(line))

                    m = format_checker.config.re["codeowners_contrib"].search(line)
                    if m is not None and not line.startswith('#'):
                        stripped_path = m.group(1).strip()
                        if not stripped_path.endswith('/'):
                            error_messages.append(
                                "Contrib CODEOWNERS entry '{}' must end in '/'".format(
                                    stripped_path))
                            continue

                        if not (stripped_path.count('/') == 3 or
                                (stripped_path.count('/') == 4
                                 and stripped_path.startswith('/contrib/common/'))):
                            error_messages.append(
                                "Contrib CODEOWNERS entry '{}' must be 2 directories deep unless in /contrib/common/ and then it can be 3 directories deep"
                                .format(stripped_path))
                            continue

                        owned.append(stripped_path)
                        owners = format_checker.config.re["owner"].findall(m.group(2).strip())
                        if len(owners) < 2:
                            error_messages.append(
                                "Contrib extensions require at least 2 owners in CODEOWNERS:\n"
                                "    {}".format(line))

            return owned
        except IOError:
            return []  # for the check format tests.

    # Calculate the list of owned directories once per run.
    error_messages = []
    owned_directories = owned_directories(error_messages)

    check_visibility(error_messages)

    if os.path.isfile(args.target_path):
        # All of our `excluded_prefixes` start with "./", but the provided
        # target path argument might not. Add it here if it is missing,
        # and use that normalized path for both lookup and `check_format`.
        normalized_target_path = normalize_path(args.target_path)
        if not normalized_target_path.startswith(
                excluded_prefixes) and normalized_target_path.endswith(
                    format_checker.config.suffixes["included"]):
            error_messages += format_checker.check_format(normalized_target_path)
    else:
        results = []

        def pooled_check_format(path_predicate):
            pool = multiprocessing.Pool(processes=args.num_workers)
            # For each file in target_path, start a new task in the pool and collect the
            # results (results is passed by reference, and is used as an output).
            for root, _, files in os.walk(args.target_path):
                _files = []
                for filename in files:
                    file_path = os.path.join(root, filename)
                    check_file = (
                        path_predicate(filename) and not file_path.startswith(excluded_prefixes)
                        and file_path.endswith(format_checker.config.suffixes["included"]) and not (
                            file_path.endswith(format_checker.config.suffixes["proto"])
                            and root.startswith(args.api_prefix)))
                    if check_file:
                        _files.append(filename)
                if not _files:
                    continue
                format_checker.check_format_visitor(
                    (pool, results, owned_directories, error_messages), root, _files,
                    args.fail_on_diff)

            # Close the pool to new tasks, wait for all of the running tasks to finish,
            # then collect the error messages.
            pool.close()
            pool.join()

        # We first run formatting on non-BUILD files, since the BUILD file format
        # requires analysis of srcs/hdrs in the BUILD file, and we don't want these
        # to be rewritten by other multiprocessing pooled processes.
        pooled_check_format(lambda f: not format_checker.is_build_file(f))
        pooled_check_format(lambda f: format_checker.is_build_file(f))

        error_messages += sum((r.get() for r in results), [])

    if format_checker.check_error_messages(error_messages):
        if args.operation_type == "check":
            print("ERROR: check format failed. run 'tools/code_format/check_format.py fix'")
        else:
            print("ERROR: check format failed. diff has been applied'")
        sys.exit(1)

    if args.operation_type == "check":
        print("PASS")
