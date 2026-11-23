<!--
Silicon LogiX / SLX SeaBalance
Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
Author: Marco Pezzullo
License: Silicon LogiX Evaluation License 1.0. See LICENSE.
-->

# Third-party notices

| Component | Use | Upstream notice |
| --- | --- | --- |
| GLFW | Windows OpenGL windowing | [GLFW license](licenses/glfw-LICENSE.txt) |
| SDL gamepad mappings | Mappings included by GLFW | [Notices in `mappings.h`](https://github.com/glfw/glfw/blob/3.4/src/mappings.h) |
| Dear ImGui | Desktop controls | [Dear ImGui license](licenses/imgui-LICENSE.txt) |
| ImPlot | Telemetry graphs | [ImPlot license](licenses/implot-LICENSE.txt) |
| cgltf | GLB ingestion | [cgltf license](licenses/cgltf-LICENSE.txt) |
| stb_image and stb_image_write | Embedded images and PNG capture | [stb license](licenses/stb-LICENSE.txt) |
| nlohmann/json | Metadata and telemetry snapshots | [MIT license](licenses/json-LICENSE.MIT) |
| Khronos headers | OpenGL types and declarations | [Khronos notice](licenses/Khronos-NOTICE.txt) |
| Dear ImGui glyph ranges | Optional character ranges in the library | [Glyph notices](licenses/imgui-glyph-ranges-NOTICE.txt) |
| ProggyClean | Dear ImGui bundled fallback font | [ProggyClean notice](licenses/ProggyClean-NOTICE.txt) |

Windows builds use MSVC and the Windows SDK. The GitHub source inventory
excludes the legacy MinGW fallback headers. Each vendored source file retains
its upstream attribution.

The Linux desktop build uses the system GLFW package. The optional Linux panel
links dynamically to system GTK 3, GLib, Pango, GdkPixbuf and Cairo. OpenGL
drivers and installed fonts come from the host system. These system dependencies
retain their own licenses and notices. OpenSTLinux components come from the ST
distribution, with licensing described by the applicable image manifests.
