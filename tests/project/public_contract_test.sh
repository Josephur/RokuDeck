#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

repository_root=$(git rev-parse --show-toplevel)
cd "$repository_root"

for required_file in \
    .github/ISSUE_TEMPLATE/bug_report.yml \
    .github/ISSUE_TEMPLATE/feature_request.yml \
    .github/ISSUE_TEMPLATE/config.yml \
    .github/PULL_REQUEST_TEMPLATE.md; do
    if [[ ! -s "$required_file" ]]; then
        echo "public contract: missing or empty $required_file" >&2
        exit 1
    fi
done

for cmake_file in CMakeLists.txt cmake/ProjectOptions.cmake; do
    if [[ $(head -n 1 "$cmake_file") != '# SPDX-License-Identifier: GPL-3.0-or-later' ]]; then
        echo "public contract: missing GPL-3.0-or-later SPDX marker in $cmake_file" >&2
        exit 1
    fi
done

echo "public contract: templates and CMake licensing metadata are present"
