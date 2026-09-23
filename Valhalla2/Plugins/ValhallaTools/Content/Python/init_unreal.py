# Runs automatically when the ValhallaTools plugin's Python content is mounted.
# Same pattern as Epic's EditorToolset init_unreal.py.
import unreal  # noqa: F401

from valhalla_tools import toolsets

toolsets._registration.register()
