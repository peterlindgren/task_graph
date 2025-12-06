# task_graph

A minimal C++ task graph experiment exploring low-boilerplate task authoring with compile-time dependency declaration.

Currently requires C++11 support.

## Usage

Define tasks by inheriting from `Task<Dependencies...>`:
```cpp
struct LoadMesh : Task<> {
    Mesh mesh;
    void operator()() override {
        mesh = load_mesh("model.obj");
    }
};

struct LoadTexture : Task<> {
    Texture tex;
    void operator()() override {
        tex = load_texture("albedo.png");
    }
};

struct CreateMaterial : Task<LoadMesh, LoadTexture> {
    using Task::Task;
    void operator()() override {
        LoadMesh *mesh = std::get<0>(in);
        LoadTexture *tex = std::get<1>(in);
        // both guaranteed complete
    }
};
```

Wire up and run:
```cpp
LoadMesh mesh;
LoadTexture tex;
CreateMaterial mat(&mesh, &tex);

TaskGraph g(&mesh, &tex, &mat);
g.submit(pool);
g.wait(pool);
```

Dependencies are declared in the type, passed to the constructor, and accessible via `std::get<N>(in)`.

## Building
```
build.bat
```

Requires MSVC. Uses [Bikeshed](https://github.com/DanEngelbrecht/bikeshed) for scheduling.

## License

Unlicense