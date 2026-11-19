# Imports /ECOTIHeat/SM_ECOTIHudQuad (UEMaps\import\SM_ECOTIHudQuad.obj): the ECOTI fusion HUD's plane as a static mesh
# (a GML dynamic mesh does not draw our cooked materials). Axes kept as authored (Unreal: X ahead, Y right, Z up).
import unreal
OUT = r"E:\Geronimo MODS\UEMaps\make_ecoti_hudquad.txt"
log = []
full = "/ECOTIHeat/SM_ECOTIHudQuad"
if unreal.EditorAssetLibrary.does_asset_exist(full):
    unreal.EditorAssetLibrary.delete_asset(full)
t = unreal.AssetImportTask()
t.filename = r"E:\Geronimo MODS\UEMaps\import\SM_ECOTIHudQuad.obj"
t.destination_path = "/ECOTIHeat"
t.destination_name = "SM_ECOTIHudQuad"
t.automated = True
t.save = True
t.replace_existing = True
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
log.append(f"imported: {list(t.imported_object_paths)}")
for p in t.imported_object_paths:
    a = unreal.load_asset(p)
    log.append(f"{p}: {type(a).__name__}")
    if isinstance(a, unreal.StaticMesh):
        bb = a.get_bounding_box()
        log.append(f"  bounds {bb.min} .. {bb.max}, materials {a.get_num_sections(0)}")
        if p.split(".")[0] != full:
            unreal.EditorAssetLibrary.rename_asset(p.split(".")[0], full)
mats = unreal.EditorAssetLibrary.list_assets("/ECOTIHeat", recursive=True)
log.append(f"assets: {list(mats)}")
open(OUT, "w").write("\n".join(log) + "\n")
