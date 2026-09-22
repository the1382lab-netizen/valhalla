"""The eight Phase 8a Niagara systems, built from a declarative spec.

WHY THIS FILE DOES NOT ``import unreal``

Niagara's *stack* — emitters, modules, module inputs, renderers — has no Python
binding. ``unreal.NiagaraSystemFactoryNew`` can make an empty asset and nothing
after that; there is no ``add_module``, no ``set_input``, no way to reach a
renderer's material. What does exist is UE 5.8's Toolset Registry
(``NiagaraToolsets.NiagaraToolset_System``), whose tools are exactly those
operations, and the ProgrammaticToolset, which runs a small Python script with
an ``execute_tool(name, json)`` function in scope and nothing else.

So this module is written to be *that* script: it imports only ``json``, and it
defines ``run()``. It is checked in beside ``build_toon.py`` because it is the
same kind of artefact — the reproducible source of a piece of content — and it
is executed by passing its text to
``editor_toolset.toolsets.programmatic.ProgrammaticToolset.execute_tool_script``
rather than by ``py build_vfx.py``. Running it under the normal editor
interpreter would fail at the first ``execute_tool``, which is intentional: the
only way to run it is the way that works.

WHAT THE SYSTEMS ARE

Eight systems under ``/Game/Valhalla/VFX``, all built from engine template
emitters, all CPU, all rendering the two textureless materials
``build_vfx_materials.py`` authors, all under 200 particles:

    NS_Bolt      a lit core sprite plus a ribbon trail; the projectile body
    NS_Impact    a radial burst plus a flat shockwave ring
    NS_Slash     a 120-degree arc of sprites lying on the ground, 0.25 s
    NS_Heal      sparkles rising out of a disc at the feet
    NS_BuffAura  a looping ground ring plus slow motes; lives while a buff does
    NS_Debuff    motes falling out of a cloud above the target
    NS_AoERing   a ground ring expanding to User.Radius, plus a rim of motes
    NS_Cone      a fan of sprites thrown forward inside a 55-degree cone

Every system declares the same two user parameters, and only those two:

    ``Color``   LinearColor — the palette. UValhallaVfxLibrary sets it from the
                skill's scalingStat, or the caster's class colour.
    ``Radius``  float, centimetres — how big the effect is. The AoE ring and the
                cone read it directly; the impact ring clamps it; the rest
                declare it so the C++ can set it unconditionally without
                Niagara logging a warning per cast about a parameter that is
                not there.

Shape and size come from the stack, not from curves. Everything that varies
over a particle's life is a one-line HLSL expression on a module input
(``1.0 - Particles.NormalizedAge``) rather than a curve data interface: an
expression is a string this file can own, and a curve is a binary blob it
cannot.

SIZES ARE CENTIMETRES, AND THE CAMERA IS 1000-1500 CM AWAY

``Uniform Sprite Size`` is a world-space diameter in centimetres, the same
unit as everything else in this project (Phase 3: 1 px = 1 cm). A 16 cm sprite
is a ninth of a character's height seen from a 1000-1500 cm boom, so nothing
here is smaller than 40 cm, and a mote that has to read at a glance is 45-70 cm
— a third of a character. If an effect needs to be subtle, make it dimmer or
shorter-lived; do not make it small.

WHY THE SPRITE EFFECTS WERE INVISIBLE (Phase 8a)

The first pass had 11-34 cm sprites and every sprite effect drew nothing in
PIE while every ring drew fine. Raising the sizes was necessary but was *not*
the fix; three separate faults stacked up, each found by isolating one thing:

1. The sprite material's mask was ``(1 - d)^2``, a pinprick one fifth of the
   sprite's size — see ``build_vfx_materials._BLOB_HLSL``. Bisected by
   rendering one NS_Bolt core particle three times, differing only in the
   renderer material.
2. Additive blending at an emissive of 1.0 cannot show over this level's
   sunlit grass; the sprite material is now translucent with a glow gain —
   see ``build_vfx_materials._build``.
3. Two Fountain emitters whose spawn script was never fully rebuilt drew
   nothing at all; see ``recompile_shape``.

And two that did not hide anything but were wrong: the bolt core had no
``Particles.Color`` (see the NS_Bolt spec) and the held systems never died
after ``Deactivate()`` (see ``held``).
"""

import json

# ── Content ──────────────────────────────────────────────────────────────────

VFX_DIR = "/Game/Valhalla/VFX"
MAT_SPRITE = "/Game/Valhalla/Materials/M_ValhallaVfxSprite.M_ValhallaVfxSprite"
MAT_RING = "/Game/Valhalla/Materials/M_ValhallaVfxRing.M_ValhallaVfxRing"

#: The system every new asset is cloned from. It carries one Fountain emitter,
#: which is removed immediately; there is no genuinely empty system template.
BASE_SYSTEM = "/Niagara/DefaultAssets/DefaultSystem.DefaultSystem"

TEMPLATE_DIR = "/Niagara/DefaultAssets/Templates/Emitters/"

