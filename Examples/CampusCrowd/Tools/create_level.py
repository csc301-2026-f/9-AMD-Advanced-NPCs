"""Run with UnrealEditor-Cmd -run=pythonscript -script=<this file>."""
import unreal
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.MaterialEditingLibrary

def material(name, color=None, crowd=False):
    path = '/Game/Campus/' + name
    mat = unreal.load_asset(path)
    if mat:
        mat.set_editor_property('two_sided', True)
        if crowd:
            lib.set_material_usage(mat, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
        lib.recompile_material(mat)
        unreal.EditorAssetLibrary.save_loaded_asset(mat)
        return mat
    mat = asset_tools.create_asset(name, '/Game/Campus', unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property('two_sided', True)
    if crowd:
        rand = lib.create_material_expression(mat, unreal.MaterialExpressionPerInstanceRandom)
        lerp = lib.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate)
        for pin, rgb in [('A', (.04,.15,.24)), ('B', (.75,.32,.08))]:
            c = lib.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector)
            c.set_editor_property('constant', unreal.LinearColor(*rgb))
            lib.connect_material_expressions(c, '', lerp, pin)
        lib.connect_material_expressions(rand, '', lerp, 'Alpha')
        node = lerp
    elif color:
        node = lib.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector)
        node.set_editor_property('constant', unreal.LinearColor(*color))
    else:
        node = lib.create_material_expression(mat, unreal.MaterialExpressionVertexColor)
    lib.connect_material_property(node, '', unreal.MaterialProperty.MP_BASE_COLOR)
    rough = lib.create_material_expression(mat, unreal.MaterialExpressionConstant)
    rough.set_editor_property('r', .85)
    lib.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
    if crowd:
        lib.set_material_usage(mat, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    return mat

material('M_Campus')
material('M_Students', crowd=True)
sub = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if unreal.EditorAssetLibrary.does_asset_exist('/Game/Maps/NorthCampus'):
    assert sub.load_level('/Game/Maps/NorthCampus')
else:
    assert sub.new_level('/Game/Maps/NorthCampus')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
scene_class = unreal.load_class(None, '/Script/CampusCrowd.CampusScene')
scene = next((a for a in actors.get_all_level_actors() if a.get_class() == scene_class), None)
if scene is None:
    scene = actors.spawn_actor_from_class(scene_class, unreal.Vector(0,0,0))
scene.set_actor_label('50H NORTH - CAD campus blockout')
def ensure_actor(cls, label, rotation=None):
    actor = next((a for a in actors.get_all_level_actors() if isinstance(a, cls)), None)
    if actor is None:
        actor = actors.spawn_actor_from_class(cls, unreal.Vector(0,0,0), rotation or unreal.Rotator())
    actor.set_actor_label(label)
    return actor
sun = ensure_actor(unreal.DirectionalLight, 'Campus sun', unreal.Rotator(-50,-35,0))
sun.light_component.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
sun.light_component.set_editor_property('intensity', 6.0)
sun.light_component.set_editor_property('atmosphere_sun_light', True)
ambient = ensure_actor(unreal.SkyLight, 'Campus skylight')
ambient.light_component.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
ambient.light_component.set_editor_property('intensity', 1.5)
ambient.light_component.set_editor_property('real_time_capture', True)
ensure_actor(unreal.SkyAtmosphere, 'Campus atmosphere')
assert sub.save_current_level()
unreal.log('CAMPUS_LEVEL_READY /Game/Maps/NorthCampus')
