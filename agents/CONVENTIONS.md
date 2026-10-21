# Codebase Conventions & Engineering Standards

> **Notice:** This document establishes the strict coding standards, formatting rules, comment conventions, and architectural idioms for this codebase. All contributors and AI agents must adhere to these conventions.

---

* **Commenting and Documentation Conventions**
  * **Casing, Spacing, and Punctuation**
    * Start every comment with an uppercase letter.
    * Omit terminal periods at the end of single-line and inline comments, using periods only when separating multiple sentences within a multi-clause explanation.
    * Enforce exactly one space between the comment delimiter (`//`, `#`, `/*`) and the comment text.
  * **Tone, Phrasing, and Shorthand**
    * Omit grammatical articles (*a*, *an*, *the*) in short comments to maintain a telegraphic, high-signal style (e.g., `// Bind socket`, `// Read client IP`, `# Extract archive`).
    * Lead with imperative action verbs that describe the immediate operation or state transition (e.g., `// Resolve paths`, `// Verify ptr is valid`, `# Clean existing`).
    * Use common shorthand abbreviations inside comments and logs, such as `w/` for "with", `fwd` for "forward", `ptr` for "pointer", `reg` for "register", `sim` for "simulation", and `opts` for "options".
    * Prefix reusable utility function headers with `// Used to <action>` or `# Returns <value>`.
    * Include occasional candid or dryly humorous parenthetical remarks when documenting architectural quirks (e.g., `// (SLI is dead and we have killed it)`, `# Tuple-ize for fun`).
  * **Placement and Markup Rules**
    * Place descriptive comments on the line directly above the code block, rule definition, or HTML element being documented.
    * Use trailing end-of-line comments on struct/class fields, configuration keys, and register operations to specify units, bit widths, or side effects (e.g., `pos: Vector2; // Cartesian coordinates, in meters`, `tv.tv_sec = 1; // 1 second timeout`).
    * In CSS and HTML, proactively comment logical sections and element groups above their definitions, reserving inline trailing comments (`property: value; /* explanation */`) strictly for explaining non-obvious, property-specific behavior.
    * Mark incomplete implementations or future work with compact tokens (`/* STUB */`, `// TODO - ...`).
  * **Section Banners**
    * Delimit major file sections, compilation stages, or architectural boundaries with full-width banners using asterisks (`/************** ... **************/`), hashes (`######################...######################`), or equals signs (`# ==== Build in Parallel ====`).

* **Module Organization and Import Hygiene**
  * **Grouping and Separation**
    * Group imports into distinct blocks separated by a single blank line in the following order:
      1. Associated compilation-unit header (in C/C++ implementation files, e.g., `#include "server.hpp"`).
      2. Standard library and external third-party packages (e.g., `<iostream>`, `express`, `pathlib`).
      3. Parent or cross-module project headers/imports (e.g., `../conf/conf.hpp`).
      4. Local same-directory or submodule imports (e.g., `./tools.mjs`, `version/handler_1_1.hpp`).
  * **Alphabetization**
    * Alphabetize import statements by module path within each import group.
    * Alphabetize destructured or named symbols within individual import statements (e.g., `import { collectTelemetry, debug, telemetry } from "./tools.mjs";`).

* **Naming Conventions and Identifier Anatomy**
  * **Language-Specific Casing Defaults**
    * **Default (JavaScript, TypeScript, Java, etc.)**: Default to `camelCase` for variables, functions, methods, and properties, and `PascalCase` for classes, interfaces, and type aliases.
    * **C**: Strictly use `snake_case` for functions, variables, structs, and typedefs.
    * **C++**: Use `PascalCase` for classes, structs, enums, and exception types, and maintain a single consistent convention (`camelCase` or `snake_case`) for methods, functions, and variables across a given project.
    * **Python**: Use `snake_case` for functions, variables, and modules, and `PascalCase` for classes and exceptions.
    * **Shell / Makefile**: Use `SCREAMING_SNAKE_CASE` for environment, configuration, and path constants, and lowercase `snake_case` for local script variables and Makefile target names.
  * **Prefixes, Pointers, and Visibility**
    * Prefix raw pointers and smart pointers (`std::unique_ptr`, `SSL*`) with a lowercase `p` (e.g., `pSSL`, `pSSL_CTX`, `pResponse`, `pStream`).
    * Enforce strict encapsulation using native private syntax (`#field` in JS/TS, `private:` in C++), or prefix internal module-scoped variables with underscores (`__animStart`, `_SHIFT_SIGN`) where native private scoping is unavailable.
    * Explicitly qualify instance members and methods with `this->` or `this.` when mutating state or disambiguating from local variables.
  * **Constants and Domain Vocabulary**
    * Use `SCREAMING_SNAKE_CASE` for constants, preprocessor macros, bitmasks, and enum keys, while keeping hardware/protocol acronyms uppercase (`EAX`, `IP`, `TLS`, `MIME`, `WSS_PORT`, `FPS`).
    * Use concise, domain-standard abbreviations for systems and mathematical variables (`addr`, `args`, `cbyte`, `dt`, `dx`/`dy`, `ext`, `fd`, `fname`, `imm`, `inst`, `len`, `pos`, `ptr`, `reg`, `rv`, `sock`, `val`, `vel`, `accel`).

