---
description: A README with what the project is, its features, how to build and use it, and its licence
file: README.md
whole-file: yes
ask: ProjectName = My Project
order: 10
section: documents
---
# {{ProjectName}}

{{cursor}}

## Features

- First feature
- Second feature

## Building

Needs CMake 3.20 or newer and a C++20 compiler (g++ 11 or newer).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Usage

```sh
./build/{{ProjectName:kebab}} --help
```

## Contributing

Bug reports and pull requests are welcome; see [CONTRIBUTING.md](CONTRIBUTING.md).

## Licence

See [LICENSE](LICENSE).
