"""Valhalla 2.0 editor toolsets, exposed to Unreal MCP.

Modules under a plugin's ``Content/Python/`` directory are auto-discovered by
the editor, so importing this package is enough to register everything in
:mod:`valhalla_tools.toolsets`. Call ``ModelContextProtocol.RefreshTools`` in
the editor after editing a toolset to re-poll the registry without restarting.

Toolsets:
    :class:`valhalla_tools.toolsets.data_tools.ValhallaDataTools`
        Validate the 1.0 JSON data, batch-import glTF meshes, run the
        ``Valhalla.*`` automation tests.
"""

from valhalla_tools import toolsets  # noqa: F401  (registers the toolsets)

__all__ = ["toolsets"]