#: Module script assets, read off the templates rather than guessed.
MOD = {
    "SpawnRate": "/Niagara/Modules/Emitter/SpawnRate.SpawnRate",
    "ScaleColor": "/Niagara/Modules/Update/Color/ScaleColor.ScaleColor",
    "ScaleSpriteSize": "/Niagara/Modules/Update/Size/ScaleSpriteSize.ScaleSpriteSize",
    "GravityForce": "/Niagara/Modules/Update/Forces/GravityForce.GravityForce",
    "Drag": "/Niagara/Modules/Update/Forces/Drag.Drag",
}

# ── Niagara type paths ───────────────────────────────────────────────────────

T_FLOAT = "/Script/Niagara.NiagaraFloat"
T_INT = "/Script/Niagara.NiagaraInt32"
T_BOOL = "/Script/Niagara.NiagaraBool"
T_COLOR = "/Script/CoreUObject.LinearColor"
T_V2 = "/Script/CoreUObject.Vector2f"
T_V3 = "/Script/CoreUObject.Vector3f"

W_ENUM = "/Script/NiagaraEditor.NiagaraExt_StackInputData_Enum"
W_LINK = "/Script/NiagaraEditor.NiagaraExt_StackInputData_Linked"
W_HLSL = "/Script/NiagaraEditor.NiagaraExt_StackInputData_HlslExpression"

# ── Enum values ──────────────────────────────────────────────────────────────
#
# These are UserDefinedEnums, so their internal names are "NewEnumeratorN" and
# the display name is the only readable part. Both are passed; the display name
# is what makes the call sites legible.

E_LIFETIME = "/Niagara/Enums/ENiagara_LifetimeMode.ENiagara_LifetimeMode"
E_COLOR = "/Niagara/Enums/ENiagara_ColorInitializationMode.ENiagara_ColorInitializationMode"
E_SIZE = "/Niagara/Enums/ENiagara_SizeScaleMode.ENiagara_SizeScaleMode"
E_SHAPE = "/Niagara/Enums/Location/ENiagara_LocationShapes.ENiagara_LocationShapes"
E_VELOCITY = "/Niagara/Enums/Utility/ENiagara_VelocityMode.ENiagara_VelocityMode"
E_LOOP = "/Niagara/Enums/ENiagara_EmitterStateOptions.ENiagara_EmitterStateOptions"
E_LIFECYCLE = "/Niagara/Enums/ENiagaraEmitterLifeCycleMode.ENiagaraEmitterLifeCycleMode"
E_UNSETDIRECT = "/Niagara/Enums/Ribbons/ENiagara_UnsetDirectSet.ENiagara_UnsetDirectSet"
E_SPHEREDIST = "/Niagara/Enums/ENiagaraSphereDistributionMode.ENiagaraSphereDistributionMode"
E_COORD = "/Script/Niagara.ENiagaraCoordinateSpace"
E_SIZESCALE = "/Niagara/Enums/SpriteRenderer/ENiagara_ScaleSpriteSize.ENiagara_ScaleSpriteSize"
E_OFFSET = "/Niagara/Enums/Transforms/ENiagara_OffsetMode.ENiagara_OffsetMode"

V = {
    "DirectSet": ("NewEnumerator0", "Direct Set"),          # ENiagara_LifetimeMode
    "ColorDirect": ("NewEnumerator1", "Direct Set"),
    "SizeUniform": ("NewEnumerator3", "Uniform"),
    "ShapeSphere": ("NewEnumerator0", "Sphere"),
    "ShapeOther": ("NewEnumerator1", "Cylinder"),   # any value but Sphere; see recompile_shape
    "ShapeRing": ("NewEnumerator4", "Ring / Disc"),
    "VelLinear": ("NewEnumerator0", "Linear"),
    "VelFromPoint": ("NewEnumerator1", "From Point"),
    "VelInCone": ("NewEnumerator2", "In Cone"),
    "LoopInfinite": ("NewEnumerator0", "Infinite"),
    "LoopOnce": ("NewEnumerator1", "Once"),
    "LifeSelf": ("NewEnumerator1", "Self"),
    "UnsetDirect": ("NewEnumerator1", "Direct Set"),
    "DistUniform": ("NewEnumerator3", "Uniform"),
    "ScaleUniform": ("NewEnumerator0", "Uniform"),
    "OffsetDefault": ("NewEnumerator0", "Default"),
    "Local": ("Local", "Local"),
}

# ── Op helpers, used by the spec below ───────────────────────────────────────


def enum(script, module, name, enum_path, key):
    return ("enum", script, module, name, enum_path, V[key][0], V[key][1])


def flt(script, module, name, value):
    return ("scalar", script, module, name, T_FLOAT, {"value": float(value)})


def integer(script, module, name, value):
    return ("scalar", script, module, name, T_INT, {"value": int(value)})


def boolean(script, module, name, value):
    # FNiagaraBool is -1 for true, 0 for false; anything else is undefined.
    return ("scalar", script, module, name, T_BOOL, {"value": -1 if value else 0})


def vec2(script, module, name, x, y):
    return ("scalar", script, module, name, T_V2, {"x": x, "y": y})


def vec3(script, module, name, x, y, z):
    return ("scalar", script, module, name, T_V3, {"x": x, "y": y, "z": z})


def link_color(script, module, name):
    return ("link", script, module, name, "User.Color", T_COLOR)


