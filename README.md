# C/C++ Projects

Welcome to my collection of C and C++ projects. This repository documents my journey in understanding low-level computing by building core tools and data structures from scratch.

## 📂 Project List

### 1. [Text Editor From Scratch (Kilo)](./building_my_own_text_editor/)
A lightweight, terminal-based text editor built in C without any external libraries (only standard `libc`).
* **Tech:** C, Makefiles, VT100 Escape Sequences
* **Key Features:** * Syntax highlighting (C/C++).
  * Search functionality.
  * Raw mode input handling and window resizing.

### 2. [Custom Hashmap Implementation](./HashMap_from_scratch/)
A high-performance Hashmap data structure built from the ground up to understand memory management and algorithmic complexity.
* **Tech:** C/C++
* **Key Features:** * Custom hash function implementation.
  * Collision resolution (Chaining/Open Addressing).
  * Dynamic resizing and memory management.

### 3. [Memory Allocator From Scratch](./Memory_Allocator_from_scratch/)

A custom implementation of the C standard library memory management functions (malloc, free, calloc, realloc) designed to replace the system allocator at runtime.

**Tech:** C, Pthreads, Linux System Calls ( `sbrk`, `unistd.h`)

**Key Features:**

* Thread-safe memory allocation using mutex locks.

* Manual heap management using sbrk and pointer arithmetic.

* Library interposition (`LD_PRELOAD`) to inject the allocator into standard system commands like `ls`.



---

### 🛠️ How to Run
Click on the project titles above to visit their directories. Each project contains its own `README.md` with specific build and run instructions.