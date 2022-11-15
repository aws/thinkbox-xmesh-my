# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

# Scripted utilities for Maya XMesh

from contextlib import contextmanager

import maya.cmds

@contextmanager
def waitCursor():
    maya.cmds.waitCursor(state=True)
    try:
        yield
    finally:
        maya.cmds.waitCursor(state=False)