def hlsl(script, module, name, expression):
    return ("hlsl", script, module, name, expression)


def add_module(script, key):
    return ("addmod", script, MOD[key])


def del_module(script, module):
    """Drop a module the template shipped, before adding our own.

    Not tidiness — correctness. The Fountain and DirectionalBurst templates
    drive ``ScaleColor.Scale Alpha`` and ``ScaleSpriteSize.Scale Factor`` from
    *curve* dynamic inputs, and a curve is a data interface living in the
    emitter's parameter store. Overwriting such an input with an HLSL
    expression takes the curve out of the graph and leaves the data interface
    behind, so the next full recompile finds

        Data interface count mismatch during script presave
        Name:Emitter.Scale Alpha.FloatCurve, Type: NiagaraDataInterfaceCurve
        Could not create script runtime data ... compiled DI count 1 and
        resolved DI count 0 difference

    and throws the compile result away. The emitter is then left with a
    particle spawn and update script containing *nothing* — the asset still
    opens, ``GetStackIssues`` still says zero issues, and the effect silently
    emits no particles. That is what happened to NS_Heal, NS_Debuff and
    NS_Cone.

    So every module whose input this file drives by expression is removed and
    re-added fresh, which is the only state where what the graph references and
    what the parameter store holds are guaranteed to agree.
    """
    return ("delmod", script, module)


def renderer(index, props):
    return ("renderer", index, props)


#: Lay the sprite flat on the ground instead of facing the camera. Two particle
#: attributes plus the two renderer modes that read them; a ring that faces the
#: camera on an isometric view reads as a vertical hoop, which is wrong in a way
#: no amount of tuning fixes.
def ground_quad():
    return ("groundquad",)


GROUND_RENDER = {"material": MAT_RING, "alignment": "CustomAlignment",
                 "facingMode": "CustomFacingVector", "sortMode": "None"}
SPRITE_RENDER = {"material": MAT_SPRITE, "alignment": "Unaligned",
                 "facingMode": "FaceCamera", "sortMode": "None"}

#: Fade out over the particle's life. Applied through ScaleColor's alpha, which
#: the additive material folds into its emissive.
FADE = "1.0 - Particles.NormalizedAge"
#: Shrink over the particle's life. Scalar; size_over_life widens it when the
#: module it is going into wants a Vector2.
SHRINK = "1.0 - Particles.NormalizedAge"
#: Grow over the particle's life; the expanding ring.
GROW = "Particles.NormalizedAge"


def one_shot(duration):
    """Make a SingleLoopingParticle-derived emitter finish by itself.

    Its EmitterState ships with Life Cycle Mode = System, which means "never
    complete on my own" — a leak for anything spawned with bAutoDestroy. The
    bolt and the aura, which the C++ owns and stops, use ``held`` instead.
    """
    return [
        enum("EmitterUpdateScript", "EmitterState", "Life Cycle Mode", E_LIFECYCLE, "LifeSelf"),
        enum("EmitterUpdateScript", "EmitterState", "Loop Behavior", E_LOOP, "LoopOnce"),
        flt("EmitterUpdateScript", "EmitterState", "Loop Duration", duration),
    ]


def held(period):
    """A single particle re-spawned every ``period`` seconds, for as long as the
    component is active — the shape of anything the C++ *holds* and later
    stops (the bolt core, the buff ring).

    The obvious way to write "one particle that lasts as long as the effect" is
    one burst with a lifetime of 1e5 s, and it is wrong: the C++ stops a held
    effect with ``Deactivate()``, which stops *spawning* and lets the particles
    already alive finish their lives. A 1e5 s particle never finishes, so every
    muzzle flash and every expired buff ring stayed on screen for the rest of
    the session. With the emitter looping on its own clock and each particle
    living exactly one loop, deactivation takes at most ``period`` to clear.

    Use it together with ``basic_particle(period, ...)``.
    """
    return [
        enum("EmitterUpdateScript", "EmitterState", "Life Cycle Mode", E_LIFECYCLE, "LifeSelf"),
        enum("EmitterUpdateScript", "EmitterState", "Loop Behavior", E_LOOP, "LoopInfinite"),
        flt("EmitterUpdateScript", "EmitterState", "Loop Duration", period),
    ]


#: The bolt core's respawn period: short, so a stopped muzzle flash is gone
#: within a quarter second of the C++ letting go of it.
BOLT_PERIOD = 0.25
#: The aura ring's: exactly one period of its ``sin(Age * 2.5)`` breath (2*pi /
#: 2.5), so the re-spawned ring picks up the breath where the last one left it.
AURA_PERIOD = 2.5133


def basic_particle(lifetime, size, color=True):
    """Lifetime, tint and uniform size — the four inputs every emitter sets."""
    ops = [
        enum("ParticleSpawnScript", "InitializeParticle", "Lifetime Mode", E_LIFETIME, "DirectSet"),
        flt("ParticleSpawnScript", "InitializeParticle", "Lifetime", lifetime),
        enum("ParticleSpawnScript", "InitializeParticle", "Sprite Size Mode", E_SIZE, "SizeUniform"),
    ]
    if isinstance(size, str):
        ops.append(hlsl("ParticleSpawnScript", "InitializeParticle", "Uniform Sprite Size", size))
    else:
        ops.append(flt("ParticleSpawnScript", "InitializeParticle", "Uniform Sprite Size", size))
    if color:
        ops.append(enum("ParticleSpawnScript", "InitializeParticle", "Color Mode", E_COLOR, "ColorDirect"))
        ops.append(link_color("ParticleSpawnScript", "InitializeParticle", "Color"))
    return ops


