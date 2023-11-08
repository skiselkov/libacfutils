#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Copyright 2023 Saso Kiselkov. All rights reserved.

CLIPPY_OPTS=(
    "-Aclippy::tabs_in_doc_comments"
    "-Aclippy::collapsible_else_if"
    "-Aclippy::missing_safety_doc"
    "-Aclippy::manual_range_contains"
    "-Aclippy::excessive_precision"
)

cargo clippy -- ${CLIPPY_OPTS[@]}
