# Registers the Valhalla toolsets with Epic's Toolset Registry (UE 5.8).
# Mirrors Engine/Plugins/Experimental/Toolsets/EditorToolset/Content/Python/editor_toolset/toolsets/__init__.py
from toolset_registry.registration import Registration
from valhalla_tools.toolsets import data_tools
from valhalla_tools.toolsets import level_tools

_registration = Registration([
    data_tools.ValhallaDataTools,
    level_tools.ValhallaLevelTools,
])