def fade_alpha(has_template_module=True):
    """Fade the particle out over its life, through a *fresh* ScaleColor.

    ``has_template_module`` says whether the emitter template shipped one to
    throw away first; see ``del_module`` for why it is never reused.
    """
    ops = [del_module("ParticleUpdateScript", "ScaleColor")] if has_template_module else []
    ops.append(add_module("ParticleUpdateScript", "ScaleColor"))
    ops.append(boolean("ParticleUpdateScript", "ScaleColor", "ScaleA", True))
    ops.append(hlsl("ParticleUpdateScript", "ScaleColor", "Scale Alpha", FADE))
    return ops


def size_over_life(expression, has_template_module=True):
    """Drive sprite size from a scalar expression of the particle's age.

    Always a freshly added module, for ``del_module``'s reason, which also
    means always the *current* version of the script: the one with the ``Scale
    Sprite Size Mode`` static switch, whose Uniform branch takes the float a
    uniform scale actually wants. The older pinned version some templates ship
    with — Vector2 ``Scale Factor``, no switch — is removed rather than driven.
    """
    ops = [del_module("ParticleUpdateScript", "ScaleSpriteSize")] if has_template_module else []
    ops += [
        add_module("ParticleUpdateScript", "ScaleSpriteSize"),
        enum("ParticleUpdateScript", "ScaleSpriteSize", "Scale Sprite Size Mode", E_SIZESCALE, "ScaleUniform"),
        hlsl("ParticleUpdateScript", "ScaleSpriteSize", "Uniform Scale Factor", expression),
    ]
    return ops


def around_feet(radius):
    """Spawn in a ball of ``radius`` cm round the feet — the heal sparkles and
    the aura motes. Half the ball is under the floor, so the motes appear to
    rise out of the ground rather than pop into the air.

    Use together with ``recompile_shape()`` as the emitter's *last* op.
    """
    return [
        enum("ParticleSpawnScript", "ShapeLocation", "Shape Primitive", E_SHAPE, "ShapeSphere"),
        flt("ParticleSpawnScript", "ShapeLocation", "Sphere Radius", radius),
    ]


def recompile_shape():
    """Flip ShapeLocation's Shape Primitive away and back, as the last op.

    WHY: NS_Heal's sparkles and NS_BuffAura's motes — the two Fountain-template
    emitters that set nothing else on ShapeLocation's static switches after the
    module edits below them — built clean, reported UpToDate with no stack
    issues, were active and attached in the right place, and drew *nothing*.
    NS_Debuff, the same template and the same ops, drew fine; the difference is
    that it also sets ``Offset Mode``, another static switch. Changing a static
    switch forces a full rebuild of the spawn script, and nothing else this
    file does (value inputs, module add/remove) is enough. Verified in PIE:
    the freshly built NS_Heal drew nothing; the same asset after Cylinder ->
    Sphere drew at once. (The first attempts blamed the Cylinder and Ring
    shapes; they may well be innocent, but Sphere is what is verified.)

    There is no "compile this system" tool in the toolset, so this is the
    rebuild trigger.
    """
    return [
        enum("ParticleSpawnScript", "ShapeLocation", "Shape Primitive", E_SHAPE, "ShapeOther"),
        enum("ParticleSpawnScript", "ShapeLocation", "Shape Primitive", E_SHAPE, "ShapeSphere"),
    ]


def no_gravity(add=False):
    ops = [add_module("ParticleUpdateScript", "GravityForce")] if add else []
    ops.append(vec3("ParticleUpdateScript", "GravityForce", "Gravity", 0.0, 0.0, 0.0))
    return ops


# ── The eight systems ────────────────────────────────────────────────────────

