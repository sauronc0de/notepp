# C++ Naming and Syntax Style Guide

This document defines **common naming conventions and syntax rules** for
writing consistent and readable C++ code.

The goal is to ensure:

-   Consistency
-   Readability
-   Maintainability
-   Predictable structure across the codebase

These conventions are inspired by commonly used industry styles such as:

-   Google C++ Style Guide
-   LLVM Coding Standards
-   C++ Core Guidelines

------------------------------------------------------------------------

# 1. File Naming

Use **snake_case** for file names.

    physics_system.cpp
    physics_system.hpp
    player_controller.cpp
    player_controller.hpp

Rules:

-   Lowercase only
-   Words separated with `_`
-   Match class name if the file contains a primary class

Example:

> class PlayerController

File:

    player_controller.hpp

------------------------------------------------------------------------

# 2. Class Names

Use **PascalCase**.

``` cpp
class PlayerController
class PhysicsSystem
class NetworkManager
```

Rules:

-   First letter uppercase
-   Each word capitalized
-   No underscores

Avoid:

``` cpp
class player_controller
class playerController
```

------------------------------------------------------------------------

# 3. Struct Names

Use **PascalCase**.

``` cpp
struct Vector3
struct Transform
struct Vertex
```

------------------------------------------------------------------------

# 4. Enum Names

Use **PascalCase** for enum types.

``` cpp
enum class RenderMode
{
    Opaque,
    Transparent,
    Additive
};
```

Enum values should also use **PascalCase**.

Avoid:

``` cpp
RENDER_MODE_OPAQUE
```

------------------------------------------------------------------------

# 5. Function Names

Use **camelCase**.

``` cpp
void updatePhysics();
int calculateDamage();
void loadTexture();
```

Rules:

-   First word lowercase
-   Following words uppercase

Avoid:

``` cpp
UpdatePhysics()
calculate_damage()
```

------------------------------------------------------------------------

# 6. Variable Names

Use **camelCase**.

``` cpp
int playerHealth;
float movementSpeed;
Vector3 playerPosition;
```

Rules:

-   Start lowercase
-   Avoid unclear abbreviations

------------------------------------------------------------------------

# 7. Member Variables

### Trailing underscore

``` cpp
class Player
{
private:
    int health_;
    float speed_;
};
```

Benefits:

-   Distinguishes member variables
-   Avoids naming conflicts

Example:

``` cpp
health_ = health;
```

# 8. Constants

Use **kPrefix + PascalCase**.

``` cpp
constexpr int kMaxPlayers = 64;
constexpr float kGravity = 9.81f;
```

Avoid:

``` cpp
MAX_PLAYERS
```

------------------------------------------------------------------------

# 9. Global Variables

Avoid global variables when possible.

If required use prefix `g_`.

``` cpp
Renderer* g_renderer;
Window* g_window;
```

------------------------------------------------------------------------

# 10. Namespaces

Use **snake_case**.

``` cpp
namespace engine
{
namespace physics
{
}
}
```

Example:

``` cpp
namespace game_engine
```

Avoid:

``` cpp
namespace GameEngine
```

------------------------------------------------------------------------

# 11. Template Parameters

Use **PascalCase**.

``` cpp
template<typename T>
template<typename Key, typename Value>
template<typename ComponentType>
```

If semantic meaning is important:

``` cpp
template<typename Allocator>
template<typename Iterator>
```

------------------------------------------------------------------------

# 12. Boolean Variables

Use descriptive prefixes:

-   `is`
-   `has`
-   `can`
-   `should`

Examples:

``` cpp
bool isVisible;
bool hasFinished;
bool canJump;
bool shouldReload;
```

------------------------------------------------------------------------

# 13. Include Order

Recommended include order:

1.  Corresponding header
2.  Standard library headers
3.  Third‑party libraries
4.  Project headers

Example:

``` cpp
#include "player_controller.hpp"

#include <vector>
#include <string>

#include <glm/glm.hpp>

#include "physics_system.hpp"
```

------------------------------------------------------------------------

# 14. Using Declarations

Avoid:

``` cpp
using namespace std;
```

Prefer explicit namespaces:

``` cpp
std::vector<int> values;
```

------------------------------------------------------------------------

# 15. Class Layout

Recommended class structure:

``` cpp
class Player
{
public:
    Player();
    ~Player();

    void update();
    void render();

private:
    void calculateMovement();

private:
    int health_;
    float speed_;
};
```

Order:

1.  Constructors
2.  Public methods
3.  Private methods
4.  Member variables

------------------------------------------------------------------------

# 16. Constructor Initialization

Prefer **initializer lists**.

``` cpp
Player::Player(int health)
    : health_(health)
{
}
```

Avoid:

``` cpp
Player::Player(int health)
{
    health_ = health;
}
```

------------------------------------------------------------------------

# 17. Use of `auto`

Use `auto` when the type is obvious.

Good:

``` cpp
auto it = players.begin();
```

Avoid overusing it when the type is unclear.

------------------------------------------------------------------------

# 18. Abbreviations

Avoid unclear abbreviations.

Bad:

``` cpp
int cnt;
int calcVal();
```

Better:

``` cpp
int count;
int calculateValue();
```

------------------------------------------------------------------------

# Summary

| Element | Style |
|-------|-------|
| Classes | PascalCase |
| Structs | PascalCase |
| Functions | camelCase |
| Variables | camelCase |
| Member variables | trailing `_` |
| Constants | `kPascalCase` |
| Namespaces | snake_case |
| Files | snake_case |
| Templates | PascalCase |
| Booleans | `is/has/can/should` prefix |

------------------------------------------------------------------------

# Example

``` cpp
namespace game_engine
{

class PlayerController
{
public:
    PlayerController();

    void update(float deltaTime);

private:
    int health_;
    float speed_;
};

}
```
