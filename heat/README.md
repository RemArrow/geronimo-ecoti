# ECOTIHeat: the cooked materials

`Mods\ECOTIHeat` is a small Unreal content plugin. GML mirrors it into the game's `Mods` folder. It holds:

- `M_ECOTIHeat`: the heat overlay on characters.
- `M_ECOTIHud`: the fusion HUD's render target.
- `SM_ECOTIHudQuad`: the HUD's plane.

Both materials draw only where custom stencil = 250. That is where the goggle's lenses are on screen while it is
powered, and the same mask the game's night-vision picture uses.

To rebuild it you need Unreal Engine 5.7 and a project named `Geronimo`, because a plugin's mount point is
`../../../Geronimo/Mods/<Plugin>/`.

1. Put `ECOTIHeat.uplugin` in `<project>\Mods\ECOTIHeat\`.
2. Run the three scripts. Edit the paths at their tops first (`OUT`, and the `.obj` path in
   `make_ecoti_hudquad.py`):
   `UnrealEditor-Cmd <project>.uproject -ExecutePythonScript=<script> -unattended`
3. Cook it as DLC:
   `RunUAT BuildCookRun -project=<project>.uproject -platform=Win64 -clientconfig=Shipping -cook -stage -pak -iostore -skipbuild -nocompile -nocompileeditor -dlcname=ECOTIHeat -basedonreleaseversion=<a base release>`
4. Copy `Saved\StagedBuilds\Windows\Geronimo\Mods\ECOTIHeat\Content\Paks\Windows\*` to
   `Mods\ECOTIHeat\Content\Paks\Windows\`.

The project must target **SM6**. The game renders with D3D12 SM6, and a material with only SM5 shaders draws nothing.
Put this in `Config\DefaultEngine.ini`:

```ini
[/Script/WindowsTargetPlatform.WindowsTargetSettings]
DefaultGraphicsRHI=DefaultGraphicsRHI_DX12
+D3D12TargetedShaderFormats=PCD3D_SM6
+D3D12TargetedShaderFormats=PCD3D_SM5
```

Check a cooked material's `.uexp` for the text `PCD3D_SM6`.