SYSTEMS = [
    # ── NS_Bolt: the spell projectile's body. Loops forever; the actor that
    # carries it is what decides when it stops.
    {
        "name": "NS_Bolt",
        "emitters": [
            {
                "name": "Core", "template": "SingleLoopingParticle",
                "ops": (
                    held(BOLT_PERIOD)
                    + [integer("EmitterUpdateScript", "SpawnBurst_Instantaneous", "Spawn Count", 1),
                       flt("EmitterUpdateScript", "SpawnBurst_Instantaneous", "Spawn Time", 0.0)]
                    + basic_particle(BOLT_PERIOD, 85.0)
                    # Every other emitter here fades through ScaleColor; this
                    # one never fades, and without *some* update module that
                    # reads Particles.Color the compiled emitter drops the
                    # attribute InitializeParticle wrote. The renderer's Color
                    # binding then resolves to nothing and the core draws
                    # untinted white. A ScaleColor left at its defaults (x1)
                    # is the cheapest thing that keeps the colour alive.
                    + [add_module("ParticleUpdateScript", "ScaleColor")]
                    + [renderer(0, SPRITE_RENDER)]
                ),
            },
            {
                "name": "Trail", "template": "LocationBasedRibbon",
                "ops": (
                    [add_module("EmitterUpdateScript", "SpawnRate"),
                     flt("EmitterUpdateScript", "SpawnRate", "SpawnRate", 90.0),
                     enum("ParticleSpawnScript", "InitializeParticle", "Lifetime Mode", E_LIFETIME, "DirectSet"),
                     flt("ParticleSpawnScript", "InitializeParticle", "Lifetime", 0.28),
                     enum("ParticleSpawnScript", "InitializeParticle", "Color Mode", E_COLOR, "ColorDirect"),
                     link_color("ParticleSpawnScript", "InitializeParticle", "Color"),
                     enum("ParticleSpawnScript", "InitializeParticle", "Ribbon Width Mode", E_UNSETDIRECT, "UnsetDirect"),
                     flt("ParticleSpawnScript", "InitializeParticle", "Ribbon Width", 44.0)]
                    + fade_alpha(has_template_module=False)
                    + [renderer(0, {"material": MAT_SPRITE})]
                ),
            },
        ],
    },

    # ── NS_Impact: what a projectile leaves behind. 36 motes + one ring.
    {
        "name": "NS_Impact",
        "emitters": [
            {
                "name": "Burst", "template": "OmnidirectionalBurst",
                "ops": (
                    [enum("EmitterUpdateScript", "EmitterState", "Loop Behavior", E_LOOP, "LoopOnce"),
                     flt("EmitterUpdateScript", "EmitterState", "Loop Duration", 0.45),
                     integer("EmitterUpdateScript", "SpawnBurst_Instantaneous", "Spawn Count", 36),
                     flt("EmitterUpdateScript", "SpawnBurst_Instantaneous", "Spawn Time", 0.0)]
                    + basic_particle(0.4, 60.0)
                    + [enum("ParticleSpawnScript", "ShapeLocation", "Shape Primitive", E_SHAPE, "ShapeSphere"),
                       flt("ParticleSpawnScript", "ShapeLocation", "Sphere Radius", 12.0),
                       enum("ParticleSpawnScript", "AddVelocity", "Velocity Mode", E_VELOCITY, "VelFromPoint"),
                       flt("ParticleSpawnScript", "AddVelocity", "Velocity Speed", 380.0),
                       flt("ParticleUpdateScript", "Drag", "Drag", 3.0)]
                    + no_gravity()
                    + size_over_life(SHRINK)
                    + fade_alpha()
                    + [renderer(0, SPRITE_RENDER)]
                ),
            },
            {
                "name": "Ring", "template": "SingleLoopingParticle",
                "ops": (
                    one_shot(0.35)
                    + [integer("EmitterUpdateScript", "SpawnBurst_Instantaneous", "Spawn Count", 1),
                       flt("EmitterUpdateScript", "SpawnBurst_Instantaneous", "Spawn Time", 0.0)]
                    + basic_particle(0.35, "max(User.Radius, 70.0) * 2.0")
                    + [ground_quad()]
                    + size_over_life(GROW, has_template_module=False)
                    + fade_alpha(has_template_module=False)
                    + [renderer(0, GROUND_RENDER)]
                ),
            },
        ],
    },

    # ── NS_Slash: the melee swing. An arc of sprites lying flat, a third of a
    # circle wide, gone in a quarter second. The component is spawned rotated
    # by the C++ so the arc's opening points where the attacker is facing.
    {
        "name": "NS_Slash",
        "emitters": [
            {
                "name": "Arc", "template": "OmnidirectionalBurst",
                "ops": (
                    [enum("EmitterUpdateScript", "EmitterState", "Loop Behavior", E_LOOP, "LoopOnce"),
                     flt("EmitterUpdateScript", "EmitterState", "Loop Duration", 0.25),
                     integer("EmitterUpdateScript", "SpawnBurst_Instantaneous", "Spawn Count", 28),
                     flt("EmitterUpdateScript", "SpawnBurst_Instantaneous", "Spawn Time", 0.0)]
                    + basic_particle(0.25, 70.0)
                    + [enum("ParticleSpawnScript", "ShapeLocation", "Shape Primitive", E_SHAPE, "ShapeRing"),
                       flt("ParticleSpawnScript", "ShapeLocation", "Ring Radius", 95.0),
                       flt("ParticleSpawnScript", "ShapeLocation", "Disc Coverage", 0.34),
                       enum("ParticleSpawnScript", "ShapeLocation", "Ring / Disc Distribution Mode", E_SPHEREDIST, "DistUniform"),
                       enum("ParticleSpawnScript", "AddVelocity", "Velocity Mode", E_VELOCITY, "VelFromPoint"),
                       flt("ParticleSpawnScript", "AddVelocity", "Velocity Speed", 45.0)]
                    + [ground_quad()]
                    + no_gravity()
                    + size_over_life(SHRINK)
                    + fade_alpha()
                    + [renderer(0, SPRITE_RENDER)]
                ),
            },
        ],
    },

    # ── NS_Heal: sparkles rising out of a disc at the feet.
    {
        "name": "NS_Heal",
        "emitters": [
            {
                "name": "Sparkles", "template": "Fountain",
                "ops": (
                    [enum("EmitterUpdateScript", "EmitterState", "Loop Behavior", E_LOOP, "LoopOnce"),
                     flt("EmitterUpdateScript", "EmitterState", "Loop Duration", 0.7),
                     flt("EmitterUpdateScript", "SpawnRate", "SpawnRate", 48.0)]
                    + basic_particle(1.0, 45.0)
                    + around_feet(34.0)
                    + [
                       enum("ParticleSpawnScript", "AddVelocity", "Velocity Mode", E_VELOCITY, "VelLinear"),
                       vec3("ParticleSpawnScript", "AddVelocity", "Velocity", 0.0, 0.0, 115.0),
                       flt("ParticleUpdateScript", "Drag", "Drag", 0.5)]
                    + no_gravity()
                    + size_over_life(SHRINK, has_template_module=False)
                    + fade_alpha()
                    + [renderer(0, SPRITE_RENDER)]
                    + recompile_shape()
                ),
            },
        ],
    },

    # ── NS_BuffAura: the only looping system. Runs from buffApplied until
    # buffRemoved, so nothing here ever completes on its own.
    {
        "name": "NS_BuffAura",
        "emitters": [
            {
                "name": "Ring", "template": "SingleLoopingParticle",
                "ops": (
                    held(AURA_PERIOD)
                    + [integer("EmitterUpdateScript", "SpawnBurst_Instantaneous", "Spawn Count", 1),
                       flt("EmitterUpdateScript", "SpawnBurst_Instantaneous", "Spawn Time", 0.0)]
                    + basic_particle(AURA_PERIOD, "max(User.Radius, 80.0) * 2.0")
                    + [ground_quad()]
                    # A slow breath rather than a static decal, so a buffed
                    # player reads as buffed even standing still.
                    + size_over_life("1.0 + 0.06 * sin(Particles.Age * 2.5)", has_template_module=False)
                    + [renderer(0, GROUND_RENDER)]
                ),
            },
            {
                "name": "Motes", "template": "Fountain",
                "ops": (
                    [enum("EmitterUpdateScript", "EmitterState", "Loop Behavior", E_LOOP, "LoopInfinite"),
                     flt("EmitterUpdateScript", "SpawnRate", "SpawnRate", 14.0)]
                    + basic_particle(1.6, 40.0)
                    + around_feet(46.0)
                    + [
                       enum("ParticleSpawnScript", "AddVelocity", "Velocity Mode", E_VELOCITY, "VelLinear"),
                       vec3("ParticleSpawnScript", "AddVelocity", "Velocity", 0.0, 0.0, 55.0),
                       flt("ParticleUpdateScript", "Drag", "Drag", 0.4)]
                    + no_gravity()
                    + fade_alpha()
                    + [renderer(0, SPRITE_RENDER)]
                    + recompile_shape()
                ),
            },
        ],
    },

    # ── NS_Debuff: motes dripping out of a cloud above the target's head.
    {
        "name": "NS_Debuff",
        "emitters": [
            {
                "name": "Drips", "template": "Fountain",
                "ops": (
                    [enum("EmitterUpdateScript", "EmitterState", "Loop Behavior", E_LOOP, "LoopOnce"),
                     flt("EmitterUpdateScript", "EmitterState", "Loop Duration", 0.9),
                     flt("EmitterUpdateScript", "SpawnRate", "SpawnRate", 24.0)]
                    + basic_particle(1.1, 45.0)
                    + [enum("ParticleSpawnScript", "ShapeLocation", "Shape Primitive", E_SHAPE, "ShapeSphere"),
                       flt("ParticleSpawnScript", "ShapeLocation", "Sphere Radius", 30.0),
                       # The cloud sits above the head, not at the feet. Offset
                       # Mode ships as None, which hides Offset entirely.
                       enum("ParticleSpawnScript", "ShapeLocation", "Offset Mode", E_OFFSET, "OffsetDefault"),
                       vec3("ParticleSpawnScript", "ShapeLocation", "Offset", 0.0, 0.0, 95.0),
                       enum("ParticleSpawnScript", "AddVelocity", "Velocity Mode", E_VELOCITY, "VelLinear"),
                       vec3("ParticleSpawnScript", "AddVelocity", "Velocity", 0.0, 0.0, -10.0),
                       vec3("ParticleUpdateScript", "GravityForce", "Gravity", 0.0, 0.0, -260.0),
                       flt("ParticleUpdateScript", "Drag", "Drag", 1.0)]
                    + size_over_life(SHRINK, has_template_module=False)
                    + fade_alpha()
                    + [renderer(0, SPRITE_RENDER)]
                ),
            },
        ],
    },

    # ── NS_AoERing: the ground telegraph. Sized by User.Radius, which the C++
    # sets from the skill's aoeRadius.
    {
        "name": "NS_AoERing",
        "emitters": [
            {
                "name": "Ring", "template": "SingleLoopingParticle",
                "ops": (
                    one_shot(0.8)
                    + [integer("EmitterUpdateScript", "SpawnBurst_Instantaneous", "Spawn Count", 1),
                       flt("EmitterUpdateScript", "SpawnBurst_Instantaneous", "Spawn Time", 0.0)]
                    + basic_particle(0.8, "max(User.Radius, 60.0) * 2.0")
                    + [ground_quad()]
                    + size_over_life(GROW, has_template_module=False)
                    + fade_alpha(has_template_module=False)
                    + [renderer(0, GROUND_RENDER)]
                ),
            },
            {
                "name": "Rim", "template": "OmnidirectionalBurst",
                "ops": (
                    [enum("EmitterUpdateScript", "EmitterState", "Loop Behavior", E_LOOP, "LoopOnce"),
                     flt("EmitterUpdateScript", "EmitterState", "Loop Duration", 0.7),
                     integer("EmitterUpdateScript", "SpawnBurst_Instantaneous", "Spawn Count", 30),
                     flt("EmitterUpdateScript", "SpawnBurst_Instantaneous", "Spawn Time", 0.0)]
                    + basic_particle(0.6, 50.0)
                    + [enum("ParticleSpawnScript", "ShapeLocation", "Shape Primitive", E_SHAPE, "ShapeRing"),
                       hlsl("ParticleSpawnScript", "ShapeLocation", "Ring Radius", "max(User.Radius, 60.0) * 0.9"),
                       flt("ParticleSpawnScript", "ShapeLocation", "Disc Coverage", 1.0),
                       enum("ParticleSpawnScript", "ShapeLocation", "Ring / Disc Distribution Mode", E_SPHEREDIST, "DistUniform"),
                       enum("ParticleSpawnScript", "AddVelocity", "Velocity Mode", E_VELOCITY, "VelLinear"),
                       vec3("ParticleSpawnScript", "AddVelocity", "Velocity", 0.0, 0.0, 130.0),
                       flt("ParticleUpdateScript", "Drag", "Drag", 1.2)]
                    + no_gravity()
                    + size_over_life(SHRINK)
                    + fade_alpha()
                    + [renderer(0, SPRITE_RENDER)]
                ),
            },
        ],
    },

    # ── NS_Cone: a fan thrown along the component's +X, which the C++ aligns
    # with the caster's facing.
    {
        "name": "NS_Cone",
        "emitters": [
            {
                "name": "Fan", "template": "DirectionalBurst",
                "ops": (
                    [enum("EmitterUpdateScript", "EmitterState", "Loop Behavior", E_LOOP, "LoopOnce"),
                     flt("EmitterUpdateScript", "EmitterState", "Loop Duration", 0.5),
                     integer("EmitterUpdateScript", "SpawnBurst_Instantaneous", "Spawn Count", 40),
                     flt("EmitterUpdateScript", "SpawnBurst_Instantaneous", "Spawn Time", 0.0)]
                    + basic_particle(0.45, 65.0)
                    + [enum("ParticleSpawnScript", "AddVelocity", "Velocity Mode", E_VELOCITY, "VelInCone"),
                       vec3("ParticleSpawnScript", "AddVelocity", "Cone Axis", 1.0, 0.0, 0.0),
                       flt("ParticleSpawnScript", "AddVelocity", "Cone Angle", 55.0),
                       hlsl("ParticleSpawnScript", "AddVelocity", "Velocity Speed", "max(User.Radius, 180.0) / 0.45"),
                       flt("ParticleUpdateScript", "Drag", "Drag", 0.8),
                       # DirectionalBurst is the one template that sizes its
                       # sprites by how fast they are going. A cone that is
                       # thrown at max(User.Radius, 180) / 0.45 cm/s and then
                       # dragged would taper by speed *and* by age, which is a
                       # shrink nobody asked for on top of the one below.
                       del_module("ParticleUpdateScript", "ScaleSpriteSizeBySpeed")]
                    + no_gravity()
                    + size_over_life(SHRINK, has_template_module=False)
                    + fade_alpha()
                    + [renderer(0, SPRITE_RENDER)]
                ),
            },
        ],
    },
]


