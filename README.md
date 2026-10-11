# Buddy Allocator <->

This `README` provides an overview of the project structure, how the allocator works, and instructions for building and integrating it.

---

## Project Structure <->

The project follows a standard C++ layout using CMake for the build system:

* `CMakeLists.txt`: Build configuration file for the project.
* `LICENSE`: Licensing information for your code.
* `README.md`: High-level project overview and quick start guide.
* `src/main.cpp`: Core implementation of the buddy allocator algorithm and entry point.

---

## How the Buddy Allocator Works <->

A buddy allocator is a memory allocation algorithm that manages memory in power-of-two blocks, partitioning memory into halves to try to give a best-fit allocation. 

### Key Concepts <->
* **Block Splitting**: When an allocation request comes in, the allocator finds the smallest available block of size $2^k$ that is greater than or equal to the requested size. If the current block is larger than necessary, it recursively splits the block into two equal "buddy" halves until the appropriate size is reached.
* **Block Merging (Coalescing)**: When memory is freed, the allocator checks if the block's buddy is also free. If both buddies are free, they are merged back together into a single larger block of size $2^{k+1}$. This process repeats recursively to prevent external fragmentation.

---

## Building and Running <->

To build and run the project using CMake, execute the following commands in your terminal:

```bash
mkdir build
cd build
cmake ..
cmake --build . --target run
./buddy_allocator
```

And then profit.

## File Structure <->

```txt
BuddyAllocator
├── CMakeLists.txt
├── LICENSE
├── README.md
└── src
    └── main.cpp
```

## License <->

This project is licensed under the [MIT License](./LICENSE).