# include-engine
### `#include <engine>`

This repository was inspired by a conversation I had with [Stan Melax](https://github.com/melax) the other day, in which he lamented that game engines were so heavyweight and it wasn't possible to simply `#include <unity>` or `#include <unreal>`. While these game engines provide a wealth of services to kickstart the development of your game, you essentially give up control over the lifetime of your application and wind up writing most of your scripts inside a sandboxed environment (Unity's C# APIs or Unreal's Blueprint system).

This project will therefore strive to provide game engine-like services while allowing the programmer to remain in control of the main loop of their program. Note that this project is extremely early and is unlikely to be tremendously useful to anyone in its present state. In particular, you should assume that **NONE** of the functionality listed below has been implemented in any form.

### Rough design goals

The application owns the main loop, and therefore, `#include <engine>` will have no concept of game object hierarchies, entity-component systems, scene graphs or scripting languages. The application programmer chooses whichever data structures they want to represent their game world, and can code the game logic directly in C++ or in any scripting language they feel like integrating. The world can be as structured or unstructured, as explicit or procedural as the programmer desires.

Instead, I envision `#include <engine>` as a collection of lightweight, composable, "immediate-mode" subsystems. For instance, the renderer will not retain any notion of the scene from frame to frame. When you are ready to draw a frame, you will traverse your game's data structures in whatever manner is most convenient, and issue draw calls, which may reference both long-lived assets and transient data generated for that frame only. The renderer will provide automatic services such as reordering of draw calls for depth ordering, transparent objects, etc., as well as automatic resubmission of draw calls to render shadowmaps, depth prepasses, and the lighting pass, but the data structures required to do so will only live for the lifetime of the frame. This means that your world can change drastically from frame to frame, or even be entirely procedural, without needing to worry about the overhead of spawning and destroying rendergraph objects. In cases where you DO need to retain state from frame to frame, such as the current state of a particle system, we will provide data types to carry that state, which your code will be expected to pass to the renderer on an as-needed basis. 

The rough feature set I'm aiming for would look something like this:

- Support for loading many different asset formats (textures, models, shaders, fonts, etc.)
- A library of pure functions for analyzing, manipulating, and transforming assets and other data structures
- An "immediate mode plus" renderer targeting a Vulkan backend
- An immediate mode GUI subsystem for building tools and editors
- A flexible asset packaging system that can be used to author packages at runtime and optimize load times

The idea is that you should be able to quickly pull in some assets and start rendering 3D scenes, and that as your game evolves, you can author your tools and editors by coding against the same framework.

### Target Environment

- The engine itself will be written in C++17 and aim to compile cleanly on Visual Studio 2017, GCC, and Clang.
- The engine will consume shaders in SPIR-V and will initially target Vulkan on Windows and Linux. A Metal backend for OS X support is a possibility once the renderer API crystalizes.
- Platform specific window creation and input handling will use GLFW windows created with the `GLFW_NO_CLIENT` hint.
- We will link against the C++ standard library, use RTTI and throw exceptions. Deal with it.