* **Formatting, Spacing, and Code Layout**
  * **Indentation and Bracing**
    * Indent with 4 spaces across all programming languages, HTML, and CSS (using tabs only where required by `Makefile` recipe syntax).
    * Use 1TBS / K&R brace placement, keeping opening braces on the same line as declarations and control statements.
    * Indent C/C++ preprocessor directives (`#ifdef`, `#ifndef`, `#else`, `#endif`, `#define`, `#undef`) to match the surrounding code block's indentation level, and indent statements inside conditional compilation blocks by an additional 4 spaces.
  * **Operator and Parenthesis Spacing**
    * Surround binary arithmetic, bitwise, comparison, and assignment operators with single spaces (e.g., `width / 2`, `100vw * 9 / 16`, `headerEnd + (isSimpleRequest ? 2 : 4)`).
    * Apply selective internal space padding inside parentheses around nested function calls, type casts, or resource acquisitions to improve visual legibility (e.g., `fs.readFileSync( configPath, "utf-8" )`, `static_cast<inst>( this->nextByte(mem) )`, `version=$( cat ... )`).
  * **Compact Constructs and Alignment**
    * Format trivial getters, setters, state checks, and single-action `switch`/`match` cases as single-line statements.
    * Chain related scalar initializations or coordinate resets on a single line (`EAX = EBX = ECX = EDX = {0};`, `this.accel.x = this.accel.y = 0;`, `let numReqs = 0, numResolved = 0;`).
    * Vertically align parallel assignments, enum tables, register dumps, and adjacent conditional branches when symmetry aids scanning.

* **Operators, Control Flow, and Execution Lifecycle**
  * **Increment and Decrement Operators**
    * Default to pre-increment and pre-decrement (`++i`, `--c`, `--requestsLeft`, `++numReqs`) in loops and standalone statements.
    * Use post-increment (`i++`) strictly when the expression requires the pre-mutation return value (e.g., `this->IP.dword++`, `this.bodies[i--] = ...`, `if (frameNo++ === TRACK_ANIM_NUM_FRAMES)`).
  * **Guard Clauses and Early Exits**
    * Flatten control flow with early returns, `continue`, or `break` statements at the top of functions and loop bodies.
    * Keep single-statement guard clauses on a single line without braces (`if (!this.#isVisible) return;`, `if (client < 0) continue;`, `if len(line) == 0: continue`).
  * **Scoping, RAII, and Local Helpers**
    * Use anonymous lexical blocks (`{ ... }`) to bound the lifetime of RAII mutex locks (`std::unique_lock lock(clientsMutex);`) and isolate temporary initialization variables.
    * Define local lambdas or closures inside complex orchestration methods (e.g., `readMore`, `reject`, `bindReq`) to encapsulate repetitive stateful helpers that do not belong on the class interface.
  * **Fail-Fast Validation**
    * Validate CLI arguments, file paths, sockets, and library dependencies upfront, logging actionable diagnostics (e.g., checking `errno == 13` for elevated permissions) and exiting immediately on fatal setup errors.

* **Data Modeling, Literals, and Low-Level Idioms**
  * **Literals and Formatting**
    * Use double quotes (`"..."`) consistently for strings, paths, dictionary keys, HTML attributes, and CSS urls, using single quotes (`'...'`) in C++ for single `char` literals (`'\n'`, `' '`, `'/'`).
    * Express large or small powers of ten in scientific notation (`1e3`, `6.6743e-11`) and bitmasks in hexadecimal (`0x8000'0000`, `0xFF`), using digit separators (`_` or `'`) for readability.
  * **Bitwise Operations and Type Safety**
    * Use bitwise operators (`&`, `|`, `^`, `~`, `<<`, `>>`) for flag manipulation and endian serialization.
    * Prefer explicit C++ casts (`static_cast`, `std::bit_cast`) and template trait structs (`ALUTraits<U>`) over untyped macros when handling multi-width arithmetic.
    * When using preprocessor macros to eliminate repetitive switch/dispatch boilerplate, define them locally and `#undef` them immediately after the block.

* **Stylesheet (CSS) and Markup (HTML) Conventions**
  * **CSS Property Clustering**
    * Group declarations within CSS rules into logical clusters separated by a single blank line:
      1. Box sizing and dimensions (`width`, `height`, `min-*`, `max-*`).
      2. Display and flex/grid layout (`display`, `flex-*`, `justify-content`, `align-items`, `column-gap`).
      3. Positioning and spacing (`position`, `top`, `left`, `right`, `bottom`, `margin`, `padding`).
      4. Visual styling and typography (`background-*`, `border-*`, `box-shadow`, `color`, `font`, `text-shadow`).
      5. Interactivity, stacking, and motion (`overflow`, `cursor`, `pointer-events`, `opacity`, `z-index`, `transition`, `animation`).
  * **CSS Nesting and Variables**
    * Centralize theme palettes and responsive scaling math in `:root` custom properties (`--theme-*`, `--card-height`).
    * Use native CSS nesting (`&`) when selector chains exceed 2 levels of depth so rule blocks fold cleanly in the IDE.
  * **HTML Structure**
    * Use semantic lowercase tags, 4-space indentation, and dedicated state/utility classes (`.no-select`, `.overlay`, `.covered`) decoupled from JS ID hooks.

* **Build Systems and Shell Scripting**
  * **Portability and Safety**
    * Start executable scripts with `#!/usr/bin/env bash` or `#!/usr/bin/env python3` and set `SHELL := /bin/bash` in Makefiles.
    * Enable fail-fast execution in shell scripts (`set -e`), use `flock` for concurrent directory safety, and compile C++ with strict warnings (`-Wall -Wextra -Werror`).
  * **Output and Parallelism**
    * Suppress noisy build commands using `@` prefixes in Makefiles and `> /dev/null` redirects in scripts, surfacing clean terminal milestones prefixed with `✅` (e.g., `✅ Built for Linux.`).
    * Execute independent build targets concurrently using backgrounded subshells `( ... ) &` synchronized with `wait`.
