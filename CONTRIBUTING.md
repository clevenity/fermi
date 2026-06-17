Contributing to Fermi

Thank you for your interest in contributing to Fermi. Contributions are welcome and help improve the language, compiler, runtime, standard library, and documentation.

Please read the guidelines below before opening an issue or submitting a pull request.

---

Reporting Issues

When reporting a bug, please provide enough information to reproduce the problem.

Include:

- Fermi version.
- Operating system and architecture.
- Source code that reproduces the issue.
- Expected behavior.
- Actual behavior.
- Error messages or compiler output.

Issues without enough information may require additional clarification before they can be addressed.

---

Feature Requests

Before proposing a new feature, consider:

- The problem it solves.
- The proposed syntax or API.
- The impact on the language and compiler.
- Possible alternatives.

Large language changes should be discussed before implementation.

---

Pull Requests

Before submitting a pull request, ensure that:

- The project builds successfully.
- Existing tests pass.
- New functionality includes tests when appropriate.
- Documentation is updated when necessary.
- The changes follow the existing project style.

Prefer small and focused pull requests over large unrelated changes.

---

Code Quality

General guidelines:

- Write clear and maintainable code.
- Prefer simple implementations.
- Avoid unnecessary abstractions.
- Keep functions and modules focused on a single responsibility.
- Remove dead code and unnecessary comments.
- Follow the existing coding style of the project.

---

Compiler Architecture

Fermi's compiler is organized into separate stages.

Typical pipeline:

Source Code
    |
    v
Lexer
    |
    v
Parser
    |
    v
HIR
    |
    v
Semantic Analysis
    |
    v
Code Generation

Changes should respect the responsibility of each stage. Avoid introducing logic into the wrong compiler component.

---

Language Changes

Changes involving:

- Syntax
- Type system
- Keywords
- Standard library APIs
- Runtime behavior

require careful consideration.

When proposing a language change, explain:

- Why the change is necessary.
- How it affects existing code.
- The implementation complexity.
- Long-term maintenance considerations.

---

Performance

Performance-related changes should be supported by measurements whenever possible.

Avoid assumptions such as "this should be faster".

Include benchmark results when submitting significant optimizations.

---

Documentation

Documentation should be updated when changes affect:

- Language syntax.
- Compiler behavior.
- Runtime behavior.
- Standard library APIs.
- Build or installation procedures.

Documentation and implementation should remain synchronized.

---

Commit Messages

Write clear and meaningful commit messages.

Examples:

lexer: fix incorrect string literal parsing

parser: add support for range expressions

runtime: optimize integer formatting

Avoid vague messages such as:

fix bug

update code

changes

---

Communication

Please keep discussions respectful and focused on technical topics.

Not all proposed changes will be accepted. Decisions are based on correctness, maintainability, compatibility, performance, and the overall direction of the project.

---

License

By submitting code, documentation, or other contributions to Fermi, you agree that your contribution will be distributed under the same license as the Fermi project.