# Builds /ECOTIHeat/M_ECOTIHeat: the ECOTI Thermal Fusion plugin's heat overlay for characters.
# Translucent unlit, usable on skeletal meshes, no vertex colour. Emissive = BaseColorFactor.rgb; opacity =
# BaseColorFactor.a where the night-vision goggle's lenses are on screen (they write custom stencil 250 while
# powered - the game's own NVG picture uses the same mask) and 0 everywhere else, so the heat never shows in the
# bare-eye view around the tubes.
import unreal

PATH, NAME = "/ECOTIHeat", "M_ECOTIHeat"
OUT = r"E:\Geronimo MODS\UEMaps\make_ecoti_heat.txt"
log = []
full = PATH + "/" + NAME
if unreal.EditorAssetLibrary.does_asset_exist(full):
    unreal.EditorAssetLibrary.delete_asset(full)
tools = unreal.AssetToolsHelpers.get_asset_tools()
m = tools.create_asset(NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())
m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
m.set_editor_property("used_with_skeletal_mesh", True)
m.set_editor_property("used_with_morph_targets", True)
m.set_editor_property("two_sided", False)
for prop in ("apply_fogging", "compute_fog_per_pixel"):
    try: m.set_editor_property(prop, False)
    except Exception as e: log.append(f"skip {prop}: {e}")

mel = unreal.MaterialEditingLibrary
col = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -800, 0)
col.set_editor_property("parameter_name", "BaseColorFactor")
col.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 0.7))
mel.connect_material_property(col, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

st = mel.create_material_expression(m, unreal.MaterialExpressionSceneTexture, -800, 300)
st.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_CUSTOM_STENCIL)
mask = mel.create_material_expression(m, unreal.MaterialExpressionComponentMask, -600, 300)
mask.set_editor_property("r", True); mask.set_editor_property("g", False); mask.set_editor_property("b", False); mask.set_editor_property("a", False)
mel.connect_material_expressions(st, "Color", mask, "")
sub = mel.create_material_expression(m, unreal.MaterialExpressionSubtract, -450, 300)
sub.set_editor_property("const_b", 250.0)
mel.connect_material_expressions(mask, "", sub, "A")
ab = mel.create_material_expression(m, unreal.MaterialExpressionAbs, -300, 300)
mel.connect_material_expressions(sub, "", ab, "")
inv = mel.create_material_expression(m, unreal.MaterialExpressionOneMinus, -150, 300)
mel.connect_material_expressions(ab, "", inv, "")
sat = mel.create_material_expression(m, unreal.MaterialExpressionSaturate, 0, 300)
mel.connect_material_expressions(inv, "", sat, "")
alpha = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, 150, 200)
mel.connect_material_expressions(col, "A", alpha, "A")
mel.connect_material_expressions(sat, "", alpha, "B")
mel.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY)

mel.recompile_material(m)
unreal.EditorAssetLibrary.save_asset(full)
log.append(f"saved {full}: exists {unreal.EditorAssetLibrary.does_asset_exist(full)}")
open(OUT, "w").write("\n".join(log) + "\n")
