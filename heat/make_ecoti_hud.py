# Builds /ECOTIHeat/M_ECOTIHud: the Panoramic Fusion goggle's HUD plane material (ECOTI Thermal Fusion plugin).
# Translucent unlit, no vertex colour. Emissive = BaseColorTexture.rgb * BaseColorFactor.rgb; opacity =
# BaseColorTexture.a * BaseColorFactor.a where the goggle's lenses are on screen (custom stencil 250, the mask of
# the game's night-vision picture), so the HUD shows only inside the tubes. Same parameter names as the engine's
# glTF unlit material, which it replaces.
import unreal

PATH, NAME = "/ECOTIHeat", "M_ECOTIHud"
OUT = r"E:\Geronimo MODS\UEMaps\make_ecoti_hud.txt"
log = []
full = PATH + "/" + NAME
if unreal.EditorAssetLibrary.does_asset_exist(full):
    unreal.EditorAssetLibrary.delete_asset(full)
tools = unreal.AssetToolsHelpers.get_asset_tools()
m = tools.create_asset(NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())
m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
m.set_editor_property("two_sided", True)

mel = unreal.MaterialEditingLibrary
tex = mel.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, -900, -100)
tex.set_editor_property("parameter_name", "BaseColorTexture")
tex.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
deft = unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture")
if deft:
    tex.set_editor_property("texture", deft)
    try: tex.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    except Exception: pass
log.append(f"default texture {deft}")
col = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -900, 150)
col.set_editor_property("parameter_name", "BaseColorFactor")
col.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))
rgb = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -600, -50)
mel.connect_material_expressions(tex, "RGB", rgb, "A")
mel.connect_material_expressions(col, "", rgb, "B")
mel.connect_material_property(rgb, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

st = mel.create_material_expression(m, unreal.MaterialExpressionSceneTexture, -900, 400)
st.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_CUSTOM_STENCIL)
mask = mel.create_material_expression(m, unreal.MaterialExpressionComponentMask, -700, 400)
mask.set_editor_property("r", True); mask.set_editor_property("g", False); mask.set_editor_property("b", False); mask.set_editor_property("a", False)
mel.connect_material_expressions(st, "Color", mask, "")
sub = mel.create_material_expression(m, unreal.MaterialExpressionSubtract, -550, 400)
sub.set_editor_property("const_b", 250.0)
mel.connect_material_expressions(mask, "", sub, "A")
ab = mel.create_material_expression(m, unreal.MaterialExpressionAbs, -420, 400)
mel.connect_material_expressions(sub, "", ab, "")
inv = mel.create_material_expression(m, unreal.MaterialExpressionOneMinus, -300, 400)
mel.connect_material_expressions(ab, "", inv, "")
sat = mel.create_material_expression(m, unreal.MaterialExpressionSaturate, -180, 400)
mel.connect_material_expressions(inv, "", sat, "")
a1 = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -450, 200)
mel.connect_material_expressions(tex, "A", a1, "A")
mel.connect_material_expressions(col, "A", a1, "B")
a2 = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -50, 250)
mel.connect_material_expressions(a1, "", a2, "A")
mel.connect_material_expressions(sat, "", a2, "B")
mel.connect_material_property(a2, "", unreal.MaterialProperty.MP_OPACITY)

mel.recompile_material(m)
unreal.EditorAssetLibrary.save_asset(full)
log.append(f"saved {full}: exists {unreal.EditorAssetLibrary.does_asset_exist(full)}")
open(OUT, "w").write("\n".join(log) + "\n")
