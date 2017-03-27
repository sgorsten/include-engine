# include-engine
### `#include <engine>`

This repository was inspired by a conversation I had with [Stan Melax](https://github.com/melax) the other day, in which he lamented that game engines were so heavyweight and it wasn't possible to simply `#include <unity>` or `#include <unreal>`. While these game engines provide a wealth of services to kickstart the development of your game, you essentially give up control over the lifetime of your application and wind up writing most of your scripts inside a sandboxed environment (Unity's C# APIs or Unreal's Blueprint system).

This project will therefore strive to provide game engine-like services while allowing the programmer to remain in control of the main loop of their program. Note that this project is extremely early and is unlikely to be tremendously useful to anyone in its present state.