# ── Execution ────────────────────────────────────────────────────────────────

NS = "NiagaraToolsets.NiagaraToolset_System."


def _call(name, args):
    return execute_tool(NS + name, json.dumps(args))["returnValue"]  # noqa: F821


def _ref(system, emitter="", script="", module="", ridx=-1, stack=None):
    return {"system": system, "emitterName": emitter, "scriptName": script,
            "moduleName": module, "rendererIndex": ridx,
            "inputNameStack": stack or []}


def _set_input(system, emitter, script, module, name, struct, value):
    _call("SetStackInputData", {
        "stackInputRef": _ref(system, emitter, script, module, -1, [name]),
        "inputData": {"struct": {"refPath": struct}, "value": value}})


def _apply(system, emitter, op, log):
    kind = op[0]
    try:
        if kind == "enum":
            _, script, module, name, enum_path, enum_name, display = op
            _set_input(system, emitter, script, module, name, W_ENUM,
                       {"enum": {"refPath": enum_path}, "enumName": enum_name,
                        "displayName": display})
        elif kind == "scalar":
            _, script, module, name, struct, value = op
            _set_input(system, emitter, script, module, name, struct, value)
        elif kind == "link":
            _, script, module, name, var, type_path = op
            _set_input(system, emitter, script, module, name, W_LINK,
                       {"linkedVariable": {"name": var,
                                           "type": {"classStructOrEnum": {"refPath": type_path}}}})
        elif kind == "hlsl":
            _, script, module, name, expression = op
            _set_input(system, emitter, script, module, name, W_HLSL,
                       {"hlslExpression": expression})
        elif kind == "addmod":
            _, script, asset = op
            _call("AddModule", {
                "moduleLocationRef": _ref(system, emitter, script),
                "moduleAsset": {"refPath": asset}})
        elif kind == "delmod":
            _, script, module = op
            _call("RemoveModule", {
                "moduleToRemove": _ref(system, emitter, script, module)})
        elif kind == "groundquad":
            _call("AddSetParametersModule", {
                "moduleLocationRef": _ref(system, emitter, "ParticleSpawnScript"),
                "parameters": [
                    {"variable": {"name": "Particles.SpriteFacing",
                                  "type": {"classStructOrEnum": {"refPath": T_V3}}},
                     "defaultValue": {"struct": {"refPath": T_V3},
                                      "value": {"x": 0.0, "y": 0.0, "z": 1.0}}},
                    {"variable": {"name": "Particles.SpriteAlignment",
                                  "type": {"classStructOrEnum": {"refPath": T_V3}}},
                     "defaultValue": {"struct": {"refPath": T_V3},
                                      "value": {"x": 1.0, "y": 0.0, "z": 0.0}}},
                ]})
        elif kind == "renderer":
            _, index, props = op
            _call("SetRendererData", {
                "renderer": _ref(system, emitter, "", "", index, []),
                "rendererData": {"propertyValues": json.dumps(props)}})
        else:
            raise RuntimeError("unknown op " + kind)
    except BaseException as error:      # noqa: BLE001 - reported, not swallowed
        log.append("{}/{} {}: {}".format(emitter, kind, op[1:4], str(error)[:220]))


def _build_system(spec, log):
    path = "{}/{}".format(VFX_DIR, spec["name"])
    system = {"refPath": "{}.{}".format(path, spec["name"])}

    _call("CreateNiagaraSystem", {
        "assetName": spec["name"], "assetPath": VFX_DIR,
        "templateSystem": {"refPath": BASE_SYSTEM}})

    # The clone arrives carrying DefaultSystem's Fountain emitter and whatever
    # a previous run of this script left behind. Strip every emitter, so a
    # re-run produces the same asset rather than a doubled one.
    summary = json.loads(json.dumps(_call("GetSystemSummary", {"system": system})))
    for existing in summary["emitters"]:
        _call("RemoveEmitter", {"emitterToRemove": _ref(system, existing["emitterName"])})

    _call("AddUserVariables", {"system": system, "variablesToAdd": [
        {"name": "Color", "description": "Palette tint, set per skill by UValhallaVfxLibrary.",
         "type": {"classStructOrEnum": {"refPath": T_COLOR}},
         "defaultValue": {"struct": {"refPath": T_COLOR},
                          "value": {"r": 1.0, "g": 0.55, "b": 0.2, "a": 1.0}}},
        {"name": "Radius", "description": "Effect size in centimetres.",
         "type": {"classStructOrEnum": {"refPath": T_FLOAT}},
         "defaultValue": {"struct": {"refPath": T_FLOAT}, "value": {"value": 100.0}}},
    ]})

    for emitter in spec["emitters"]:
        _call("AddEmitter", {
            "system": system,
            "templateEmitter": {"refPath": "{}{}.{}".format(
                TEMPLATE_DIR, emitter["template"], emitter["template"])},
            "emitterName": emitter["name"]})
        for op in emitter["ops"]:
            _apply(system, emitter["name"], op, log)

    return path, system


def run():
    log = []
    built = []
    for spec in SYSTEMS:
        try:
            path, _ = _build_system(spec, log)
            built.append(path)
        except BaseException as error:      # noqa: BLE001
            log.append("{}: FATAL {}".format(spec["name"], str(error)[:300]))
    return {"built": built, "problems": log}


def verify():
    """Compile state and stack issues for the eight systems. Run after ``run``."""
    report = {}
    for spec in SYSTEMS:
        system = {"refPath": "{}/{}.{}".format(VFX_DIR, spec["name"], spec["name"])}
        entry = {}
        try:
            state = json.loads(json.dumps(_call("GetSystemCompileState", {"system": system})))
            entry["status"] = state["aggregateStatus"]
            entry["errors"] = state["bHasErrors"]
            entry["bad"] = [s["emitterName"] + "/" + s["scriptName"] + ": " + s["errorSummary"][:160]
                            for s in state["scripts"] if s["lastCompileStatus"] != "UpToDate"][:6]
            issues = json.loads(json.dumps(_call("GetStackIssues", {"system": system})))
            entry["numErrors"] = issues["numErrors"]
            entry["numWarnings"] = issues["numWarnings"]
            entry["issues"] = [i["shortDescription"] + " | " + i["longDescription"][:120]
                               for i in issues["issues"] if i["severity"] in ("Error", "Warning")][:6]
        except BaseException as error:      # noqa: BLE001
            entry["error"] = str(error)[:300]
        report[spec["name"]] = entry
    return report
